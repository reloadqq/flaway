#include "base_finder.h"
#include "../../flaway.h"
#include "../../globals/globals.h"
#include "../../gui/GUI.h"
#include <sdk/minecraft/minecraft.h>
#include <sdk/minecraft/entity/entity.h>
#include <sdk/minecraft/world/world.h>
#include <sdk/classloader.h>
#include <sdk/mappings/mappings.hpp>
#include <sdk/projection.h>
#include "../../utils/logger.h"
#include "../../utils/chat_notify.h"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <cfloat>
#include <climits>
#include <algorithm>
#include <utility>
#include <vector>
#include <string>
#include <unordered_set>
#include <unordered_map>
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
	long long g_resolve_attempt_us = 0;

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
	long long g_frame_counter = 0;

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

	// Releases up to two JNI local refs on every exit path (early return and
	// exception alike) so a throwing run() cannot leak references on the
	// render thread.
	struct ref_guard
	{
		JNIEnv* env;
		jobject a;
		jobject b;
		ref_guard(JNIEnv* e, jobject x, jobject y) : env(e), a(x), b(y) {}
		~ref_guard()
		{
			if (!env) return;
			if (a) env->DeleteLocalRef(a);
			if (b) env->DeleteLocalRef(b);
		}
		ref_guard(const ref_guard&) = delete;
		ref_guard& operator=(const ref_guard&) = delete;
	};

	// ---- Target search (specific blocks + players) -------------------------
	// Recognised block types. Keep target_name()/target_enabled() in sync.
	enum target_type : int
	{
		T_CHEST = 0,      // chest / trapped chest / barrel
		T_SHULKER,        // any shulker box
		T_SPAWNER,        // monster spawner
		T_FRAME,          // end portal frame
		T_ENDPORTAL,      // end portal block
		T_OBSIDIAN,       // obsidian / crying obsidian
		T_PLAYER,         // live player (separate scan pass)
		T_TYPE_COUNT
	};

	const char* target_name(int t)
	{
		switch (t)
		{
			case T_CHEST:      return "Chest";
			case T_SHULKER:    return "Shulker";
			case T_SPAWNER:    return "Spawner";
			case T_FRAME:      return "EndPortalFrame";
			case T_ENDPORTAL:  return "EndPortal";
			case T_OBSIDIAN:   return "Obsidian";
			case T_PLAYER:     return "Player";
			default:           return "Unknown";
		}
	}

	bool target_enabled(int t)
	{
		switch (t)
		{
			case T_CHEST:     return globals::base_finder_target_chest;
			case T_SHULKER:   return globals::base_finder_target_shulker;
			case T_SPAWNER:   return globals::base_finder_target_spawner;
			case T_FRAME:     return globals::base_finder_target_frame;
			case T_ENDPORTAL: return globals::base_finder_target_endportal;
			case T_OBSIDIAN:  return globals::base_finder_target_obsidian;
			case T_PLAYER:    return globals::base_finder_target_players;
			default:          return false;
		}
	}

	// Any block (non-player) filter switched on — lets us skip the whole
	// block scan when the user only wants player tracers.
	bool any_block_target_enabled()
	{
		return globals::base_finder_target_chest || globals::base_finder_target_shulker ||
			globals::base_finder_target_spawner || globals::base_finder_target_frame ||
			globals::base_finder_target_endportal || globals::base_finder_target_obsidian;
	}

	// Chunk-major walker: one chunk column is scanned in one go so
	// isChunkLoaded() runs once per 16x16 column instead of once per block.
	struct target_scanner_state
	{
		bool active = false;
		bool finished = false;
		scan_area area;
		int min_cx = 0, max_cx = 0, min_cz = 0, max_cz = 0;
		// Chunk columns in the order they should be visited (nearest first).
		std::vector<std::pair<int, int>> chunks;
		int chunk_i = 0;
		int lx = 0, lz = 0, y = 0;
		double org_x = 0.0, org_z = 0.0;   // player position when this pass started
		long long finished_us = 0;
		std::unordered_map<uint64_t, int> found;
	};

	target_scanner_state g_target_scanner;
	long long g_last_partial_commit = 0;

	// Shared output (written by the scan, read by draw / notify).
	std::vector<base_finder_target> g_targets;
	std::vector<base_finder_player> g_players;
	// Positions already reported to chat/file this session, so a rescan of
	// the same area never spams the report a second time.
	std::unordered_set<uint64_t> g_reported;
	std::unordered_set<int> g_reported_players;
	// Chat lines waiting for the rate limiter (separate lock: flush_chat()
	// calls into JNI, which must never happen while g_mutex is held).
	std::mutex g_chat_mutex;
	std::vector<std::string> g_pending_chat;
	long long g_last_chat_us = 0;

	// Identity caches. Blocks/BlockStates are singletons, so most probes hit
	// these instead of paying for getBlock() + getTranslationKey() + a UTF-8
	// round trip on every single block of the volume. A small ring (instead of
	// one slot) keeps the handful of block types actually present in a chunk
	// cached at once — one slot is useless on mixed terrain.
	struct ident_cache
	{
		jobject obj[32];
		int kind[32];
		int n;
		int next;
	};
	ident_cache g_state_cache = {};
	ident_cache g_block_cache = {};

	// Returns true on hit and stores the cached kind in *kind_out.
	bool cache_lookup(JNIEnv* env, ident_cache& c, jobject o, int* kind_out)
	{
		if (!o) return false;
		for (int i = 0; i < c.n; i++)
		{
			if (c.obj[i] && env->IsSameObject(o, c.obj[i]))
			{
				*kind_out = c.kind[i];
				return true;
			}
		}
		return false;
	}

	void cache_store(JNIEnv* env, ident_cache& c, jobject o, int kind)
	{
		if (!o) return;
		jobject g = env->NewGlobalRef(o);
		if (!g) return;
		int i = c.next;
		if (c.obj[i]) env->DeleteGlobalRef(c.obj[i]);
		c.obj[i] = g;
		c.kind[i] = kind;
		c.next = (i + 1) % 32;
		if (c.n < 32) c.n++;
	}

	void reset_target_caches(JNIEnv* env)
	{
		if (!env) return;
		auto drop = [&](ident_cache& c)
		{
			for (int i = 0; i < c.n; i++)
			{
				if (c.obj[i]) { env->DeleteGlobalRef(c.obj[i]); c.obj[i] = nullptr; }
				c.kind[i] = -1;
			}
			c.n = 0;
			c.next = 0;
		};
		drop(g_state_cache);
		drop(g_block_cache);
	}

	// Translate "block.minecraft.xxx" -> one of target_type, or -1.
	int classify_block_key(JNIEnv* env, jobject block)
	{
		if (!block) return -1;
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
		if (!key) return -1;

		int kind = -1;
		const char* s = env->GetStringUTFChars((jstring)key, nullptr);
		if (s)
		{
			if (strstr(s, "shulker_box"))
				kind = T_SHULKER;
			else if (strcmp(s, "block.minecraft.chest") == 0 ||
				strcmp(s, "block.minecraft.trapped_chest") == 0 ||
				strcmp(s, "block.minecraft.barrel") == 0)
				kind = T_CHEST;
			else if (strcmp(s, "block.minecraft.spawner") == 0)
				kind = T_SPAWNER;
			else if (strcmp(s, "block.minecraft.end_portal_frame") == 0)
				kind = T_FRAME;
			else if (strcmp(s, "block.minecraft.end_portal") == 0)
				kind = T_ENDPORTAL;
			else if (strcmp(s, "block.minecraft.obsidian") == 0 ||
				strcmp(s, "block.minecraft.crying_obsidian") == 0)
				kind = T_OBSIDIAN;
			env->ReleaseStringUTFChars((jstring)key, s);
		}
		env->DeleteLocalRef(key);
		return kind;
	}

	// Classify a BlockState, using the singleton identity caches. Returns one
	// of target_type or -1 (filters are applied by the caller, not here, so
	// toggling a filter in the menu takes effect without a rescan).
	int classify_state(JNIEnv* env, jobject state)
	{
		if (!state || !g_jni.state_get_block) return -1;

		int cached;
		if (cache_lookup(env, g_state_cache, state, &cached))
			return cached;

		jobject block = env->CallObjectMethod(state, g_jni.state_get_block);
		if (env->ExceptionCheck()) { env->ExceptionClear(); block = nullptr; }
		if (!block) return -1;

		int kind;
		if (cache_lookup(env, g_block_cache, block, &kind))
		{
			// Block already classified: skip getTranslationKey() entirely.
		}
		else
		{
			kind = classify_block_key(env, block);
			cache_store(env, g_block_cache, block, kind);
		}
		env->DeleteLocalRef(block);

		cache_store(env, g_state_cache, state, kind);
		return kind;
	}

	// Defined further down (after the JNI resolver); forward-declared here so
	// the target scanner can build its volume before that point.
	void build_area(JNIEnv* env, jobject world, double px, double py, double pz, scan_area& out);

	void start_target_scanner(JNIEnv* env, jobject world, double px, double py, double pz)
	{
		target_scanner_state& ts = g_target_scanner;
		build_area(env, world, px, py, pz, ts.area);
		ts.min_cx = ts.area.minX >> 4;
		ts.max_cx = ts.area.maxX >> 4;
		ts.min_cz = ts.area.minZ >> 4;
		ts.max_cz = ts.area.maxZ >> 4;

		// Visit the chunk column the player is standing in first, then the
		// surrounding ones. The scan restarts whenever the player moves, so a
		// corner-first order would keep re-covering ground the player is
		// already far from and never reach anything worth reporting.
		int pcx = ((int)floor(px)) >> 4;
		int pcz = ((int)floor(pz)) >> 4;
		ts.chunks.clear();
		ts.chunks.reserve((size_t)(ts.max_cx - ts.min_cx + 1) * (size_t)(ts.max_cz - ts.min_cz + 1));
		for (int cx = ts.min_cx; cx <= ts.max_cx; cx++)
			for (int cz = ts.min_cz; cz <= ts.max_cz; cz++)
				ts.chunks.push_back({cx, cz});
		std::sort(ts.chunks.begin(), ts.chunks.end(),
			[pcx, pcz](const std::pair<int, int>& a, const std::pair<int, int>& b)
			{
				long long da = (long long)(a.first - pcx) * (a.first - pcx) +
					(long long)(a.second - pcz) * (a.second - pcz);
				long long db = (long long)(b.first - pcx) * (b.first - pcx) +
					(long long)(b.second - pcz) * (b.second - pcz);
				if (da != db) return da < db;
				if (a.first != b.first) return a.first < b.first;
				return a.second < b.second;
			});

		ts.chunk_i = 0;
		ts.lx = 0;
		ts.lz = 0;
		ts.y = ts.area.minY;
		ts.org_x = px;
		ts.org_z = pz;
		ts.finished = false;
		ts.active = true;
		// ts.found is intentionally kept: a restart (player moved) must not
		// throw away everything already discovered. process_target_chunk()
		// removes a position again as soon as it re-visits it and finds that
		// it is no longer a target, so the set cannot go permanently stale.
	}

	// Advance the chunk-major cursor. Returns true when the whole area is done.
	bool target_advance(target_scanner_state& ts)
	{
		// Inner loop runs over y so a vertical run of identical blocks keeps
		// hitting the state identity cache.
		ts.y++;
		if (ts.y > ts.area.maxY)
		{
			ts.y = ts.area.minY;
			ts.lz++;
			if (ts.lz > 15)
			{
				ts.lz = 0;
				ts.lx++;
				if (ts.lx > 15)
				{
					ts.lx = 0;
					ts.chunk_i++;
					if (ts.chunk_i >= (int)ts.chunks.size()) return true;
				}
			}
		}
		return false;
	}

	// Write one line into ~/.minecraft/flaway_basefinder.txt.
	void log_target_file(const base_finder_target& t, double px, double py, double pz)
	{
		if (!globals::base_finder_log_file) return;
		char buf[256];
		time_t now_t = time(nullptr);
		struct tm tmv;
		localtime_r(&now_t, &tmv);
		double dx = t.x + 0.5 - px;
		double dy = t.y + 0.5 - py;
		double dz = t.z + 0.5 - pz;
		double d = sqrt(dx * dx + dy * dy + dz * dz);
		snprintf(buf, sizeof(buf),
			"%04d-%02d-%02d %02d:%02d:%02d\t%s\t%d\t%d\t%d\td=%.1f",
			tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
			tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
			target_name(t.type), t.x, t.y, t.z, d);
		chat_notify::append_file("flaway_basefinder.txt", buf);
	}

	// Drain queued chat lines. Rate limited to one message every 2 seconds so
	// a freshly discovered base cannot flood the chat window.
	void flush_chat(bool force)
	{
		std::vector<std::string> lines;
		{
			std::lock_guard<std::mutex> lock(g_chat_mutex);
			if (g_pending_chat.empty()) return;
			long long now = now_us();
			if (!force && now - g_last_chat_us < 2000000) return;
			int n = (int)g_pending_chat.size();
			if (n > 3) n = 3;
			lines.assign(g_pending_chat.begin(), g_pending_chat.begin() + n);
			g_pending_chat.erase(g_pending_chat.begin(), g_pending_chat.begin() + n);
			g_last_chat_us = now;
		}
		// Never hold a lock while calling into JNI.
		if (globals::base_finder_log_chat)
			for (const std::string& l : lines) chat_notify::add(l);
	}

	void queue_chat(const std::string& line)
	{
		std::lock_guard<std::mutex> lock(g_chat_mutex);
		if (g_pending_chat.size() >= 32) return;
		g_pending_chat.push_back(line);
	}

	// Merge a finished scan into g_targets, report every position that has
	// never been reported before (chat summary + one file line each).
	void commit_target_scan(target_scanner_state& ts, double px, double py, double pz)
	{
		int counts[T_TYPE_COUNT] = { 0 };
		int new_total = 0;
		std::string coords;
		int shown = 0;
		std::vector<base_finder_target> merged;
		std::vector<base_finder_target> to_log;
		merged.reserve(ts.found.size());

		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_targets.clear();
			g_targets.reserve(ts.found.size());

			for (const auto& kv : ts.found)
			{
				int x, y, z;
				unpack_pos(kv.first, x, y, z);
				int type = kv.second;
				// Anything outside the current volume was found on a previous
				// pass; drop it once the player has moved away from it.
				if (x < ts.area.minX || x > ts.area.maxX ||
					y < ts.area.minY || y > ts.area.maxY ||
					z < ts.area.minZ || z > ts.area.maxZ)
					continue;
				if (type < 0 || type >= T_TYPE_COUNT || !target_enabled(type)) continue;

				base_finder_target t{ x, y, z, type };
				g_targets.push_back(t);

				if (g_reported.insert(kv.first).second)
				{
					counts[type]++;
					new_total++;
					to_log.push_back(t);
					if (shown < 3)
					{
						if (!coords.empty()) coords += ", ";
						coords += std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(z);
						shown++;
					}
				}
			}

			// Safety valve: the set only ever grows during a session.
			if (g_reported.size() > 400000) g_reported.clear();
		}

		for (const auto& t : to_log) log_target_file(t, px, py, pz);

		if (new_total > 0 && globals::base_finder_log_chat)
		{
			std::string msg = "[BaseFinder] +" + std::to_string(new_total);
			bool any = false;
			for (int i = 0; i < T_PLAYER; i++)
			{
				if (!counts[i]) continue;
				if (any) msg += ", ";
				msg += target_name(i);
				msg += " x" + std::to_string(counts[i]);
				any = true;
			}
			if (!coords.empty()) msg += " @ " + coords;
			queue_chat(msg);
		}
	}

	// ---- Player targets ---------------------------------------------------
	std::string player_nick(JNIEnv* env, jobject player)
	{
		std::string result;
		if (!player) return result;
		jclass pe = sdk::classloader::find_class(env, sdk::mappings::player_entity_class_sig);
		if (!pe) return result;
		jmethodID mid = env->GetMethodID(pe, sdk::mappings::player_get_game_profile_name,
			sdk::mappings::player_get_game_profile_sig);
		if (env->ExceptionCheck()) { env->ExceptionClear(); mid = nullptr; }
		env->DeleteLocalRef(pe);
		if (!mid) return result;

		jobject profile = env->CallObjectMethod(player, mid);
		if (env->ExceptionCheck()) { env->ExceptionClear(); profile = nullptr; }
		if (!profile) return result;

		jclass gc = env->GetObjectClass(profile);
		jmethodID gn = gc ? env->GetMethodID(gc, sdk::mappings::game_profile_get_name_name,
			sdk::mappings::game_profile_get_name_sig) : nullptr;
		if (env->ExceptionCheck()) { env->ExceptionClear(); gn = nullptr; }
		if (gn)
		{
			jstring name = (jstring)env->CallObjectMethod(profile, gn);
			if (env->ExceptionCheck()) { env->ExceptionClear(); name = nullptr; }
			if (name)
			{
				const char* utf = env->GetStringUTFChars(name, nullptr);
				if (utf) { result = utf; env->ReleaseStringUTFChars(name, utf); }
				env->DeleteLocalRef(name);
			}
		}
		if (gc) env->DeleteLocalRef(gc);
		env->DeleteLocalRef(profile);
		return result;
	}

	void collect_players(JNIEnv* env, jobject world, jobject local_player, double px, double py, double pz)
	{
		if (!globals::base_finder_target_players)
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_players.clear();
			return;
		}

		std::vector<base_finder_player> found;
		{
			sdk::world_client wc(world);
			std::vector<jobject> list = wc.get_players();
			jclass player_cls = sdk::classloader::find_class(env, sdk::mappings::player_entity_class_sig);
			for (jobject p : list)
			{
				if (!p) continue;
				if (player_cls && !env->IsInstanceOf(p, player_cls)) { env->DeleteLocalRef(p); continue; }
				if (local_player && env->IsSameObject(p, local_player)) { env->DeleteLocalRef(p); continue; }
				sdk::entity_client ec(p);
				int id = ec.get_entity_id();
				if (id <= 0) { env->DeleteLocalRef(p); continue; }
				double x = ec.get_x(), y = ec.get_y(), z = ec.get_z();
				double dx = x - px, dy = y - py, dz = z - pz;
				// Same reach the ESP uses: tracers past 64 blocks are noise.
				if (dx * dx + dy * dy + dz * dz > 4096.0) { env->DeleteLocalRef(p); continue; }
				std::string nick = player_nick(env, p);
				found.push_back({ id, x, y, z, nick.empty() ? ("#" + std::to_string(id)) : nick });
				env->DeleteLocalRef(p);
			}
			if (player_cls) env->DeleteLocalRef(player_cls);
		}

		std::vector<std::string> new_lines;
		std::vector<std::string> new_file;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_players = found;
			for (const auto& pl : found)
			{
				if (g_reported_players.insert(pl.id).second)
				{
					double dx = pl.x - px, dy = pl.y - py, dz = pl.z - pz;
					double d = sqrt(dx * dx + dy * dy + dz * dz);
					char buf[256];
					snprintf(buf, sizeof(buf), "[BaseFinder] Player %s @ %.0f %.0f %.0f (d=%.0f)",
						pl.name.c_str(), pl.x, pl.y, pl.z, d);
					new_lines.push_back(buf);

					if (globals::base_finder_log_file)
					{
						char fbuf[256];
						snprintf(fbuf, sizeof(fbuf), "Player\t%s\t%.0f\t%.0f\t%.0f\td=%.0f",
							pl.name.c_str(), pl.x, pl.y, pl.z, d);
						new_file.push_back(fbuf);
					}
				}
			}
			if (g_reported_players.size() > 65536) g_reported_players.clear();
		}
		// Disk and chat only after g_mutex is released: the draw thread waits
		// on the same lock every frame.
		for (const auto& f : new_file)
			chat_notify::append_file("flaway_basefinder.txt", f);
		for (const auto& l : new_lines) queue_chat(l);
	}

	// ---- JNI resolution -----------------------------------------------------------
	bool resolve_jni(JNIEnv* env)
	{
		if (g_cached && g_jni.world_class && g_jni.get_block_state) return true;

		// A class/method lookup can legitimately fail for a moment (e.g. the
		// first frame after the world loads). Latching that failure would
		// silently disable the module for the rest of the session, so retry —
		// but at most once a second to keep the retry loop free.
		long long attempt = now_us();
		if (g_resolve_attempt_us && attempt - g_resolve_attempt_us < 1000000)
			return g_cached && g_jni.world_class && g_jni.get_block_state;
		g_resolve_attempt_us = attempt;

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
			if (!g_jni.block_get_translation_key)
			{
				g_jni.block_get_translation_key = env->GetMethodID(g_jni.block_class,
					sdk::mappings::block_get_translation_key_name_legacy,
					sdk::mappings::block_get_translation_key_sig);
				if (env->ExceptionCheck()) env->ExceptionClear();
			}
			g_jni.block_translation_key = env->GetFieldID(g_jni.block_class,
				sdk::mappings::block_translation_key_field, sdk::mappings::block_translation_key_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			if (!g_jni.block_translation_key)
			{
				g_jni.block_translation_key = env->GetFieldID(g_jni.block_class,
					sdk::mappings::block_translation_key_field_legacy,
					sdk::mappings::block_translation_key_sig);
				if (env->ExceptionCheck()) env->ExceptionClear();
			}
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

		bool resolved = g_jni.world_class != nullptr && g_jni.get_block_state != nullptr;
		g_cached = resolved;
		return resolved;
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
			size_t cave_count = 0;
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_cave_blocks = std::move(out);
				// Capture under the lock: run() clears these containers under
				// the same mutex, so a bare .size() outside would race.
				cave_count = g_cave_blocks.size();
			}
			cs.active = false;
			cs.finished_us = now_us();
			if (globals::debug_logging_enabled)
				logger::log("[base_finder] cave scan done: " + std::to_string(cave_count) + " blocks");
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
			size_t solid_count = 0;
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_solid_blocks = out;
				if (globals::base_finder_click) g_click_queue = out;
				else g_click_queue.clear();
				solid_count = g_solid_blocks.size();
			}
			bs.active = false;
			bs.finished_us = now_us();
			if (globals::debug_logging_enabled)
				logger::log("[base_finder] bypass scan done: " + std::to_string(solid_count) + " blocks");
		}
	}

	// ---- Target scanning pass (chunk-major, filtered by target_enabled) ----
	void process_target_chunk(JNIEnv* env, jobject world, double px, double py, double pz)
	{
		target_scanner_state& ts = g_target_scanner;

		if (!any_block_target_enabled())
		{
			ts.active = false;
			std::lock_guard<std::mutex> lock(g_mutex);
			g_targets.clear();
			return;
		}
		if (!ts.active) return;

		// Throughput: the old 6000-block cap bound the scan to ~36k blocks/s
		// (a full 128-range volume took ~12 minutes). The wall-clock budget is
		// the real limit now, so fast blocks cost nothing extra while heavy
		// JNI runs still get cut off at 5 ms per pass.
		const int block_limit = 100000;
		int processed = 0;
		long long frame_start = now_us();

		// isChunkLoaded is per chunk column, so cache it for the column the
		// cursor is currently inside (16x16xH blocks -> one JNI call).
		int cached_cx = INT_MAX, cached_cz = INT_MAX;
		bool cached_loaded = false;

		while (processed < block_limit && !ts.finished && now_us() - frame_start < 4000)
		{
			if (ts.chunk_i >= (int)ts.chunks.size()) { ts.finished = true; break; }
			const std::pair<int, int>& col = ts.chunks[ts.chunk_i];
			int x = (col.first << 4) + ts.lx;
			int z = (col.second << 4) + ts.lz;
			int y = ts.y;
			uint64_t key = pack_pos(x, y, z);

			if (y >= ts.area.minY && y <= ts.area.maxY && ts.area.contains(x, y, z))
			{
				if (col.first != cached_cx || col.second != cached_cz)
				{
					cached_cx = col.first;
					cached_cz = col.second;
					cached_loaded = chunk_loaded(env, world, col.first << 4, col.second << 4);
				}
				if (cached_loaded)
				{
					jobject st = get_block_state_at(env, world, x, y, z);
					if (st)
					{
						int kind = classify_state(env, st);
						env->DeleteLocalRef(st);
						if (kind >= 0 && kind < T_PLAYER && target_enabled(kind))
							ts.found[key] = kind;
						else
							// Also drop stale hits (chest mined out, filter
							// switched off) so the display stays truthful.
							ts.found.erase(key);
					}
				}
			}

			processed++;
			if (target_advance(ts)) ts.finished = true;
		}

		if (ts.finished)
		{
			commit_target_scan(ts, px, py, pz);
			ts.active = false;
			ts.finished_us = now_us();
			g_last_partial_commit = ts.finished_us;
		}
		else
		{
			// The volume can take minutes to walk. Publishing what has been
			// found so far every couple of seconds means the user gets the
			// report even if the player keeps moving and restarting the scan.
			long long n = now_us();
			if (n - g_last_partial_commit >= 2000000)
			{
				g_last_partial_commit = n;
				commit_target_scan(ts, px, py, pz);
			}
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
			// interactBlock returns an ActionResult - delete it, otherwise the
			// local-ref table fills up on this never-detached thread.
			jobject interact_res = env->CallObjectMethod(im, g_jni.interact_block, player, hand, bhr);
			if (env->ExceptionCheck()) env->ExceptionClear();
			if (interact_res) env->DeleteLocalRef(interact_res);
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
			if (!flaway::instance || !sdk::instance) return;
			if (!globals::base_finder_enabled)
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_cave_blocks.clear();
				g_solid_blocks.clear();
				g_click_queue.clear();
				g_targets.clear();
				g_players.clear();
				g_cave_scanner = {};
				g_bypass_scanner = {};
				g_target_scanner = {};
				g_force_restart = false;
				return;
			}

			auto env = flaway::instance->get_env();
			if (!env) return;
			if (!resolve_jni(env)) return;

			jobject world = sdk::instance->get_world();
			if (!world) return;
			// Releases both local refs on every path, including the catch below.
			ref_guard guard(env, world, nullptr);
			jobject local_player = sdk::instance->get_player();
			if (!local_player) return;
			guard.b = local_player;

			sdk::entity_client local_entity(local_player);
			double px = local_entity.get_x();
			double py = local_entity.get_y();
			double pz = local_entity.get_z();

			bool cave_mode = globals::base_finder_mode == 0 || globals::base_finder_mode == 2;
			bool bypass_mode = globals::base_finder_mode == 1 || globals::base_finder_mode == 2;
			bool targets_on = globals::base_finder_targets;
			bool blocks_on = targets_on && any_block_target_enabled();

			g_frame_counter++;
			if (g_frame_counter % 10 != 0) return;

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

			if (blocks_on)
			{
				target_scanner_state& ts = g_target_scanner;
				// Restart ONLY when the volume being scanned no longer
				// contains the player. The old code also restarted on every
				// g_force_restart (any step > 8 blocks), which reset the
				// cursor to the nearest chunk over and over — the scan never
				// got past the chunk under the player's feet, so it reported
				// nothing while the player was walking around.
				if (ts.active)
				{
					double r = globals::base_finder_range;
					double dx = px - ts.org_x, dz = pz - ts.org_z;
					if (dx * dx + dz * dz > r * r)
					{
						commit_target_scan(ts, px, py, pz);
						ts.active = false;
						ts.finished_us = now;
					}
				}
				if (!ts.active && now - ts.finished_us >= 500000)
					start_target_scanner(env, world, px, py, pz);
				if (ts.active)
					process_target_chunk(env, world, px, py, pz);
			}
			else
			{
				// Keep g_reported: toggling the feature off/on must not
				// re-report everything the user already knows about.
				std::lock_guard<std::mutex> lock(g_mutex);
				g_targets.clear();
				g_target_scanner.active = false;
			}

			if (targets_on)
				collect_players(env, world, local_player, px, py, pz);
			else
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_players.clear();
			}

			process_clicks(env, world, local_player);
			flush_chat(false);
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
		std::vector<base_finder_target> targets;
		std::vector<base_finder_player> players;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			caves = g_cave_blocks;
			solid = g_solid_blocks;
			targets = g_targets;
			players = g_players;
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
		// A saved config with alpha 0 would make every marker and tracer
		// silently invisible — clamp it to something that can always be seen.
		float ta = globals::base_finder_target_color[3];
		if (!(ta > 0.05f)) ta = 1.0f;
		ImU32 target_col = IM_COL32(
			(int)(globals::base_finder_target_color[0] * 255),
			(int)(globals::base_finder_target_color[1] * 255),
			(int)(globals::base_finder_target_color[2] * 255),
			(int)(ta * 255));
		// Players stand out from the block targets so a person is never
		// mistaken for a chest when both tracers are on screen.
		const ImU32 player_col = IM_COL32(255, 92, 92, (int)(ta * 255));
		const ImVec2 tracer_origin((float)screen_width * 0.5f, (float)screen_height);

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

		// Markers for the recognised targets (chest / shulker / spawner /
		// portal / obsidian). A circle is one draw instead of 12 edge lines,
		// so hundreds of targets stay cheap.
		int marked = 0;
		const int marker_cap = 400;
		for (const auto& t : targets)
		{
			if (marked >= marker_cap) break;
			float sx, sy;
			if (!projection::world_to_screen((float)t.x + 0.5f, (float)t.y + 0.5f,
				(float)t.z + 0.5f, sx, sy))
				continue;
			draw_list->AddCircle(ImVec2(sx, sy), 7.0f, target_col, 14, 1.6f);
			draw_list->AddCircleFilled(ImVec2(sx, sy), 2.0f, target_col, 8);
			marked++;
		}
		for (const auto& p : players)
		{
			float sx, sy;
			if (!projection::world_to_screen((float)p.x, (float)p.y + 1.0f, (float)p.z, sx, sy))
				continue;
			draw_list->AddCircle(ImVec2(sx, sy), 9.0f, player_col, 14, 1.8f);
		}

		// Tracers from the bottom-center of the screen to every target.
		if (globals::base_finder_tracers)
		{
			auto tracer_to = [&](double wx, double wy, double wz, ImU32 col, float width)
			{
				float sx, sy;
				if (!projection::world_to_screen((float)wx, (float)wy, (float)wz, sx, sy))
					return;
				draw_list->AddLine(tracer_origin, ImVec2(sx, sy), col, width);
				draw_list->AddCircleFilled(ImVec2(sx, sy), 3.0f, col, 8);
			};
			for (const auto& t : targets)
				tracer_to(t.x + 0.5, t.y + 0.5, t.z + 0.5, target_col, 1.6f);
			for (const auto& p : players)
				tracer_to(p.x, p.y + 1.0, p.z, player_col, 2.0f);
		}
	}

	void flaway::modules::base_finder::shutdown()
	{
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_cave_blocks.clear();
			g_solid_blocks.clear();
			g_click_queue.clear();
			g_targets.clear();
			g_players.clear();
			g_cave_scanner = {};
			g_bypass_scanner = {};
			g_target_scanner = {};
			g_last_partial_commit = 0;
		}
		{
			// Never hand queued chat lines to a half-torn-down client.
			std::lock_guard<std::mutex> lock(g_chat_mutex);
			g_pending_chat.clear();
		}

		// No `g_cached` early-out here: a partially resolved state can still
		// hold global refs (identity caches, some classes), and every release
		// below is a no-op on null anyway.
		if (!flaway::instance) { g_cached = false; return; }

		auto env = flaway::instance->get_env();
		if (!env) { g_cached = false; return; }
		reset_target_caches(env);

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
		g_resolve_attempt_us = 0;
	}
