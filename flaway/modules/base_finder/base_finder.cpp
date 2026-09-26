#include "base_finder.h"
#include "../../flaway.h"
#include "../../globals/globals.h"
#include "../../gui/GUI.h"
#include <sdk/minecraft/minecraft.h>
#include <sdk/minecraft/entity/entity.h>
#include <sdk/classloader.h>
#include <sdk/mappings/mappings.hpp>
#include <sdk/projection.h>
#include "../../utils/logger.h"
#include <cmath>
#include <cstring>
#include <cfloat>
#include <vector>
#include <unordered_set>
#include <mutex>
#include <chrono>

namespace
{
	// Position packing: 21 bits per axis supports |coord| < 2^20 (plenty for any
	// Minecraft world) in a single uint64 so we can use one set/vector per scan.
	constexpr int kPosBits = 21;
	constexpr int kPosMask = (1 << kPosBits) - 1;
	constexpr int kPosSign = 1 << (kPosBits - 1);

	inline uint64_t pack_pos(int x, int y, int z)
	{
		return ((uint64_t)(x & kPosMask) << 42) | ((uint64_t)(y & kPosMask) << 21) | (uint64_t)(z & kPosMask);
	}

	inline void unpack_pos(uint64_t k, int& x, int& y, int& z)
	{
		x = (int)((k >> 42) & kPosMask); if (x & kPosSign) x -= (1 << kPosBits);
		y = (int)((k >> 21) & kPosMask); if (y & kPosSign) y -= (1 << kPosBits);
		z = (int)(k & kPosMask);         if (z & kPosSign) z -= (1 << kPosBits);
	}

	struct scan_area
	{
		int minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0, maxZ = 0;

		bool contains(int x, int y, int z) const
		{
			return x >= minX && x <= maxX && y >= minY && y <= maxY && z >= minZ && z <= maxZ;
		}
	};

	struct cave_result
	{
		std::vector<uint64_t> blocks;
		std::vector<uint64_t> border_blocks;
		int size = 0;
		int minX = INT_MAX, maxX = INT_MIN, minY = INT_MAX, maxY = INT_MIN, minZ = INT_MAX, maxZ = INT_MIN;
		bool has_border = false;
		bool is_closed = false;
		bool overflow = false;
	};

	// ---- Cached JNI resources (resolved once, reused across frames) -----------
	struct base_finder_jni
	{
		jclass world_class = nullptr;                 // class_638 [global]
		jmethodID get_block_state = nullptr;          // method_8320
		jmethodID is_air = nullptr;                   // method_22347
		jmethodID is_chunk_loaded_ii = nullptr;       // method_8393
		jmethodID is_chunk_loaded_bp = nullptr;       // method_22340
		jmethodID get_bottom_y = nullptr;             // method_31607
		jmethodID get_top_y = nullptr;                // method_31600
		jmethodID get_light_level = nullptr;          // method_8314
		jclass light_type_class = nullptr;            // class_1944 [global]
		jfieldID light_type_block = nullptr;          // field_9282
		jobject light_type_block_obj = nullptr;       // LightType.BLOCK [global]
		jclass block_state_class = nullptr;           // class_4970$class_4971 [global]
		jmethodID state_get_block = nullptr;          // method_26204
		jmethodID state_is_replaceable = nullptr;     // method_45474
		jclass block_class = nullptr;                 // class_2248 [global]
		jmethodID block_get_translation_key = nullptr; // method_9539 (fallback to field)
		jfieldID block_translation_key = nullptr;     // field_10642
		// click mode
		jclass block_pos_class = nullptr;             // class_2338 [global]
		jmethodID block_pos_ctor = nullptr;
		jclass vec3d_class = nullptr;                 // class_243 [global]
		jmethodID vec3d_ctor = nullptr;
		jclass direction_class = nullptr;             // class_2350 [global]
		jfieldID direction_up = nullptr;              // field_11036
		jclass bhr_class = nullptr;                   // class_3965 [global]
		jmethodID bhr_ctor = nullptr;
		jclass im_class = nullptr;                    // class_636 [global]
		jmethodID interact_block = nullptr;           // method_2896
		jclass hand_class = nullptr;                  // class_1268 [global]
		jfieldID hand_main = nullptr;                 // field_5808
	};

	base_finder_jni g_jni;
	bool g_cached = false;

	// ---- Shared output state (written by scan, read by draw/click) -----------
	std::mutex g_mutex;
	std::vector<base_finder_block> g_cave_blocks;
	std::vector<base_finder_block> g_solid_blocks;
	std::vector<base_finder_block> g_click_queue;

