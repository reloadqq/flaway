#include "config.h"
#include "../globals/globals.h"
#include "../utils/logger.h"

#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <Windows.h>
#include <direct.h>
#define ENH_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define ENH_MKDIR(p) mkdir(p, 0755)
#endif

namespace
{
	enum class EKind : int
	{
		K_BOOL,
		K_INT,
		K_FLOAT,
		K_DOUBLE,
		K_FLOAT4,
		K_STR,   // NUL terminated char buffer, size = capacity
	};

	struct entry
	{
		const char* key;
		EKind kind;
		void* ptr;
		int size = 0;
	};

	// One entry per persisted global. Runtime-only state (timers, saved slots,
	// *_is_active, *_last_attack) is intentionally excluded.
#define EBOOL(k, v) { (k), EKind::K_BOOL, (void*)&(v) }
#define EINT(k, v)  { (k), EKind::K_INT,  (void*)&(v) }
#define EFLT(k, v)  { (k), EKind::K_FLOAT,(void*)&(v) }
#define EDBL(k, v)  { (k), EKind::K_DOUBLE,(void*)&(v) }
#define EF4(k, v)   { (k), EKind::K_FLOAT4,(void*)&(v) }
#define ESTR(k, v)  { (k), EKind::K_STR,  (void*)&(v), (int)sizeof(v) }

