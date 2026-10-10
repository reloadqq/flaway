#include "storage_esp.h"
#include "../../flaway.h"
#include "../../globals/globals.h"
#include "../../gui/GUI.h"
#include <sdk/minecraft/minecraft.h>
#include <sdk/minecraft/world/world.h>
#include <sdk/minecraft/entity/entity.h>
#include <sdk/classloader.h>
#include <sdk/mappings/mappings.hpp>
#include <sdk/projection.h>
#include "../../utils/logger.h"
#include "../../utils/rlog.h"
#define _USE_MATH_DEFINES
#include <cmath>
#include <cfloat>
#include <vector>
#include <mutex>
#include <chrono>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static std::vector<storage_block_data> storage_blocks;
static storage_esp_camera_data storage_camera;
static std::mutex storage_blocks_mutex;
static double g_last_scan_x = 0.0, g_last_scan_y = 0.0, g_last_scan_z = 0.0;
static long long g_last_scan_frame = 0;
static long long g_scan_frame_counter = 0;
static int64_t g_last_scan_us = 0;
static bool g_cached = false;
static int64_t g_resolve_attempt_us = 0;
static bool g_resolve_logged = false;
// Set whenever the block list was dropped without a successful scan behind it
// (module/screen toggled off). Without it the periodic rescan timer below can
// decide "nothing changed" and keep an empty list on screen.
static bool g_needs_rescan = false;

// ---- Cached JNI resources (resolved once, reused every frame) --------------
// These replace the per-frame FindClass/GetMethodID churn that was the main
// FPS killer in the old implementation.
// CRITICAL: find_class()/FindClass() return LOCAL refs. Every class stored in
// these globals MUST be promoted with NewGlobalRef() (and the local released),
// otherwise the refs dangle once the render-thread native call returns to Java
// and IsInstanceOf()/GetFieldID() on them SIGSEGVs (observed in hs_err dumps
// at jni_IsInstanceOf/oopDesc::metadata_field on the Render thread).
static jclass g_client_world_class = nullptr;   // class_638 (ClientWorld) [global]
static jfieldID g_block_entities_field = nullptr; // field_60919 (Set<BlockEntity>), 1.21.10
static jmethodID g_world_get_chunk_mid = nullptr; // World.getChunk(II) -> WorldChunk (1.21.4)
static jclass g_world_chunk_class = nullptr;      // class_2818 (WorldChunk) [global]
static jmethodID g_wc_block_entities_mid = nullptr; // WorldChunk.getBlockEntities() -> Map
static jclass g_java_map_class = nullptr;         // java/util/Map [global]
static jmethodID g_map_values_mid = nullptr;      // Map.values() -> Collection
static jclass g_java_collection_class = nullptr;  // java/util/Collection [global]
static jmethodID g_collection_iterator_mid = nullptr; // Collection.iterator()
static jclass g_block_entity_class = nullptr;   // class_2586 (BlockEntity) [global]
static jmethodID g_be_get_pos_mid = nullptr;    // method_11016
static jclass g_block_pos_class = nullptr;      // class_2338 (BlockPos) [global]
static jmethodID g_bp_get_x_mid = nullptr;      // method_10263
static jmethodID g_bp_get_y_mid = nullptr;      // method_10264
static jmethodID g_bp_get_z_mid = nullptr;      // method_10260
static jclass g_ender_be_class = nullptr;       // class_2611 (EnderChestBlockEntity) [global]
static jclass g_shulker_be_class = nullptr;     // class_2627 (ShulkerBoxBlockEntity) [global]
static jclass g_container_be_class = nullptr;   // class_2624 (LockableContainerBlockEntity: chests/barrels/hoppers/...) [global]
static jclass g_java_set_class = nullptr;       // java/util/Set [global]
static jmethodID g_set_iterator_mid = nullptr;
static jclass g_java_iterator_class = nullptr;  // java/util/Iterator [global]
static jmethodID g_iter_has_next_mid = nullptr;
static jmethodID g_iter_next_mid = nullptr;

// Releases up to two JNI local refs on every exit path of run(), including the
// catch-all: a throwing run() must not leak references on the render thread.
struct storage_ref_guard
{
	JNIEnv* env;
	jobject a;
	jobject b;
	storage_ref_guard(JNIEnv* e, jobject x, jobject y) : env(e), a(x), b(y) {}
	~storage_ref_guard()
	{
		if (!env) return;
		if (a) env->DeleteLocalRef(a);
		if (b) env->DeleteLocalRef(b);
	}
	storage_ref_guard(const storage_ref_guard&) = delete;
	storage_ref_guard& operator=(const storage_ref_guard&) = delete;
};