	// ---- Scanner state machines --------------------------------------------------
	struct cave_scanner_state
	{
		bool active = false;
		bool finished = false;
		scan_area area;
		int x = 0, y = 0, z = 0;
		std::unordered_set<uint64_t> visited;
		std::vector<uint64_t> found;
		long long finished_us = 0;
	};

	struct bypass_scanner_state
	{
		bool active = false;
		bool finished = false;
		scan_area area;
		int x = 0, y = 0, z = 0;
		std::vector<uint64_t> found;
		long long finished_us = 0;
	};

	cave_scanner_state g_cave_scanner;
	bypass_scanner_state g_bypass_scanner;
	double g_last_px = 0.0, g_last_py = 0.0, g_last_pz = 0.0;
	bool g_force_restart = false;
	int g_frame_counter = 0;

	long long now_us()
	{
		return std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	bool scan_advance(const scan_area& a, int& x, int& y, int& z)
	{
		z++;
		if (z > a.maxZ) { z = a.minZ; y++; }
		if (y > a.maxY) { y = a.minY; x++; }
		if (x > a.maxX) return true;
		return false;
	}

	// ---- JNI resolution -----------------------------------------------------------
	bool resolve_jni(JNIEnv* env)
	{
		if (g_cached) return g_jni.world_class != nullptr && g_jni.get_block_state != nullptr;

		jclass local = nullptr;

		auto promote_local = [&](jclass& slot)
		{
			if (local && slot == nullptr)
				slot = reinterpret_cast<jclass>(env->NewGlobalRef(local));
			if (local) env->DeleteLocalRef(local);
			local = nullptr;
			return slot != nullptr;
		};

		local = sdk::classloader::find_class(env, sdk::mappings::client_world_class_sig);
		promote_local(g_jni.world_class);
		if (g_jni.world_class)
		{
			g_jni.get_block_state = env->GetMethodID(g_jni.world_class,
				sdk::mappings::world_get_block_state_name, sdk::mappings::world_get_block_state_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			g_jni.is_air = env->GetMethodID(g_jni.world_class,
				sdk::mappings::world_is_air_name, sdk::mappings::world_is_air_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			g_jni.is_chunk_loaded_ii = env->GetMethodID(g_jni.world_class,
				sdk::mappings::world_is_chunk_loaded_ii_name, sdk::mappings::world_is_chunk_loaded_ii_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			g_jni.is_chunk_loaded_bp = env->GetMethodID(g_jni.world_class,
				sdk::mappings::world_is_chunk_loaded_bp_name, sdk::mappings::world_is_chunk_loaded_bp_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			g_jni.get_bottom_y = env->GetMethodID(g_jni.world_class,
				sdk::mappings::world_get_bottom_y_name, sdk::mappings::world_get_bottom_y_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			g_jni.get_top_y = env->GetMethodID(g_jni.world_class,
				sdk::mappings::world_get_top_y_inclusive_name, sdk::mappings::world_get_top_y_inclusive_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			g_jni.get_light_level = env->GetMethodID(g_jni.world_class,
				sdk::mappings::world_get_light_level_name, sdk::mappings::world_get_light_level_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
		}

		local = sdk::classloader::find_class(env, sdk::mappings::light_type_class_sig);
		promote_local(g_jni.light_type_class);
		if (g_jni.light_type_class)
		{
			g_jni.light_type_block = env->GetStaticFieldID(g_jni.light_type_class,
				sdk::mappings::light_type_block_field, sdk::mappings::light_type_block_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			if (g_jni.light_type_block)
			{
				jobject light = env->GetStaticObjectField(g_jni.light_type_class, g_jni.light_type_block);
				if (env->ExceptionCheck()) env->ExceptionClear();
				if (light)
				{
					g_jni.light_type_block_obj = env->NewGlobalRef(light);
					env->DeleteLocalRef(light);
				}
			}
		}

		local = sdk::classloader::find_class(env, sdk::mappings::block_state_class_sig);
		promote_local(g_jni.block_state_class);
		if (g_jni.block_state_class)
		{
			g_jni.state_get_block = env->GetMethodID(g_jni.block_state_class,
				sdk::mappings::block_state_get_block_name, sdk::mappings::block_state_get_block_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			g_jni.state_is_replaceable = env->GetMethodID(g_jni.block_state_class,
				sdk::mappings::block_state_is_replaceable_name, sdk::mappings::block_state_is_replaceable_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
		}

		local = sdk::classloader::find_class(env, sdk::mappings::block_class_sig);
		promote_local(g_jni.block_class);
		if (g_jni.block_class)
		{
			g_jni.block_get_translation_key = env->GetMethodID(g_jni.block_class,
				sdk::mappings::block_get_translation_key_name, sdk::mappings::block_get_translation_key_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			g_jni.block_translation_key = env->GetFieldID(g_jni.block_class,
				sdk::mappings::block_translation_key_field, sdk::mappings::block_translation_key_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
		}

		// click-mode caches
		local = sdk::classloader::find_class(env, sdk::mappings::block_pos_class_sig);
		promote_local(g_jni.block_pos_class);
		if (g_jni.block_pos_class)
		{
			g_jni.block_pos_ctor = env->GetMethodID(g_jni.block_pos_class, "<init>",
				sdk::mappings::block_pos_ctor_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
		}

		local = sdk::classloader::find_class(env, sdk::mappings::vec3d_class_sig);
		promote_local(g_jni.vec3d_class);
		if (g_jni.vec3d_class)
		{
			g_jni.vec3d_ctor = env->GetMethodID(g_jni.vec3d_class, "<init>",
				sdk::mappings::vec3d_ctor_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
		}

		local = sdk::classloader::find_class(env, sdk::mappings::direction_class_sig);
		promote_local(g_jni.direction_class);
		if (g_jni.direction_class)
		{
			g_jni.direction_up = env->GetStaticFieldID(g_jni.direction_class,
				sdk::mappings::direction_up_name, sdk::mappings::direction_up_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
		}

		local = sdk::classloader::find_class(env, sdk::mappings::block_hit_result_class_sig);
		promote_local(g_jni.bhr_class);
		if (g_jni.bhr_class)
		{
			g_jni.bhr_ctor = env->GetMethodID(g_jni.bhr_class, "<init>",
				sdk::mappings::block_hit_result_ctor_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
		}

		local = sdk::classloader::find_class(env, sdk::mappings::interaction_manager_class_sig);
		promote_local(g_jni.im_class);
		if (g_jni.im_class)
		{
			g_jni.interact_block = env->GetMethodID(g_jni.im_class,
				sdk::mappings::interact_block_name, sdk::mappings::interact_block_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
		}

		local = sdk::classloader::find_class(env, sdk::mappings::hand_class_sig);
		promote_local(g_jni.hand_class);
		if (g_jni.hand_class)
		{
			g_jni.hand_main = env->GetStaticFieldID(g_jni.hand_class,
				sdk::mappings::hand_main_hand_name, sdk::mappings::hand_main_hand_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
		}

		g_cached = true;
		return g_jni.world_class != nullptr && g_jni.get_block_state != nullptr;
	}

	jobject make_block_pos(JNIEnv* env, int x, int y, int z)
	{
		if (!g_jni.block_pos_class || !g_jni.block_pos_ctor) return nullptr;
		jobject pos = env->NewObject(g_jni.block_pos_class, g_jni.block_pos_ctor, x, y, z);
		if (env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }
		return pos;
	}

	bool block_is_air(JNIEnv* env, jobject world, int x, int y, int z)
	{
		if (!g_jni.is_air) return false;
		jobject pos = make_block_pos(env, x, y, z);
		if (!pos) return false;
		jboolean r = env->CallBooleanMethod(world, g_jni.is_air, pos);
		env->DeleteLocalRef(pos);
		if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
		return r == JNI_TRUE;
	}

	bool chunk_loaded(JNIEnv* env, jobject world, int x, int z)
	{
		if (!g_jni.is_chunk_loaded_ii) return false;
		jboolean r = env->CallBooleanMethod(world, g_jni.is_chunk_loaded_ii, x >> 4, z >> 4);
		if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
		return r == JNI_TRUE;
	}

	jobject get_block_state_at(JNIEnv* env, jobject world, int x, int y, int z)
	{
		if (!g_jni.get_block_state) return nullptr;
		jobject pos = make_block_pos(env, x, y, z);
		if (!pos) return nullptr;
		jobject st = env->CallObjectMethod(world, g_jni.get_block_state, pos);
		env->DeleteLocalRef(pos);
		if (env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }
		return st;
	}

	bool is_replaceable(JNIEnv* env, jobject world, int x, int y, int z)
	{
		if (!g_jni.state_is_replaceable) return false;
		jobject st = get_block_state_at(env, world, x, y, z);
		if (!st) return false;
		jboolean r = env->CallBooleanMethod(st, g_jni.state_is_replaceable);
		env->DeleteLocalRef(st);
		if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
		return r == JNI_TRUE;
	}

	// Returns 0 = other, 1 = netherrack, 2 = stone, 3 = lava
	int block_kind(JNIEnv* env, jobject state)
	{
		if (!state) return 0;
		if (!g_jni.state_get_block) return 0;
		jobject block = env->CallObjectMethod(state, g_jni.state_get_block);
		if (env->ExceptionCheck()) { env->ExceptionClear(); return 0; }
		if (!block) return 0;

		int kind = 0;
		jobject key = nullptr;
		if (g_jni.block_get_translation_key)
		{
			key = env->CallObjectMethod(block, g_jni.block_get_translation_key);
			if (env->ExceptionCheck()) env->ExceptionClear();
		}
		if (!key && g_jni.block_translation_key)
		{
			key = env->GetObjectField(block, g_jni.block_translation_key);
			if (env->ExceptionCheck()) env->ExceptionClear();
		}
		env->DeleteLocalRef(block);
		if (key)
		{
			const char* s = env->GetStringUTFChars((jstring)key, nullptr);
			if (s)
			{
				if (strcmp(s, "block.minecraft.netherrack") == 0) kind = 1;
				else if (strcmp(s, "block.minecraft.stone") == 0) kind = 2;
				else if (strcmp(s, "block.minecraft.lava") == 0) kind = 3;
				env->ReleaseStringUTFChars((jstring)key, s);
			}
			env->DeleteLocalRef(key);
		}
		return kind;
	}

	bool is_valid_block(JNIEnv* env, jobject world, jobject state, int x, int y, int z)
	{
		if (!g_jni.get_light_level || !g_jni.light_type_block_obj) return false;
		jobject pos = make_block_pos(env, x, y, z);
		if (!pos) return false;
		jint light = env->CallIntMethod(world, g_jni.get_light_level, g_jni.light_type_block_obj, pos);
		env->DeleteLocalRef(pos);
		if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }

		if (globals::base_finder_holy_world)
		{
			if (light <= 5) return false;
			return block_kind(env, state) != 3; // anything but lava
		}
		if (light == 0) return false;
		int kind = block_kind(env, state);
		return kind == 1 || kind == 2; // netherrack || stone
	}

	// ---- Cave scanning (BFS over air, mirrors Zenith BlockScanner) -----------
	cave_result scan_cave(JNIEnv* env, jobject world, const scan_area& area,
		std::unordered_set<uint64_t>& visited, uint64_t start, int max_size,
		long long frame_start)
	{
		cave_result r;
		std::vector<uint64_t> stack;
		stack.push_back(start);
		visited.insert(start);

		int hard_budget = 200000;
		static const int dx[6] = {1, -1, 0, 0, 0, 0};
		static const int dy[6] = {0, 0, 1, -1, 0, 0};
		static const int dz[6] = {0, 0, 0, 0, 1, -1};

		while (!stack.empty() && hard_budget-- > 0 && now_us() - frame_start < 3000)
		{
			uint64_t k = stack.back();
			stack.pop_back();
			int x, y, z;
			unpack_pos(k, x, y, z);

			r.blocks.push_back(k);
			r.size++;
			if (x < r.minX) r.minX = x;
			if (x > r.maxX) r.maxX = x;
			if (y < r.minY) r.minY = y;
			if (y > r.maxY) r.maxY = y;
			if (z < r.minZ) r.minZ = z;
			if (z > r.maxZ) r.maxZ = z;
			if (r.size > max_size)
			{
				r.overflow = true;
				break;
			}

			for (int i = 0; i < 6; i++)
			{
				int nx = x + dx[i], ny = y + dy[i], nz = z + dz[i];
				if (!area.contains(nx, ny, nz) || !chunk_loaded(env, world, nx, nz))
				{
					r.has_border = true;
					r.border_blocks.push_back(k);
					continue;
				}
				uint64_t nk = pack_pos(nx, ny, nz);
				if (visited.count(nk)) continue;
				if (block_is_air(env, world, nx, ny, nz))
				{
					visited.insert(nk);
					stack.push_back(nk);
				}
				else if (is_replaceable(env, world, nx, ny, nz))
				{
					r.has_border = true;
					r.border_blocks.push_back(nk);
				}
			}
		}

		if (r.overflow) return r;

		// Time budget exhausted mid-BFS: the result is partial, so discard it
		// (a half-scanned cave must not be validated/rendered). The blocks are
		// already in `visited`, so the scan simply resumes past them.
		if (now_us() - frame_start >= 3000) r.overflow = true;
		if (r.overflow) return r;

		// isClosed: every neighbor must be loaded and either in the cave or solid
		if (r.size > 0)
		{
			std::unordered_set<uint64_t> in_set(r.blocks.begin(), r.blocks.end());
			bool closed = true;
			for (uint64_t k : r.blocks)
			{
				int x, y, z;
				unpack_pos(k, x, y, z);
				for (int i = 0; i < 6 && closed; i++)
				{
					int nx = x + dx[i], ny = y + dy[i], nz = z + dz[i];
					if (!chunk_loaded(env, world, nx, nz))
					{
						closed = false;
						break;
					}
					uint64_t nk = pack_pos(nx, ny, nz);
					if (!in_set.count(nk) && block_is_air(env, world, nx, ny, nz))
					{
						closed = false;
						break;
					}
				}
				if (!closed) break;
			}
			r.is_closed = closed;
		}
		return r;
	}

	bool is_valid_cave(const cave_result& r)
	{
		int min_size = globals::base_finder_min_size;
		int max_size = globals::base_finder_max_size;
		if (r.overflow) return false;
		if (r.size < min_size || r.size > max_size) return false;
		int len = r.maxX - r.minX + 1;
		int wid = r.maxZ - r.minZ + 1;
		if (len < globals::base_finder_min_length) return false;
		if (wid < globals::base_finder_min_width) return false;
		return r.has_border || r.is_closed;
	}

	void process_cave_chunk(JNIEnv* env, jobject world, cave_scanner_state& cs)
	{
		bool bypass_mode = globals::base_finder_mode == 1 || globals::base_finder_mode == 2;
		int seed_limit = bypass_mode ? 1536 : 3072;
		int processed = 0;
		int bfs_budget = 800;
		long long frame_start = now_us();

		while (processed < seed_limit && !cs.finished && now_us() - frame_start < 3000)
		{
			if (chunk_loaded(env, world, cs.x, cs.z))
			{
				uint64_t sk = pack_pos(cs.x, cs.y, cs.z);
				if (!cs.visited.count(sk) && block_is_air(env, world, cs.x, cs.y, cs.z))
				{
					cave_result r = scan_cave(env, world, cs.area, cs.visited, sk, globals::base_finder_max_size, frame_start);
					bfs_budget -= r.size;
					if (is_valid_cave(r))
					{
						cs.found.insert(cs.found.end(), r.blocks.begin(), r.blocks.end());
						if (bypass_mode && r.has_border)
							cs.found.insert(cs.found.end(), r.border_blocks.begin(), r.border_blocks.end());
					}
					if (bfs_budget <= 0) break;
				}
			}
			if (scan_advance(cs.area, cs.x, cs.y, cs.z)) cs.finished = true;
			processed++;
		}

		if (cs.finished)
		{
			std::unordered_set<uint64_t> dedupe(cs.found.begin(), cs.found.end());
			std::vector<base_finder_block> out;
			out.reserve(dedupe.size());
			for (uint64_t k : dedupe)
			{
				int x, y, z;
				unpack_pos(k, x, y, z);
				out.push_back({x, y, z});
			}
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_cave_blocks = std::move(out);
			}
			cs.active = false;
			cs.finished_us = now_us();
			if (globals::debug_logging_enabled)
				logger::log("[base_finder] cave scan done: " + std::to_string(g_cave_blocks.size()) + " blocks");
		}
	}

	// ---- Bypass scanning (light check per block, mirrors Zenith CaveScanner) --
	std::vector<uint64_t> group_clusters(const std::vector<uint64_t>& keys, int max_cluster)
	{
		std::unordered_set<uint64_t> remaining(keys.begin(), keys.end());
		std::vector<uint64_t> out;
		static const int dx[6] = {1, -1, 0, 0, 0, 0};
		static const int dy[6] = {0, 0, 1, -1, 0, 0};
		static const int dz[6] = {0, 0, 0, 0, 1, -1};

		while (!remaining.empty())
		{
			uint64_t start = *remaining.begin();
			remaining.erase(start);
			std::vector<uint64_t> cluster;
			std::vector<uint64_t> stack;
			stack.push_back(start);
			while (!stack.empty())
			{
				uint64_t k = stack.back();
				stack.pop_back();
				cluster.push_back(k);
				int x, y, z;
				unpack_pos(k, x, y, z);
				for (int i = 0; i < 6; i++)
				{
					uint64_t nk = pack_pos(x + dx[i], y + dy[i], z + dz[i]);
					auto it = remaining.find(nk);
					if (it != remaining.end())
					{
						remaining.erase(it);
						stack.push_back(nk);
					}
				}
			}
			if ((int)cluster.size() <= max_cluster)
				out.insert(out.end(), cluster.begin(), cluster.end());
		}
		return out;
	}

	void process_bypass_chunk(JNIEnv* env, jobject world, bypass_scanner_state& bs)
	{
		bool cave_mode = globals::base_finder_mode == 0 || globals::base_finder_mode == 2;
		int block_limit = cave_mode ? 4096 : 8192;
		int processed = 0;
		long long frame_start = now_us();

		while (processed < block_limit && !bs.finished && now_us() - frame_start < 3000)
		{
			if (chunk_loaded(env, world, bs.x, bs.z))
			{
				jobject st = get_block_state_at(env, world, bs.x, bs.y, bs.z);
				if (st)
				{
					if (is_valid_block(env, world, st, bs.x, bs.y, bs.z))
						bs.found.push_back(pack_pos(bs.x, bs.y, bs.z));
					env->DeleteLocalRef(st);
				}
			}
			if (scan_advance(bs.area, bs.x, bs.y, bs.z)) bs.finished = true;
			processed++;
		}

		if (bs.finished)
		{
			std::vector<uint64_t> filtered = globals::base_finder_holy_world
				? group_clusters(bs.found, 20)
				: bs.found;

			std::vector<base_finder_block> out;
			out.reserve(filtered.size());
			for (uint64_t k : filtered)
			{
				int x, y, z;
				unpack_pos(k, x, y, z);
				out.push_back({x, y, z});
			}
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_solid_blocks = out;
				if (globals::base_finder_click) g_click_queue = out;
				else g_click_queue.clear();
			}
			bs.active = false;
			bs.finished_us = now_us();
			if (globals::debug_logging_enabled)
				logger::log("[base_finder] bypass scan done: " + std::to_string(g_solid_blocks.size()) + " blocks");
		}
	}

	void build_area(JNIEnv* env, jobject world, double px, double py, double pz, scan_area& out)
	{
		int r = (int)globals::base_finder_range;
		int bottom = 0, top = 256;
		if (g_jni.get_bottom_y)
		{
			bottom = env->CallIntMethod(world, g_jni.get_bottom_y);
			if (env->ExceptionCheck()) env->ExceptionClear();
		}
		if (g_jni.get_top_y)
		{
			top = env->CallIntMethod(world, g_jni.get_top_y);
			if (env->ExceptionCheck()) env->ExceptionClear();
		}
		out.minX = (int)floor(px - r);
		out.maxX = (int)ceil(px + r);
		out.minY = bottom + 1;
		out.maxY = top;
		out.minZ = (int)floor(pz - r);
		out.maxZ = (int)ceil(pz + r);
		if (out.maxY < out.minY) out.maxY = out.minY;
	}

	void start_cave_scanner(JNIEnv* env, jobject world, double px, double py, double pz)
	{
		build_area(env, world, px, py, pz, g_cave_scanner.area);
		g_cave_scanner.x = g_cave_scanner.area.minX;
		g_cave_scanner.y = g_cave_scanner.area.minY;
		g_cave_scanner.z = g_cave_scanner.area.minZ;
		g_cave_scanner.finished = false;
		g_cave_scanner.active = true;
		g_cave_scanner.visited.clear();
		g_cave_scanner.found.clear();
	}

	void start_bypass_scanner(JNIEnv* env, jobject world, double px, double py, double pz)
	{
		build_area(env, world, px, py, pz, g_bypass_scanner.area);
		g_bypass_scanner.x = g_bypass_scanner.area.minX;
		g_bypass_scanner.y = g_bypass_scanner.area.minY;
		g_bypass_scanner.z = g_bypass_scanner.area.minZ;
		g_bypass_scanner.finished = false;
		g_bypass_scanner.active = true;
		g_bypass_scanner.found.clear();
	}

	// ---- Click mode: place a block at found positions (via interactBlock) ----
	void process_clicks(JNIEnv* env, jobject world, jobject player)
	{
		if (!globals::base_finder_click) return;
		if (!g_jni.im_class || !g_jni.interact_block || !g_jni.bhr_ctor ||
			!g_jni.direction_up || !g_jni.block_pos_ctor || !g_jni.vec3d_ctor || !g_jni.hand_main)
			return;

		jobject im = sdk::instance->get_interaction_manager();
		if (!im) return;

		jobject up = env->GetStaticObjectField(g_jni.direction_class, g_jni.direction_up);
		if (env->ExceptionCheck()) env->ExceptionClear();
		jobject hand = env->GetStaticObjectField(g_jni.hand_class, g_jni.hand_main);
		if (env->ExceptionCheck()) env->ExceptionClear();

		int clicks = 0;
		while (clicks < 3)
		{
			base_finder_block b;
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				if (g_click_queue.empty()) break;
				b = g_click_queue.back();
				g_click_queue.pop_back();
			}
			jobject vec = env->NewObject(g_jni.vec3d_class, g_jni.vec3d_ctor,
				(jdouble)b.x + 0.5, (jdouble)b.y + 0.5, (jdouble)b.z + 0.5);
			if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
			if (!vec) break;
			jobject pos = env->NewObject(g_jni.block_pos_class, g_jni.block_pos_ctor, b.x, b.y, b.z);
			if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(vec); break; }
			if (!pos) { env->DeleteLocalRef(vec); break; }
			jobject bhr = env->NewObject(g_jni.bhr_class, g_jni.bhr_ctor, vec, up, pos, JNI_FALSE);
			env->DeleteLocalRef(vec);
			env->DeleteLocalRef(pos);
			if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
			if (!bhr) break;
			env->CallObjectMethod(im, g_jni.interact_block, player, hand, bhr);
			if (env->ExceptionCheck()) env->ExceptionClear();
			env->DeleteLocalRef(bhr);
			clicks++;
		}

		env->DeleteLocalRef(hand);
		env->DeleteLocalRef(up);
		env->DeleteLocalRef(im);
	}
}

void flaway::modules::base_finder::run()
{
		try
		{
			if (!globals::base_finder_enabled)
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_cave_blocks.clear();
				g_solid_blocks.clear();
				g_click_queue.clear();
				g_cave_scanner = {};
				g_bypass_scanner = {};
				g_force_restart = false;
				return;
			}

			auto env = flaway::instance->get_env();
			if (!env) return;
			if (!resolve_jni(env)) return;

			jobject world = sdk::instance->get_world();
			if (!world) return;
			jobject local_player = sdk::instance->get_player();
			if (!local_player)
			{
				env->DeleteLocalRef(world);
				return;
			}

			sdk::entity_client local_entity(local_player);
			double px = local_entity.get_x();
			double py = local_entity.get_y();
			double pz = local_entity.get_z();

			bool cave_mode = globals::base_finder_mode == 0 || globals::base_finder_mode == 2;
			bool bypass_mode = globals::base_finder_mode == 1 || globals::base_finder_mode == 2;

			g_frame_counter++;
			if (g_frame_counter % 10 != 0)
			{
				env->DeleteLocalRef(world);
				env->DeleteLocalRef(local_player);
				return;
			}

			double ddx = px - g_last_px, ddz = pz - g_last_pz;
			double dy = py - g_last_py;
			if (ddx * ddx + ddz * ddz > 64.0 || fabs(dy) > 8.0)
			{
				g_last_px = px; g_last_py = py; g_last_pz = pz;
				g_force_restart = true;
			}

			long long now = now_us();
			if (cave_mode && !g_cave_scanner.active &&
				(g_force_restart || now - g_cave_scanner.finished_us >= 1000000))
				start_cave_scanner(env, world, px, py, pz);
			if (bypass_mode && !g_bypass_scanner.active &&
				(g_force_restart || now - g_bypass_scanner.finished_us >= 1000000))
				start_bypass_scanner(env, world, px, py, pz);
			g_force_restart = false;

			if (cave_mode && g_cave_scanner.active)
				process_cave_chunk(env, world, g_cave_scanner);
			else if (!cave_mode)
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_cave_blocks.clear();
			}

			if (bypass_mode && g_bypass_scanner.active)
				process_bypass_chunk(env, world, g_bypass_scanner);
			else if (!bypass_mode)
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_solid_blocks.clear();
				g_click_queue.clear();
			}

			process_clicks(env, world, local_player);

			env->DeleteLocalRef(world);
			env->DeleteLocalRef(local_player);
		}
		catch (...)
		{
		}
}

void flaway::modules::base_finder::draw_boxes()
{
		if (!globals::base_finder_enabled) return;
		if (!GUI::get_is_init()) return;
		if (sdk::instance && sdk::instance->is_screen_open()) return;

		ImGuiIO& io = ImGui::GetIO();
		int screen_width = (int)io.DisplaySize.x;
		int screen_height = (int)io.DisplaySize.y;
		if (screen_width <= 0 || screen_height <= 0) return;

		ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
		if (!draw_list) return;

		std::vector<base_finder_block> caves, solid;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			caves = g_cave_blocks;
			solid = g_solid_blocks;
		}

		sdk::camera_data cam = sdk::instance->get_camera();
		if (!cam.valid) return;
		projection::set_view((float)cam.x, (float)cam.y, (float)cam.z,
			cam.yaw, cam.pitch, cam.fov, screen_width, screen_height);

		ImU32 cave_col = IM_COL32(
			(int)(globals::base_finder_cave_color[0] * 255),
			(int)(globals::base_finder_cave_color[1] * 255),
			(int)(globals::base_finder_cave_color[2] * 255),
			(int)(globals::base_finder_cave_color[3] * 255));
		ImU32 bypass_col = IM_COL32(
			(int)(globals::base_finder_bypass_color[0] * 255),
			(int)(globals::base_finder_bypass_color[1] * 255),
			(int)(globals::base_finder_bypass_color[2] * 255),
			(int)(globals::base_finder_bypass_color[3] * 255));

		auto draw_blocks = [&](const std::vector<base_finder_block>& list, ImU32 color)
		{
			// Cap the number of drawn boxes per frame: each box costs 12 edge
			// lines in ImGui, and thousands of them balloon the vertex buffer
			// upload on every frame (glitchy/hard on weak iGPUs).
			int drawn = 0;
			const int cap = 1500;
			for (const auto& b : list)
			{
				if (drawn >= cap) break;
				float half = 0.5f;
				float corners[8][3] = {
					{(float)b.x - half, (float)b.y - half, (float)b.z - half},
					{(float)b.x + half, (float)b.y - half, (float)b.z - half},
					{(float)b.x + half, (float)b.y - half, (float)b.z + half},
					{(float)b.x - half, (float)b.y - half, (float)b.z + half},
					{(float)b.x - half, (float)b.y + half, (float)b.z - half},
					{(float)b.x + half, (float)b.y + half, (float)b.z - half},
					{(float)b.x + half, (float)b.y + half, (float)b.z + half},
					{(float)b.x - half, (float)b.y + half, (float)b.z + half}
				};

				float screen_x[8], screen_y[8];
				bool visible[8] = {false, false, false, false, false, false, false, false};
				int valid_count = 0;
				for (int i = 0; i < 8; i++)
				{
					if (projection::world_to_screen(corners[i][0], corners[i][1], corners[i][2], screen_x[i], screen_y[i]))
					{
						visible[i] = true;
						valid_count++;
					}
				}
				if (valid_count < 4) continue;

				static const int edges[12][2] = {
					{0,1},{1,2},{2,3},{3,0},
					{4,5},{5,6},{6,7},{7,4},
					{0,4},{1,5},{2,6},{3,7}
				};
				for (int e = 0; e < 12; e++)
				{
					int a = edges[e][0], b = edges[e][1];
					if (!visible[a] || !visible[b]) continue;
					draw_list->AddLine(ImVec2(screen_x[a], screen_y[a]),
						ImVec2(screen_x[b], screen_y[b]), color, 1.5f);
				}
				drawn++;
			}
		};

		draw_blocks(caves, cave_col);
		draw_blocks(solid, bypass_col);
	}

	void flaway::modules::base_finder::shutdown()
	{
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_cave_blocks.clear();
			g_solid_blocks.clear();
			g_click_queue.clear();
			g_cave_scanner = {};
			g_bypass_scanner = {};
		}
		if (!g_cached) return;

		auto env = flaway::instance->get_env();
		if (!env) { g_cached = false; return; }

		auto release = [&](jclass& c)
		{
			if (c) { env->DeleteGlobalRef(c); c = nullptr; }
		};
		release(g_jni.world_class);
		release(g_jni.light_type_class);
		release(g_jni.block_state_class);
		release(g_jni.block_class);
		release(g_jni.block_pos_class);
		release(g_jni.vec3d_class);
		release(g_jni.direction_class);
		release(g_jni.bhr_class);
		release(g_jni.im_class);
		release(g_jni.hand_class);
		if (g_jni.light_type_block_obj) { env->DeleteGlobalRef(g_jni.light_type_block_obj); g_jni.light_type_block_obj = nullptr; }

		g_jni = {};
		g_cached = false;
	}