	const entry k_entries[] = {
		// Aim Assist
		EBOOL("aimassist.enabled", globals::aimassist_enabled),
		EINT("aimassist.keybind", globals::aimassist_keybind),
		EBOOL("aimassist.horizontal", globals::aimassist_horizontal),
		EBOOL("aimassist.vertical", globals::aimassist_vertical),
		EFLT("aimassist.smoothing", globals::aimassist_smoothing),
		EDBL("aimassist.max_distance", globals::aimassist_max_distance),
		EFLT("aimassist.fov", globals::aimassist_fov),
		EBOOL("aimassist.randomize", globals::aimassist_randomize),
		EFLT("aimassist.random_strength", globals::aimassist_random_strength),
		EFLT("aimassist.speed", globals::aimassist_speed),
		EFLT("aimassist.prediction", globals::aimassist_prediction),
		EBOOL("aimassist.telemetry", globals::aimassist_telemetry_enabled),
		// Triggerbot
		EBOOL("triggerbot.enabled", globals::triggerbot_enabled),
		EINT("triggerbot.keybind", globals::triggerbot_keybind),
		EINT("triggerbot.keybind_mode", globals::triggerbot_keybind_mode),
		EBOOL("triggerbot.weapon_only", globals::triggerbot_weapon_only),
		EFLT("triggerbot.distance", globals::triggerbot_distance),
		EINT("triggerbot.sprint_mode", globals::triggerbot_sprint_mode),
		EBOOL("triggerbot.jump_only", globals::triggerbot_jump_only),
		// Reach
		EBOOL("reach.enabled", globals::reach_enabled),
		EINT("reach.keybind", globals::reach_keybind),
		EINT("reach.mode", globals::reach_mode),
		EDBL("reach.distance", globals::reach_distance),
		// Hitbox
		EBOOL("hitbox.enabled", globals::hitbox_enabled),
		EINT("hitbox.keybind", globals::hitbox_keybind),
		EINT("hitbox.mode", globals::hitbox_mode),
		EDBL("hitbox.expand_width", globals::hitbox_expand_width),
		EDBL("hitbox.expand_height", globals::hitbox_expand_height),
		// Shield Breaker
		EBOOL("shield_breaker.enabled", globals::shield_breaker_enabled),
		EINT("shield_breaker.keybind", globals::shield_breaker_keybind),
		EINT("shield_breaker.keybind_mode", globals::shield_breaker_keybind_mode),
		EBOOL("shield_breaker.aim", globals::shield_breaker_aim),
		EBOOL("shield_breaker.switch_back", globals::shield_breaker_switch_back),
		EINT("shield_breaker.delay_ms", globals::shield_breaker_delay_ms),
		// Mace
		EBOOL("mace.enabled", globals::mace_enabled),
		EINT("mace.keybind", globals::mace_keybind),
		EINT("mace.keybind_mode", globals::mace_keybind_mode),
		EBOOL("mace.look", globals::mace_look),
		EBOOL("mace.switch_back", globals::mace_switch_back),
		EBOOL("mace.remove_elytra", globals::mace_remove_elytra),
		EDBL("mace.min_fall_distance", globals::mace_min_fall_distance),
		EDBL("mace.height_above_target", globals::mace_height_above_target),
		EDBL("mace.fall_hitbox_width", globals::mace_fall_hitbox_width),
		EDBL("mace.fall_hitbox_height", globals::mace_fall_hitbox_height),
		// AutoCrystal
		EBOOL("autocrystal.enabled", globals::autocrystal_enabled),
		EINT("autocrystal.keybind", globals::autocrystal_keybind),
		EINT("autocrystal.mode", globals::autocrystal_mode),
		EINT("autocrystal.delay_ms", globals::autocrystal_delay_ms),
		EBOOL("autocrystal.debug", globals::autocrystal_debug_enabled),
		// AutoTotem
		EBOOL("autototem.enabled", globals::autototem_enabled),
		EINT("autototem.keybind", globals::autototem_keybind),
		EINT("autototem.mode", globals::autototem_mode),
		EBOOL("autototem.rage_mode", globals::autototem_rage_mode),
		// Anchor Macro
		EBOOL("anchor_macro.enabled", globals::anchor_macro_enabled),
		EINT("anchor_macro.keybind", globals::anchor_macro_keybind),
		EINT("anchor_macro.mode", globals::anchor_macro_mode),
		EBOOL("anchor_macro.break_anchor", globals::anchor_macro_break_anchor),
		EINT("anchor_macro.swap_delay_ms", globals::anchor_macro_swap_delay_ms),
		EINT("anchor_macro.charge_delay_ms", globals::anchor_macro_charge_delay_ms),
		EINT("anchor_macro.break_delay_ms", globals::anchor_macro_break_delay_ms),
		// Backtrack
		EBOOL("backtrack.enabled", globals::backtrack_enabled),
		EINT("backtrack.keybind", globals::backtrack_keybind),
		EINT("backtrack.mode", globals::backtrack_mode),
		EINT("backtrack.max_delay_ms", globals::backtrack_max_delay_ms),
		EDBL("backtrack.min_distance", globals::backtrack_min_distance),
		EDBL("backtrack.max_distance", globals::backtrack_max_distance),
		EINT("backtrack.max_hurt_time_ms", globals::backtrack_max_hurt_time_ms),
		EBOOL("backtrack.disable_on_hit", globals::backtrack_disable_on_hit),
		EBOOL("backtrack.visualization_enabled", globals::backtrack_visualization_enabled),
		EF4("backtrack.visualization_color", globals::backtrack_visualization_color),
		EFLT("backtrack.visualization_line_width", globals::backtrack_visualization_line_width),
		EBOOL("backtrack.visualization_filled", globals::backtrack_visualization_filled),
		EFLT("backtrack.cooldown_seconds", globals::backtrack_cooldown_seconds),
		// Stun Slam
		EBOOL("stun_slam.enabled", globals::stun_slam_enabled),
		EFLT("stun_slam.chance", globals::stun_slam_chance),
		EINT("stun_slam.swap_delay_ms", globals::stun_slam_swap_delay_ms),
		EINT("stun_slam.axe_delay_ms", globals::stun_slam_axe_delay_ms),
		EINT("stun_slam.mace_delay_ms", globals::stun_slam_mace_delay_ms),
		EDBL("stun_slam.min_fall", globals::stun_slam_min_fall),
		// S-Tap
		EBOOL("stap.enabled", globals::stap_enabled),
		EINT("stap.duration_ms", globals::stap_duration_ms),
		// W-Tap
		EBOOL("wtap.enabled", globals::wtap_enabled),
		EINT("wtap.duration_ms", globals::wtap_duration_ms),
		// Auto Jump Reset
		EBOOL("autojumpreset.enabled", globals::autojumpreset_enabled),
		EINT("autojumpreset.keybind", globals::autojumpreset_keybind),
		EINT("autojumpreset.mode", globals::autojumpreset_mode),
		EINT("autojumpreset.cooldown_ms", globals::autojumpreset_cooldown_ms),
		// ESP
		EBOOL("esp.box_enabled", globals::box_enabled),
		EBOOL("esp.health_bar", globals::esp_health_bar),
		EINT("esp.keybind", globals::esp_keybind),
		EINT("esp.mode", globals::esp_mode),
		EBOOL("esp.name_enabled", globals::esp_name_enabled),
		EBOOL("esp.item_enabled", globals::esp_item_enabled),
		EBOOL("esp.hide_vanilla_names", globals::esp_hide_vanilla_names),
		EBOOL("esp.show_fov", globals::esp_show_fov),
		EF4("esp.fov_color", globals::esp_fov_color),
		EFLT("esp.vertical_offset", globals::esp_vertical_offset),
		EBOOL("esp.tracers", globals::esp_tracers),
		EF4("esp.tracer_color", globals::esp_tracer_color),
		EBOOL("esp.arrows", globals::esp_arrows),
		// Server Rotation
		EBOOL("server_rotation.enabled", globals::server_rotation_enabled),
		// Storage ESP
		EBOOL("storage_esp.enabled", globals::storage_esp_enabled),
		EBOOL("storage_esp.chest", globals::storage_esp_chest),
		EBOOL("storage_esp.ender_chest", globals::storage_esp_ender_chest),
		EBOOL("storage_esp.shulker", globals::storage_esp_shulker),
		EINT("storage_esp.mode", globals::storage_esp_mode),
		// BaseFinder
		EBOOL("base_finder.enabled", globals::base_finder_enabled),
		EINT("base_finder.keybind", globals::base_finder_keybind),
		EINT("base_finder.mode", globals::base_finder_mode),
		EBOOL("base_finder.click", globals::base_finder_click),
		EDBL("base_finder.range", globals::base_finder_range),
		EINT("base_finder.min_size", globals::base_finder_min_size),
		EINT("base_finder.max_size", globals::base_finder_max_size),
		EINT("base_finder.min_length", globals::base_finder_min_length),
		EINT("base_finder.min_width", globals::base_finder_min_width),
		EBOOL("base_finder.holy_world", globals::base_finder_holy_world),
		EF4("base_finder.cave_color", globals::base_finder_cave_color),
		EF4("base_finder.bypass_color", globals::base_finder_bypass_color),
		EBOOL("base_finder.targets", globals::base_finder_targets),
		EBOOL("base_finder.target_chest", globals::base_finder_target_chest),
		EBOOL("base_finder.target_shulker", globals::base_finder_target_shulker),
		EBOOL("base_finder.target_spawner", globals::base_finder_target_spawner),
		EBOOL("base_finder.target_frame", globals::base_finder_target_frame),
		EBOOL("base_finder.target_endportal", globals::base_finder_target_endportal),
		EBOOL("base_finder.target_obsidian", globals::base_finder_target_obsidian),
		EBOOL("base_finder.target_players", globals::base_finder_target_players),
		EBOOL("base_finder.tracers", globals::base_finder_tracers),
		EBOOL("base_finder.log_chat", globals::base_finder_log_chat),
		EBOOL("base_finder.log_file", globals::base_finder_log_file),
		EF4("base_finder.target_color", globals::base_finder_target_color),
		// Debug / Misc
		EBOOL("debug.logging_enabled", globals::debug_logging_enabled),
		EBOOL("misc.flight_enabled", globals::flight_enabled),
		EBOOL("misc.sprint_enabled", globals::sprint_enabled),
		EBOOL("misc.autosprint_keep_swimming", globals::autosprint_keep_swimming),
		EINT("misc.pearl_catch_keybind", globals::pearl_catch_keybind),
		EINT("misc.pearl_catch_mode", globals::pearl_catch_mode),
		EINT("misc.pearl_catch_aim_mode", globals::pearl_catch_aim_mode),
		EBOOL("misc.pearl_catch_enabled", globals::pearl_catch_enabled),
		// Chest Stealer
		EBOOL("chest_stealer.enabled", globals::chest_stealer_enabled),
		EINT("chest_stealer.keybind", globals::chest_stealer_keybind),
		EINT("chest_stealer.mode", globals::chest_stealer_mode),
		EINT("chest_stealer.start_delay", globals::chest_stealer_start_delay),
		EINT("chest_stealer.min_delay", globals::chest_stealer_min_delay),
		EINT("chest_stealer.max_delay", globals::chest_stealer_max_delay),
		EINT("chest_stealer.close_delay", globals::chest_stealer_close_delay),
		EBOOL("chest_stealer.close_screen", globals::chest_stealer_close_screen),
		// Unhook All
		EINT("misc.unhook_all_keybind", globals::unhook_all_keybind),
		// Fullbright
		EBOOL("fullbright.enabled", globals::fullbright_enabled),
		EINT("fullbright.keybind", globals::fullbright_keybind),
		EDBL("fullbright.gamma", globals::fullbright_gamma),
		// Fog visual
		EBOOL("fog.enabled", globals::fog_enabled),
		EINT("fog.mode", globals::fog_mode),
		EF4("fog.color", globals::fog_color),
		EFLT("fog.strength", globals::fog_strength),
		// HUD
		EBOOL("hud.watermark_enabled", globals::hud_watermark_enabled),
		EBOOL("hud.target_enabled", globals::hud_target_enabled),
		EBOOL("hud.coords_enabled", globals::hud_coords_enabled),
		EBOOL("hud.keybinds_enabled", globals::hud_keybinds_enabled),
		EBOOL("hud.pickups_enabled", globals::hud_pickups_enabled),
		EBOOL("hud.poison_enabled", globals::hud_poison_enabled),
		EBOOL("hud.arraylist_enabled", globals::hud_arraylist_enabled),
		EFLT("hud.scale", globals::hud_scale),
		EFLT("hud.watermark_pos_x", globals::hud_watermark_pos[0]),
		EFLT("hud.watermark_pos_y", globals::hud_watermark_pos[1]),
		EFLT("hud.keybinds_pos_x", globals::hud_keybinds_pos[0]),
		EFLT("hud.keybinds_pos_y", globals::hud_keybinds_pos[1]),
		EFLT("hud.target_pos_x", globals::hud_target_pos[0]),
		EFLT("hud.target_pos_y", globals::hud_target_pos[1]),
		EFLT("hud.coords_pos_x", globals::hud_coords_pos[0]),
		EFLT("hud.coords_pos_y", globals::hud_coords_pos[1]),
		EFLT("hud.pickups_pos_x", globals::hud_pickups_pos[0]),
		EFLT("hud.pickups_pos_y", globals::hud_pickups_pos[1]),
		EFLT("hud.poison_pos_x", globals::hud_poison_pos[0]),
		EFLT("hud.poison_pos_y", globals::hud_poison_pos[1]),
		EFLT("hud.arraylist_pos_x", globals::hud_arraylist_pos[0]),
		EFLT("hud.arraylist_pos_y", globals::hud_arraylist_pos[1]),
		EFLT("hud.watermark_scale", globals::hud_watermark_scale),
		EFLT("hud.keybinds_scale", globals::hud_keybinds_scale),
		EFLT("hud.target_scale", globals::hud_target_scale),
		EFLT("hud.coords_scale", globals::hud_coords_scale),
		EFLT("hud.pickups_scale", globals::hud_pickups_scale),
		EFLT("hud.poison_scale", globals::hud_poison_scale),
		EFLT("hud.arraylist_scale", globals::hud_arraylist_scale),
		// GUI theme
		EINT("gui.theme_id", globals::theme_id),
		EBOOL("gui.background_gradient", globals::gui_background_gradient),
		EINT("gui.gradient_style", globals::gui_gradient_style),
		EINT("gui.accent", globals::theme_accent),
		EINT("gui.gradient", globals::theme_grad),
		EBOOL("gui.dark", globals::theme_dark),
		// Discord Rich Presence
		EBOOL("discord_rpc.enabled", globals::discord_rpc_enabled),
		ESTR("discord_rpc.client_id", globals::discord_rpc_client_id),
		ESTR("discord_rpc.state", globals::discord_rpc_state),
		ESTR("discord_rpc.details", globals::discord_rpc_details),
		ESTR("discord_rpc.large_image", globals::discord_rpc_large_image),
	};

