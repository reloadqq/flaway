#include "backtrack.h"
#include "backtrack_hook.h"
#include "../../flaway.h"
#include "../../globals/globals.h"
#include "../../gui/GUI.h"
#include "../../utils/logger.h"
#include <sdk/minecraft/minecraft.h>
#include <sdk/minecraft/world/world.h>
#include <sdk/minecraft/entity/entity.h>
#include <sdk/minecraft/util/box.h>
#include <sdk/classloader.h>
#include <sdk/projection.h>
#include <cmath>
#include <cfloat>
#include <limits>
#include <vector>
#include <deque>
#include <map>
#include <mutex>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static bool hook_initialized = false;
static bool last_key_state = false;
static bool backtrack_toggled = false;
static ULONGLONG last_cooldown_end = 0;
static bool is_active = false;
static double last_local_velocity_magnitude = 0.0;
static ULONGLONG knockback_disable_until = 0;

// Position history storage (keyed by entity ID)
static std::map<int32_t, std::deque<backtrack_position_record>> player_position_history;
static std::mutex position_history_mutex;

// Visualization data
static std::vector<backtrack_player_data> backtrack_players;
static std::mutex backtrack_players_mutex;

static double calculate_distance(double x1, double y1, double z1, double x2, double y2, double z2)
{
	double dx = x2 - x1;
	double dy = y2 - y1;
	double dz = z2 - z1;
	return sqrt(dx * dx + dy * dy + dz * dz);
}

static double get_velocity_magnitude(jobject velocity_vec)
{
	if (!velocity_vec) return 0.0;
	
	auto env = flaway::instance->get_env();
	if (!env) return 0.0;
	
	jclass vec3d_class = sdk::classloader::find_class(env, sdk::mappings::vec3d_class_sig);
	if (!vec3d_class) return 0.0;
	
	jfieldID x_fid = env->GetFieldID(vec3d_class, sdk::mappings::vec3d_x_name, sdk::mappings::vec3d_x_sig);
	if (env->ExceptionCheck()) { env->ExceptionClear(); x_fid = nullptr; }
	jfieldID y_fid = env->GetFieldID(vec3d_class, sdk::mappings::vec3d_y_name, sdk::mappings::vec3d_y_sig);
	if (env->ExceptionCheck()) { env->ExceptionClear(); y_fid = nullptr; }
	jfieldID z_fid = env->GetFieldID(vec3d_class, sdk::mappings::vec3d_z_name, sdk::mappings::vec3d_z_sig);
	if (env->ExceptionCheck()) { env->ExceptionClear(); z_fid = nullptr; }
	
	if (!x_fid || !y_fid || !z_fid)
	{
		env->DeleteLocalRef(vec3d_class);
		return 0.0;
	}
	
	double vx = env->GetDoubleField(velocity_vec, x_fid);
	if (env->ExceptionCheck()) { env->ExceptionClear(); vx = 0.0; }
	double vy = env->GetDoubleField(velocity_vec, y_fid);
	if (env->ExceptionCheck()) { env->ExceptionClear(); vy = 0.0; }
	double vz = env->GetDoubleField(velocity_vec, z_fid);
	if (env->ExceptionCheck()) { env->ExceptionClear(); vz = 0.0; }
	
	env->DeleteLocalRef(vec3d_class);
	
	return sqrt(vx * vx + vy * vy + vz * vz);
}