static int64_t now_us()
{
	return std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

// True once the scan path we actually use is fully resolved: either the
// 1.21.10 world-level set, or the 1.21.4 per-chunk walk (chunk getter + map).
static bool jni_ready()
{
	if (!g_cached) return false;
	if (g_block_entities_field) return true;
	return g_world_get_chunk_mid && g_wc_block_entities_mid &&
		g_map_values_mid && g_collection_iterator_mid;
}

static bool resolve_jni(JNIEnv* env)
{
	if (jni_ready()) return true;

	// A class lookup can fail transiently (first frames after the world
	// loads). Latching that failure would leave StorageESP dead for the whole
	// session, so retry — but at most once a second.
	int64_t attempt = now_us();
	if (g_resolve_attempt_us && attempt - g_resolve_attempt_us < 1000000)
		return jni_ready();
	g_resolve_attempt_us = attempt;

	jclass local = nullptr;

	// Promote a freshly resolved local class ref to a cached global ref.
	auto promote = [&](jclass& slot)
	{
		if (local && slot == nullptr)
			slot = reinterpret_cast<jclass>(env->NewGlobalRef(local));
		if (local) env->DeleteLocalRef(local);
		local = nullptr;
		return slot != nullptr;
	};

	local = sdk::classloader::find_class(env, sdk::mappings::client_world_class_sig);
	promote(g_client_world_class);
	if (g_client_world_class)
	{
		g_block_entities_field = env->GetFieldID(g_client_world_class,
			sdk::mappings::client_world_block_entities_name,
			sdk::mappings::client_world_block_entities_sig);
		if (env->ExceptionCheck()) env->ExceptionClear();

		// 1.21.4 has no world-level block entity set; fall back to walking the
		// loaded chunks and reading each WorldChunk's own map.
		g_world_get_chunk_mid = env->GetMethodID(g_client_world_class,
			sdk::mappings::world_get_chunk_ii_name, sdk::mappings::world_get_chunk_ii_sig);
		if (env->ExceptionCheck()) env->ExceptionClear();
	}

	if (!g_block_entities_field)
	{
		local = sdk::classloader::find_class(env, sdk::mappings::world_chunk_class_sig);
		promote(g_world_chunk_class);
		if (g_world_chunk_class)
		{
			g_wc_block_entities_mid = env->GetMethodID(g_world_chunk_class,
				sdk::mappings::world_chunk_block_entities_name,
				sdk::mappings::world_chunk_block_entities_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
		}
		local = sdk::classloader::find_class(env, "java/util/Map");
		promote(g_java_map_class);
		if (g_java_map_class)
		{
			g_map_values_mid = env->GetMethodID(g_java_map_class, "values",
				"()Ljava/util/Collection;");
			if (env->ExceptionCheck()) env->ExceptionClear();
		}
		local = sdk::classloader::find_class(env, "java/util/Collection");
		promote(g_java_collection_class);
		if (g_java_collection_class)
		{
			g_collection_iterator_mid = env->GetMethodID(g_java_collection_class, "iterator",
				"()Ljava/util/Iterator;");
			if (env->ExceptionCheck()) env->ExceptionClear();
		}
	}

	local = sdk::classloader::find_class(env, sdk::mappings::block_entity_class_sig);
	promote(g_block_entity_class);
	if (g_block_entity_class)
	{
		g_be_get_pos_mid = env->GetMethodID(g_block_entity_class,
			sdk::mappings::block_entity_get_pos_name,
			sdk::mappings::block_entity_get_pos_sig);
		if (env->ExceptionCheck()) env->ExceptionClear();
	}

	local = sdk::classloader::find_class(env, sdk::mappings::block_pos_class_sig);
	promote(g_block_pos_class);
	if (g_block_pos_class)
	{
		g_bp_get_x_mid = env->GetMethodID(g_block_pos_class, sdk::mappings::block_pos_get_x_name, sdk::mappings::block_pos_get_x_sig);
		if (env->ExceptionCheck()) env->ExceptionClear();
		g_bp_get_y_mid = env->GetMethodID(g_block_pos_class, sdk::mappings::block_pos_get_y_name, sdk::mappings::block_pos_get_y_sig);
		if (env->ExceptionCheck()) env->ExceptionClear();
		g_bp_get_z_mid = env->GetMethodID(g_block_pos_class, sdk::mappings::block_pos_get_z_name, sdk::mappings::block_pos_get_z_sig);
		if (env->ExceptionCheck()) env->ExceptionClear();
	}

	local = sdk::classloader::find_class(env, sdk::mappings::ender_chest_block_entity_class_sig);
	promote(g_ender_be_class);
	local = sdk::classloader::find_class(env, sdk::mappings::shulker_box_block_entity_class_sig);
	promote(g_shulker_be_class);
	local = sdk::classloader::find_class(env, sdk::mappings::container_block_entity_class_sig);
	promote(g_container_be_class);

	local = sdk::classloader::find_class(env, "java/util/Set");
	promote(g_java_set_class);
	if (g_java_set_class)
	{
		g_set_iterator_mid = env->GetMethodID(g_java_set_class, "iterator", "()Ljava/util/Iterator;");
		if (env->ExceptionCheck()) env->ExceptionClear();
	}
	local = sdk::classloader::find_class(env, "java/util/Iterator");
	promote(g_java_iterator_class);
	if (g_java_iterator_class)
	{
		g_iter_has_next_mid = env->GetMethodID(g_java_iterator_class, "hasNext", "()Z");
		if (env->ExceptionCheck()) env->ExceptionClear();
		g_iter_next_mid = env->GetMethodID(g_java_iterator_class, "next", "()Ljava/lang/Object;");
		if (env->ExceptionCheck()) env->ExceptionClear();
	}

	bool resolved = g_block_entities_field != nullptr ||
		(g_world_get_chunk_mid && g_wc_block_entities_mid &&
			g_map_values_mid && g_collection_iterator_mid);
	g_cached = resolved;
	if (resolved && !g_resolve_logged)
	{
		g_resolve_logged = true;
		rlog::logf("storage_esp: resolved mode=%s",
			g_block_entities_field ? "set-field" : "chunk-walk");
	}
	return resolved;
}

// Classify a single block entity and, when it is one of the storages we draw
// and lies inside the radius, append it to the scan result.
static void consider_be(JNIEnv* env, jobject be, double player_x, double player_z,
	double radius, std::vector<storage_block_data>& out)
{
	if (!be) return;

	int type = -1;
	if (g_ender_be_class && env->IsInstanceOf(be, g_ender_be_class))
	{
		if (globals::storage_esp_ender_chest) type = 1;
	}
	else if (g_shulker_be_class && env->IsInstanceOf(be, g_shulker_be_class))
	{
		if (globals::storage_esp_shulker) type = 2;
	}
	else if (g_container_be_class && env->IsInstanceOf(be, g_container_be_class))
	{
		if (globals::storage_esp_chest) type = 0;
	}
	if (type == -1) return;

	jobject pos = env->CallObjectMethod(be, g_be_get_pos_mid);
	if (env->ExceptionCheck()) env->ExceptionClear();
	if (!pos) return;

	int bx = env->CallIntMethod(pos, g_bp_get_x_mid);
	if (env->ExceptionCheck()) env->ExceptionClear();
	int by = env->CallIntMethod(pos, g_bp_get_y_mid);
	if (env->ExceptionCheck()) env->ExceptionClear();
	int bz = env->CallIntMethod(pos, g_bp_get_z_mid);
	if (env->ExceptionCheck()) env->ExceptionClear();

	double dx = (bx + 0.5) - player_x;
	double dz = (bz + 0.5) - player_z;
	if (dx * dx + dz * dz <= radius * radius)
	{
		storage_block_data data;
		data.x = bx + 0.5;
		data.y = by + 0.5;
		data.z = bz + 0.5;
		data.type = type;
		out.push_back(data);
	}
	env->DeleteLocalRef(pos);
}

// Walk every element of a java/util/Collection through its iterator.
static bool collect_from(JNIEnv* env, jobject collection, double player_x,
	double player_z, double radius, std::vector<storage_block_data>& out)
{
	if (!collection || !g_collection_iterator_mid || !g_iter_has_next_mid || !g_iter_next_mid)
		return false;

	jobject iter = env->CallObjectMethod(collection, g_collection_iterator_mid);
	if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
	if (!iter) return false;

	while (env->CallBooleanMethod(iter, g_iter_has_next_mid))
	{
		if (env->ExceptionCheck()) { env->ExceptionClear(); break; }

		jobject be = env->CallObjectMethod(iter, g_iter_next_mid);
		if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
		if (be)
		{
			consider_be(env, be, player_x, player_z, radius, out);
			env->DeleteLocalRef(be);
		}
	}
	env->DeleteLocalRef(iter);
	return true;
}

// A single scan pass over the client's loaded block entities. Returns true on
// success; false if the JNI state is not usable this frame (safe to retry).
static bool scan_storage(JNIEnv* env, jobject world, double player_x, double player_y, double player_z,
	std::vector<storage_block_data>& out)
{
	if (!resolve_jni(env)) return false;
	if (!g_be_get_pos_mid || !g_bp_get_x_mid || !g_bp_get_y_mid || !g_bp_get_z_mid)
		return false;

	out.clear();
	// 32-block horizontal radius filter on the player, so far-away loaded
	// chests don't light the whole map up.
	const double radius = 32.0;

	// 1.21.10: one world-wide set of block entities.
	if (g_block_entities_field)
	{
		if (!g_set_iterator_mid || !g_iter_has_next_mid || !g_iter_next_mid)
			return false;

		jobject set = env->GetObjectField(world, g_block_entities_field);
		if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
		if (!set) return false;

		bool ok = collect_from(env, set, player_x, player_z, radius, out);
		env->DeleteLocalRef(set);
		return ok;
	}

	// 1.21.4: walk the loaded chunks around the player and read each chunk's map.
	if (!g_world_get_chunk_mid || !g_wc_block_entities_mid || !g_map_values_mid ||
		!g_collection_iterator_mid || !g_iter_has_next_mid || !g_iter_next_mid)
		return false;

	int pcx = static_cast<int>(std::floor(player_x / 16.0));
	int pcz = static_cast<int>(std::floor(player_z / 16.0));
	int chunk_radius = static_cast<int>(std::ceil(radius / 16.0));

	for (int cz = pcz - chunk_radius; cz <= pcz + chunk_radius; ++cz)
	{
		for (int cx = pcx - chunk_radius; cx <= pcx + chunk_radius; ++cx)
		{
			jobject chunk = env->CallObjectMethod(world, g_world_get_chunk_mid, cx, cz);
			if (env->ExceptionCheck()) { env->ExceptionClear(); continue; }
			if (!chunk) continue;

			jobject map = env->CallObjectMethod(chunk, g_wc_block_entities_mid);
			if (env->ExceptionCheck()) { env->ExceptionClear(); map = nullptr; }

			jobject values = nullptr;
			if (map)
			{
				values = env->CallObjectMethod(map, g_map_values_mid);
				if (env->ExceptionCheck()) { env->ExceptionClear(); values = nullptr; }
			}

			if (values)
			{
				collect_from(env, values, player_x, player_z, radius, out);
				env->DeleteLocalRef(values);
			}
			if (map) env->DeleteLocalRef(map);
			env->DeleteLocalRef(chunk);
		}
	}
	return true;
}

void flaway::modules::storage_esp::run()
{
	try
	{
		if (!flaway::instance || !sdk::instance) return;

		if (!globals::storage_esp_enabled)
		{
			std::lock_guard<std::mutex> lock(storage_blocks_mutex);
			storage_blocks.clear();
			g_needs_rescan = true;
			return;
		}

		if (!globals::storage_esp_chest && !globals::storage_esp_ender_chest && !globals::storage_esp_shulker)
		{
			std::lock_guard<std::mutex> lock(storage_blocks_mutex);
			storage_blocks.clear();
			g_needs_rescan = true;
			return;
		}

		auto env = flaway::instance->get_env();
		if (!env) return;

		// No scan while a screen is open (inventory, Escape, ...). The block
		// list is deliberately NOT dropped here: draw_boxes() already hides
		// itself, and clearing made every box vanish the moment a chest UI
		// opened and then pop back after a rescan. g_needs_rescan makes the
		// first frame after the screen closes refresh the list instead.
		if (sdk::instance->is_screen_open())
		{
			g_needs_rescan = true;
			return;
		}

		jobject world = sdk::instance->get_world();
		if (!world) return;
		// Releases both refs on every exit path, including the catch below.
		storage_ref_guard guard(env, world, nullptr);

		jobject local_player = sdk::instance->get_player();
		if (!local_player) return;
		guard.b = local_player;

		// Get camera data
		sdk::camera_data cam = sdk::instance->get_camera();
		if (!cam.valid)
		{
			sdk::entity_client local_entity_client(local_player);
			cam.x = local_entity_client.get_x();
			cam.y = local_entity_client.get_y() + 1.62;
			cam.z = local_entity_client.get_z();
			cam.yaw = local_entity_client.get_yaw();
			cam.pitch = local_entity_client.get_pitch();
			cam.fov = 70.0f;
		}

		storage_esp_camera_data camera;
		camera.cam_x = cam.x;
		camera.cam_y = cam.y;
		camera.cam_z = cam.z;
		camera.yaw = cam.yaw;
		camera.pitch = cam.pitch;
		camera.fov = cam.fov;

		// Get player position
		sdk::entity_client local_entity(local_player);
		double player_x = local_entity.get_x();
		double player_y = local_entity.get_y();
		double player_z = local_entity.get_z();

		// Cache: only re-scan when the player moved enough or periodically.
		// The block-entity set is tiny (dozens, not 173k volume samples), so a
		// scan is a few hundred cheap JNI calls instead of a 100ms+ stutter.
		g_scan_frame_counter++;
		double move_dist = std::sqrt(
			(player_x - g_last_scan_x) * (player_x - g_last_scan_x) +
			(player_y - g_last_scan_y) * (player_y - g_last_scan_y) +
			(player_z - g_last_scan_z) * (player_z - g_last_scan_z));
		bool need_rescan = g_needs_rescan ||
			(move_dist > 2.0) || (g_scan_frame_counter - g_last_scan_frame > 120);

		size_t block_count = 0;
		if (need_rescan)
		{
			std::vector<storage_block_data> blocks_data;
			auto t0 = std::chrono::high_resolution_clock::now();
			bool ok = scan_storage(env, world, player_x, player_y, player_z, blocks_data);
			auto t1 = std::chrono::high_resolution_clock::now();
			g_last_scan_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

			if (ok)
			{
				std::lock_guard<std::mutex> lock(storage_blocks_mutex);
				storage_blocks = std::move(blocks_data);
				g_last_scan_x = player_x;
				g_last_scan_y = player_y;
				g_last_scan_z = player_z;
				g_last_scan_frame = g_scan_frame_counter;
				g_needs_rescan = false;
			}
			// On failure the old list is kept (better stale boxes than none)
			// and g_needs_rescan stays set, so it retries next frame.
		}

		{
			// Refreshed every frame regardless of scan success: a stale camera
			// shifts the boxes as soon as the player turns.
			std::lock_guard<std::mutex> lock(storage_blocks_mutex);
			block_count = storage_blocks.size();
			storage_camera = camera;
		}

		if (globals::debug_logging_enabled)
			logger::log("[storage_esp] scan " + std::to_string(g_last_scan_us / 1000) + " ms, " +
				std::to_string(block_count) + " blocks");
	}
	catch (...)
	{
	}
}

void flaway::modules::storage_esp::draw_boxes()
{
	if (!globals::storage_esp_enabled) return;
	if (!GUI::get_is_init()) return;
	if (sdk::instance && sdk::instance->is_screen_open()) return;

	ImGuiIO& io = ImGui::GetIO();
	int screen_width = (int)io.DisplaySize.x;
	int screen_height = (int)io.DisplaySize.y;
	if (screen_width <= 0 || screen_height <= 0) return;

	ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
	if (!draw_list) return;

	std::vector<storage_block_data> blocks_copy;
	storage_esp_camera_data camera;
	{
		std::lock_guard<std::mutex> lock(storage_blocks_mutex);
		if (storage_blocks.empty()) return;
		blocks_copy = storage_blocks;
		camera = storage_camera;
	}

	// Use the shared projection so storage boxes line up exactly with the
	// player/backtrack ESP overlays (the old storage matrix was mirrored).
	projection::set_view((float)camera.cam_x, (float)camera.cam_y, (float)camera.cam_z,
		camera.yaw, camera.pitch, camera.fov, screen_width, screen_height);

	// Colors for different container types
	const ImU32 chest_color = IM_COL32(200, 150, 50, 255);       // Orange/brown for chests
	const ImU32 ender_chest_color = IM_COL32(128, 50, 200, 255); // Purple for ender chests
	const ImU32 shulker_color = IM_COL32(200, 100, 200, 255);    // Pink for shulkers
	const ImU32 black_color = IM_COL32(0, 0, 0, 255);

	for (const auto& block : blocks_copy)
	{
		float half = 0.5f;
		float corners[8][3] = {
			{(float)block.x - half, (float)block.y - half, (float)block.z - half},
			{(float)block.x + half, (float)block.y - half, (float)block.z - half},
			{(float)block.x + half, (float)block.y - half, (float)block.z + half},
			{(float)block.x - half, (float)block.y - half, (float)block.z + half},
			{(float)block.x - half, (float)block.y + half, (float)block.z - half},
			{(float)block.x + half, (float)block.y + half, (float)block.z - half},
			{(float)block.x + half, (float)block.y + half, (float)block.z + half},
			{(float)block.x - half, (float)block.y + half, (float)block.z + half}
		};

		float screen_x[8], screen_y[8];
		bool visible[8] = { false, false, false, false, false, false, false, false };
		int valid_count = 0;
		float min_x = FLT_MAX, max_x = -FLT_MAX;
		float min_y = FLT_MAX, max_y = -FLT_MAX;

		for (int i = 0; i < 8; i++)
		{
			if (projection::world_to_screen(corners[i][0], corners[i][1], corners[i][2], screen_x[i], screen_y[i]))
			{
				visible[i] = true;
				if (screen_x[i] < min_x) min_x = screen_x[i];
				if (screen_x[i] > max_x) max_x = screen_x[i];
				if (screen_y[i] < min_y) min_y = screen_y[i];
				if (screen_y[i] > max_y) max_y = screen_y[i];
				valid_count++;
			}
		}

		if (valid_count < 4) continue;

		// Select color based on type
		ImU32 box_color;
		switch (block.type)
		{
			case 0: box_color = chest_color; break;
			case 1: box_color = ender_chest_color; break;
			case 2: box_color = shulker_color; break;
			default: box_color = chest_color; break;
		}

		if (globals::storage_esp_mode == 1)
		{
			// 3D wireframe: draw all 12 edges; skip edges with a corner behind
			// the camera to avoid the classic "through-the-screen" streaks.
			static const int edges[12][2] = {
				{0,1},{1,2},{2,3},{3,0},
				{4,5},{5,6},{6,7},{7,4},
				{0,4},{1,5},{2,6},{3,7}
			};
			for (int e = 0; e < 12; e++)
			{
				int a = edges[e][0], b = edges[e][1];
				if (!visible[a] || !visible[b]) continue;
				draw_list->AddLine(ImVec2(screen_x[a], screen_y[a]), ImVec2(screen_x[b], screen_y[b]), box_color, 1.5f);
			}
		}
		else
		{
			// 2D screen-space rounded box with outline
			float rounding = 4.0f;
			draw_list->AddRect(ImVec2(min_x, min_y), ImVec2(max_x, max_y), box_color, rounding, 0, 1.5f);
			draw_list->AddRect(ImVec2(min_x - 1, min_y - 1), ImVec2(max_x + 1, max_y + 1), black_color, rounding + 1.0f, 0, 1.0f);
		}
	}
}

void flaway::modules::storage_esp::shutdown()
{
	{
		// Scoped: get_env()/DeleteGlobalRef() below must not run while the
		// render thread waits on this lock in draw_boxes().
		std::lock_guard<std::mutex> lock(storage_blocks_mutex);
		storage_blocks.clear();
		g_needs_rescan = true;
	}

	// No `g_cached` early-out: a partially resolved state can still hold
	// global refs, and every release below is a no-op on null anyway.
	if (!flaway::instance) { g_cached = false; return; }

	auto env = flaway::instance->get_env();
	if (!env) { g_cached = false; return; }

	auto release = [&](jclass& c)
	{
		if (c) { env->DeleteGlobalRef(c); c = nullptr; }
	};
	release(g_client_world_class);
	release(g_block_entity_class);
	release(g_block_pos_class);
	release(g_ender_be_class);
	release(g_shulker_be_class);
	release(g_container_be_class);
	release(g_java_set_class);
	release(g_java_iterator_class);

	g_block_entities_field = nullptr;
	g_be_get_pos_mid = nullptr;
	g_bp_get_x_mid = nullptr;
	g_bp_get_y_mid = nullptr;
	g_bp_get_z_mid = nullptr;
	g_set_iterator_mid = nullptr;
	g_iter_has_next_mid = nullptr;
	g_iter_next_mid = nullptr;
	g_cached = false;
	g_resolve_attempt_us = 0;
}