	constexpr int k_entry_count = sizeof(k_entries) / sizeof(k_entries[0]);

	std::string config_dir()
	{
		const char* home = getenv("HOME");
		if (home && *home)
			return std::string(home) + "/.minecraft/flaway";
#ifdef _WIN32
		return "flaway";
#else
		return "/tmp/flaway";
#endif
	}
}

namespace flaway
{
	namespace config
	{
		const char* AUTO_PROFILE = "auto.flaway";

		std::string profile_path(const std::string& name)
		{
			return config_dir() + "/" + name;
		}

		std::string serialize()
		{
			std::ostringstream out;
			out.precision(6);
			for (int i = 0; i < k_entry_count; i++)
			{
				const entry& e = k_entries[i];
				out << e.key << "=";
				switch (e.kind)
				{
					case EKind::K_BOOL:
						out << (*(bool*)e.ptr ? "1" : "0");
						break;
					case EKind::K_INT:
						out << *(int*)e.ptr;
						break;
					case EKind::K_FLOAT:
						out << *(float*)e.ptr;
						break;
					case EKind::K_DOUBLE:
						out << *(double*)e.ptr;
						break;
					case EKind::K_FLOAT4:
					{
						float* f = (float*)e.ptr;
						out << f[0] << "," << f[1] << "," << f[2] << "," << f[3];
						break;
					}
					case EKind::K_STR:
						out << (const char*)e.ptr;
						break;
				}
				out << "\n";
			}
			return out.str();
		}