static int get_hurt_time(jobject entity)
{
	if (!entity) return 999;
	
	// Check if mappings are available
	if (!sdk::mappings::living_entity_hurt_time_name || !sdk::mappings::living_entity_hurt_time_sig)
	{
		return 999; // Mappings not loaded yet
	}
	
	auto env = flaway::instance->get_env();
	if (!env) return 999;
	
	jclass living_entity_class = sdk::classloader::find_class(env, sdk::mappings::living_entity_class_sig);
	if (!living_entity_class) return 999;
	
	jfieldID hurt_time_fid = env->GetFieldID(living_entity_class, sdk::mappings::living_entity_hurt_time_name, sdk::mappings::living_entity_hurt_time_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();
	
	if (!hurt_time_fid)
	{
		env->DeleteLocalRef(living_entity_class);
		return 999;
	}
	
	jint hurt_time = env->GetIntField(entity, hurt_time_fid);
	if (env->ExceptionCheck()) env->ExceptionClear();
	
	env->DeleteLocalRef(living_entity_class);
	
	return hurt_time;
}

static bool is_keybind_active()
{
	// If module not enabled, not active
	if (!globals::backtrack_enabled)
	{
		return false;
	}
	
	// If no keybind set, always active when enabled
	if (globals::backtrack_keybind == 0)
	{
		return true;
	}
	
	bool key_now = (GetAsyncKeyState(globals::backtrack_keybind) & 0x8000) != 0;
	bool key_pressed = key_now && !last_key_state;
	last_key_state = key_now;
	
	// Mode 0 = Hold: active while key is held
	if (globals::backtrack_mode == 0)
	{
		return key_now;
	}
	
	// Mode 1 = Toggle: toggle on key press
	if (globals::backtrack_mode == 1)
	{
		if (key_pressed)
		{
			backtrack_toggled = !backtrack_toggled;
		}
		return backtrack_toggled;
	}
	
	// Mode 2 = Always: always active if enabled (and keybind is set)
	if (globals::backtrack_mode == 2)
	{
		return true;
	}
	
	return false;
}

void flaway::modules::backtrack::run()
{
	// Only initialize hook when game is ready
		if (!hook_initialized)
		{
			// Wait for Minecraft instance to be ready before initializing hook
			if (sdk::instance && flaway::instance && flaway::instance->get_env())
			{
				// Try to get a player to ensure game is loaded
				jobject test_player = sdk::instance->get_player();
				if (test_player)
				{
					flaway::instance->get_env()->DeleteLocalRef(test_player);
					if (backtrack_hook::init())
					{
						hook_initialized = true;
						logger::log_debug("[Backtrack] Hook initialized successfully");
					}
					else
					{
						logger::log_error("[Backtrack] Failed to initialize hook");
					}
				}
			}
		}
		
		if (!globals::backtrack_enabled)
		{
			reset();
			backtrack_toggled = false;
			last_key_state = false;
			is_active = false;
			backtrack_hook::set_delay_enabled(false);
			return;
		}
		
		// Safety: Don't enable delay if hook isn't initialized
		if (!hook_initialized)
		{
			return;
		}

	// Check keybind based on mode (hold/toggle/always)
	bool keybind_active = is_keybind_active();
	if (!keybind_active)
	{
		reset();
		// Reset toggle state when keybind becomes inactive in hold mode
		if (globals::backtrack_mode == 0)
		{
			backtrack_toggled = false;
		}
		is_active = false;
		backtrack_hook::set_delay_enabled(false);
		return;
	}

	// Check cooldown
	ULONGLONG current_time = GetTickCount64();
	if (current_time < last_cooldown_end)
	{
		is_active = false;
		backtrack_hook::set_delay_enabled(false);
		return;
	}

	// Check knockback disable
	if (globals::backtrack_disable_on_hit && current_time < knockback_disable_until)
	{
		is_active = false;
		backtrack_hook::set_delay_enabled(false);
		return;
	}

	// Throttle the heavy JNI scan. At an uncapped loop rate this whole block
	// (get_env + get_player + get_world + get_players + per-player field reads)
	// used to run on EVERY frame — hundreds of times per second — pinning a core
	// and dragging game FPS and the HUD down. 40 ms (25 Hz) is plenty: the
	// position history replay interpolates between recorded snapshots, so
	// nothing below cares that fresh records arrive 25x/s instead of every frame.
	{
		static ULONGLONG s_last_scan_ms = 0;
		if (s_last_scan_ms != 0 && current_time - s_last_scan_ms < 40)
		{
			is_active = true;
			backtrack_hook::set_delay_enabled(true);
			return;
		}
		s_last_scan_ms = current_time;
	}

	// Get world and players early for env access
	auto env = flaway::instance->get_env();
	if (!env)
	{
		is_active = false;
		backtrack_hook::set_delay_enabled(false);
		return;
	}

	// Get local player for distance calculations and knockback detection
	jobject local_player = sdk::instance->get_player();
	if (!local_player)
	{
		is_active = false;
		backtrack_hook::set_delay_enabled(false);
		return;
	}

	sdk::entity_client local_entity_client(local_player);
	double local_x = local_entity_client.get_x();
	double local_y = local_entity_client.get_y();
	double local_z = local_entity_client.get_z();

	// Detect knockback by monitoring velocity
	if (globals::backtrack_disable_on_hit)
	{
		jobject velocity = local_entity_client.get_velocity();
		double current_velocity_magnitude = get_velocity_magnitude(velocity);
		
		// If velocity suddenly increased significantly, we took knockback
		if (last_local_velocity_magnitude > 0.0 && current_velocity_magnitude > last_local_velocity_magnitude * 1.5)
		{
			knockback_disable_until = current_time + 500; // Disable for 500ms
		}
		
		last_local_velocity_magnitude = current_velocity_magnitude;
		if (velocity) env->DeleteLocalRef(velocity);
	}

	jobject world = sdk::instance->get_world();
	if (!world)
	{
		env->DeleteLocalRef(local_player);
		is_active = false;
		backtrack_hook::set_delay_enabled(false);
		return;
	}

	sdk::world_client world_client(world);
	std::vector<jobject> players = world_client.get_players();

	ULONGLONG current_timestamp = GetTickCount64();
	ULONGLONG max_age_ms = globals::backtrack_max_delay_ms;
	
	std::vector<backtrack_player_data> players_data;
	bool found_valid_target = false;

	// Get entity IDs of current players for quick lookup
	std::vector<int32_t> current_player_ids;
	for (jobject player : players)
	{
		if (player)
		{
			sdk::entity_client ec(player);
			current_player_ids.push_back(ec.get_entity_id());
		}
	}

	// Clean up old position history and track current players
	{
		std::lock_guard<std::mutex> lock(position_history_mutex);
		
		// Remove players that are no longer in the world
		std::vector<int32_t> ids_to_remove;
		for (auto& pair : player_position_history)
		{
			bool found = false;
			for (int32_t pid : current_player_ids)
			{
				if (pair.first == pid)
				{
					found = true;
					break;
				}
			}
			if (!found)
			{
				ids_to_remove.push_back(pair.first);
			}
		}
		
		for (int32_t id : ids_to_remove)
		{
			player_position_history.erase(id);
		}
		
		// Process current players
		size_t player_count = players.size();
		for (size_t pi = 0; pi < player_count; pi++)
		{
			jobject player = players[pi];
			if (!player) continue;

			sdk::entity_client entity_client(player);
			if (entity_client.is_same_object(local_player))
				continue;

			double player_x = entity_client.get_x();
			double player_y = entity_client.get_y();
			double player_z = entity_client.get_z();
			float player_yaw = entity_client.get_yaw();
			float player_pitch = entity_client.get_pitch();
			int32_t entity_id = entity_client.get_entity_id();

			// Calculate distance
			double distance = calculate_distance(local_x, local_y, local_z, player_x, player_y, player_z);

			// Check if within target distance range
			if (distance < globals::backtrack_min_distance || distance > globals::backtrack_max_distance)
			{
				continue;
			}

			// Check hurt time (i-frames)
			int hurt_time = get_hurt_time(player);
			if (hurt_time > 0 && hurt_time * 50 > globals::backtrack_max_hurt_time_ms) // 50ms per tick
			{
				continue;
			}

			// Create or update position record
			backtrack_position_record record;
			record.x = player_x;
			record.y = player_y;
			record.z = player_z;
			record.yaw = player_yaw;
			record.pitch = player_pitch;
			record.timestamp_ms = current_timestamp;
			record.entity_id = entity_id;

			player_position_history[entity_id].push_back(record);

			// Remove old records
			while (!player_position_history[entity_id].empty())
			{
				ULONGLONG age = current_timestamp - player_position_history[entity_id].front().timestamp_ms;
				if (age > max_age_ms)
				{
					player_position_history[entity_id].pop_front();
				}
				else
				{
					break;
				}
			}

			// Store visualization data
			jobject bounding_box_obj = entity_client.get_bounding_box();
			if (bounding_box_obj)
			{
				sdk::box_client box(bounding_box_obj);

				backtrack_player_data data;
				data.x = player_x;
				data.y = player_y;
				data.z = player_z;
				data.min_x = box.get_min_x();
				data.min_y = box.get_min_y();
				data.min_z = box.get_min_z();
				data.max_x = box.get_max_x();
				data.max_y = box.get_max_y();
				data.max_z = box.get_max_z();
				data.yaw = player_yaw;
				data.pitch = player_pitch;
				data.is_valid = true;
				data.entity_id = entity_id;

				players_data.push_back(data);
				found_valid_target = true;

				env->DeleteLocalRef(bounding_box_obj);
			}
		}
	}

	// Update visualization data
	{
		std::lock_guard<std::mutex> lock(backtrack_players_mutex);
		backtrack_players = players_data;
	}

	// Activate backtrack if we found a valid target
	if (found_valid_target)
	{
		is_active = true;
		backtrack_hook::set_delay_enabled(true);
		backtrack_hook::set_delay_ms(globals::backtrack_max_delay_ms);
	}
	else
	{
		is_active = false;
		backtrack_hook::set_delay_enabled(false);
	}

	// Cleanup
	for (jobject player : players)
	{
		if (player) env->DeleteLocalRef(player);
	}
	if (world) env->DeleteLocalRef(world);
	if (local_player) env->DeleteLocalRef(local_player);
}

void flaway::modules::backtrack::draw_indicators()
{
	try
	{
		if (!globals::backtrack_visualization_enabled) return;
		if (!GUI::get_is_init()) return;
		if (!is_active) return;
		if (!sdk::instance) return;
		if (sdk::instance->is_screen_open()) return;

		ImGuiIO& io = ImGui::GetIO();
		int screen_width = (int)io.DisplaySize.x;
		int screen_height = (int)io.DisplaySize.y;
		if (screen_width <= 0 || screen_height <= 0 || screen_width > 10000 || screen_height > 10000) return;

		ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
		if (!draw_list) return;

		// Get camera data
		sdk::camera_data cam = sdk::instance->get_camera();
		if (!cam.valid) return;
		
		// Validate camera data
		if (!std::isfinite(cam.x) || !std::isfinite(cam.y) || !std::isfinite(cam.z) ||
		    !std::isfinite(cam.yaw) || !std::isfinite(cam.pitch) || !std::isfinite(cam.fov) ||
		    cam.fov <= 0 || cam.fov > 180) return;

		// Extract latest positions from history (locked first, avoids lock order inversion with run())
		std::map<int32_t, backtrack_position_record> latest_positions_by_id;
		{
			std::lock_guard<std::mutex> lock(position_history_mutex);
			for (const auto& pair : player_position_history)
			{
				if (!pair.second.empty())
				{
					latest_positions_by_id[pair.first] = pair.second.back();
				}
			}
		}

		// Copy data under lock
		std::vector<backtrack_player_data> players_copy;
		{
			std::lock_guard<std::mutex> lock(backtrack_players_mutex);
			if (backtrack_players.empty()) return;
			players_copy = backtrack_players;
		}

	// Pre-compute view matrix (shared projection: aligns with every
	// other ESP overlay so backtrack boxes sit exactly on the player)
	projection::set_view((float)cam.x, (float)cam.y, (float)cam.z,
	                    cam.yaw, cam.pitch, cam.fov, screen_width, screen_height);

	// Get visualization color
	ImU32 indicator_color = IM_COL32(
		(int)(globals::backtrack_visualization_color[0] * 255),
		(int)(globals::backtrack_visualization_color[1] * 255),
		(int)(globals::backtrack_visualization_color[2] * 255),
		(int)(globals::backtrack_visualization_color[3] * 255)
	);

	float line_width = globals::backtrack_visualization_line_width;

		for (const auto& player : players_copy)
		{
			try
			{
				if (!player.is_valid) continue;

				// Validate player position data
				if (!std::isfinite(player.x) || !std::isfinite(player.y) || !std::isfinite(player.z) ||
				    !std::isfinite(player.min_x) || !std::isfinite(player.min_y) || !std::isfinite(player.min_z) ||
				    !std::isfinite(player.max_x) || !std::isfinite(player.max_y) || !std::isfinite(player.max_z))
				{
					continue; // Skip invalid data
				}

				double real_x = player.x;
				double real_y = player.y;
				double real_z = player.z;
				
				// Look up latest position by entity ID from pre-extracted history
				{
					try
					{
						auto it = latest_positions_by_id.find(player.entity_id);
						if (it != latest_positions_by_id.end())
						{
							const auto& latest = it->second;
							if (std::isfinite(latest.x) && std::isfinite(latest.y) && std::isfinite(latest.z))
							{
								real_x = latest.x;
								real_y = latest.y;
								real_z = latest.z;
							}
						}
					}
					catch (...)
					{
						// If history access fails, just use player position
					}
				}

				// Validate real position
				if (!std::isfinite(real_x) || !std::isfinite(real_y) || !std::isfinite(real_z))
				{
					continue;
				}

				// 8 corners of bounding box at real position
				float corners[8][3] = {
					{(float)(real_x + player.min_x - player.x), (float)(real_y + player.min_y - player.y), (float)(real_z + player.min_z - player.z)},
					{(float)(real_x + player.max_x - player.x), (float)(real_y + player.min_y - player.y), (float)(real_z + player.min_z - player.z)},
					{(float)(real_x + player.max_x - player.x), (float)(real_y + player.min_y - player.y), (float)(real_z + player.max_z - player.z)},
					{(float)(real_x + player.min_x - player.x), (float)(real_y + player.min_y - player.y), (float)(real_z + player.max_z - player.z)},
					{(float)(real_x + player.min_x - player.x), (float)(real_y + player.max_y - player.y), (float)(real_z + player.min_z - player.z)},
					{(float)(real_x + player.max_x - player.x), (float)(real_y + player.max_y - player.y), (float)(real_z + player.min_z - player.z)},
					{(float)(real_x + player.max_x - player.x), (float)(real_y + player.max_y - player.y), (float)(real_z + player.max_z - player.z)},
					{(float)(real_x + player.min_x - player.x), (float)(real_y + player.max_y - player.y), (float)(real_z + player.max_z - player.z)}
				};

				// Validate corners
				for (int i = 0; i < 8; i++)
				{
					if (!std::isfinite(corners[i][0]) || !std::isfinite(corners[i][1]) || !std::isfinite(corners[i][2]))
					{
						continue; // Skip this player if corners are invalid
					}
				}

				// Project corners to screen
				float screen_x[8], screen_y[8];
				int valid_count = 0;
				float min_screen_x = FLT_MAX, max_screen_x = -FLT_MAX;
				float min_screen_y = FLT_MAX, max_screen_y = -FLT_MAX;

				for (int i = 0; i < 8; i++)
				{
					if (projection::world_to_screen(corners[i][0], corners[i][1], corners[i][2], screen_x[i], screen_y[i]))
					{
						// Validate screen coordinates
						if (std::isfinite(screen_x[i]) && std::isfinite(screen_y[i]) &&
						    screen_x[i] >= -1000 && screen_x[i] <= screen_width + 1000 &&
						    screen_y[i] >= -1000 && screen_y[i] <= screen_height + 1000)
						{
							if (screen_x[i] < min_screen_x) min_screen_x = screen_x[i];
							if (screen_x[i] > max_screen_x) max_screen_x = screen_x[i];
							if (screen_y[i] < min_screen_y) min_screen_y = screen_y[i];
							if (screen_y[i] > max_screen_y) max_screen_y = screen_y[i];
							valid_count++;
						}
					}
				}

				if (valid_count < 4) continue;
				
				// Validate screen bounds before drawing
				if (!std::isfinite(min_screen_x) || !std::isfinite(max_screen_x) ||
				    !std::isfinite(min_screen_y) || !std::isfinite(max_screen_y) ||
				    min_screen_x < -10000 || max_screen_x > 20000 ||
				    min_screen_y < -10000 || max_screen_y > 20000)
				{
					continue; // Skip drawing if coordinates are invalid
				}

				// Draw box
				try
				{
					if (globals::backtrack_visualization_filled)
					{
						draw_list->AddRectFilled(ImVec2(min_screen_x, min_screen_y), ImVec2(max_screen_x, max_screen_y), indicator_color);
					}
					else
					{
						draw_list->AddRect(ImVec2(min_screen_x, min_screen_y), ImVec2(max_screen_x, max_screen_y), indicator_color, 0.0f, 0, line_width);
					}
				}
				catch (...)
				{
					// Skip drawing on error
				}
			}
			catch (...)
			{
				// Skip this player on error, continue with next
			}
		}
	}
	catch (...)
	{
		// Silent catch - don't crash on drawing errors
	}
}

void flaway::modules::backtrack::reset()
{
	is_active = false;
	backtrack_hook::set_delay_enabled(false);
	
	// Clear position history
	{
		std::lock_guard<std::mutex> lock(position_history_mutex);
		player_position_history.clear();
	}
	
	// Clear visualization data
	{
		std::lock_guard<std::mutex> lock(backtrack_players_mutex);
		backtrack_players.clear();
	}
}

void flaway::modules::backtrack::reset_hook_state()
{
	hook_initialized = false;
}