		int apply(const std::string& data)
		{
			int applied = 0;
			std::istringstream in(data);
			std::string line;
			while (std::getline(in, line))
			{
				if (line.empty() || line[0] == '#' || line[0] == ';')
					continue;
				size_t eq = line.find('=');
				if (eq == std::string::npos)
					continue;
				std::string key = line.substr(0, eq);
				std::string val = line.substr(eq + 1);

				for (int i = 0; i < k_entry_count; i++)
				{
					const entry& e = k_entries[i];
					if (key != e.key)
						continue;
					switch (e.kind)
					{
						case EKind::K_BOOL:
							*(bool*)e.ptr = (val == "1" || val == "true" || val == "on");
							break;
						case EKind::K_INT:
							*(int*)e.ptr = atoi(val.c_str());
							break;
						case EKind::K_FLOAT:
							*(float*)e.ptr = (float)atof(val.c_str());
							break;
						case EKind::K_DOUBLE:
							*(double*)e.ptr = atof(val.c_str());
							break;
						case EKind::K_FLOAT4:
						{
							float* f = (float*)e.ptr;
							std::stringstream ss(val);
							char comma;
							ss >> f[0] >> comma >> f[1] >> comma >> f[2] >> comma >> f[3];
							break;
						}
						case EKind::K_STR:
						{
							char* dst = (char*)e.ptr;
							int cap = e.size > 0 ? e.size : 1;
							strncpy(dst, val.c_str(), (size_t)cap - 1);
							dst[cap - 1] = '\0';
							break;
						}
					}
					applied++;
					break;
				}
			}
			return applied;
		}

		bool save(const std::string& name)
		{
			ENH_MKDIR(config_dir().c_str());
			std::string path = profile_path(name);
			std::ofstream f(path, std::ios::out | std::ios::trunc);
			if (!f.is_open())
			{
				logger::log_error("[config] cannot open for write: " + path);
				return false;
			}
			f << "# flaway config v1\n";
			f << "# Minecraft 1.21.10 - flaway cheat\n";
			f << serialize();
			f.close();
			logger::log("[config] saved " + name + " (" + path + ")");
			return true;
		}

		bool load(const std::string& name)
		{
			std::string path = profile_path(name);
			std::ifstream f(path, std::ios::in);
			if (!f.is_open())
			{
				logger::log("[config] profile not found, skipping: " + path);
				return false;
			}
			std::stringstream ss;
			ss << f.rdbuf();
			f.close();
			int applied = apply(ss.str());
			logger::log("[config] loaded " + name + " (" + path + ") applied " +
				std::to_string(applied) + " keys");
			return applied > 0;
		}

		bool save_auto()
		{
			return save(AUTO_PROFILE);
		}

		bool load_auto()
		{
			return load(AUTO_PROFILE);
		}
	}
}
