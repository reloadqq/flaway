#include "modules.h"

#include "../flaway.h"
#include "../hooks/Hook.h"
#include "../globals/globals.h"
#include "../utils/logger.h"
#include <thread>
#include <chrono>
#include "aimassist/aimassist.h"
#include "triggerbot/triggerbot.h"
#include "hitbox/hitbox.h"
#include "esp/esp.h"
#include "mace/mace.h"
#include "shield_breaker/shield_breaker.h"
#include "reach/reach.h"
#include "wtap/wtap.h"
#include "stap/stap.h"
#include "server_rotation/server_rotation.h"
#include "anchor_macro/anchor_macro.h"
#include "pearl_catch/pearl_catch.h"
#include "autocrystal/autocrystal.h"
#include "autototem/autototem.h"
#include "autojumpreset/autojumpreset.h"
#include "storage_esp/storage_esp.h"
#include "base_finder/base_finder.h"
#include "stun_slam/stun_slam.h"
#include "backtrack/backtrack.h"
#include "chest_stealer/chest_stealer.h"
#include "autosprint/autosprint.h"
#include "fullbright/fullbright.h"
#include "../../platform/linux/x11_helper.h"

namespace flaway
{
	namespace modules
	{
		static void toggle_if_keybind(int keybind, bool& enabled)
		{
			if (keybind > 0 && x11_helper::is_key_just_pressed(keybind))
				enabled = !enabled;
		}

		void handle_keybinds()
		{
			toggle_if_keybind(globals::aimassist_keybind, globals::aimassist_enabled);
			toggle_if_keybind(globals::triggerbot_keybind, globals::triggerbot_enabled);
			toggle_if_keybind(globals::reach_keybind, globals::reach_enabled);
			toggle_if_keybind(globals::hitbox_keybind, globals::hitbox_enabled);
			toggle_if_keybind(globals::shield_breaker_keybind, globals::shield_breaker_enabled);
			toggle_if_keybind(globals::mace_keybind, globals::mace_enabled);
			toggle_if_keybind(globals::autocrystal_keybind, globals::autocrystal_enabled);
			toggle_if_keybind(globals::autototem_keybind, globals::autototem_enabled);
			toggle_if_keybind(globals::pearl_catch_keybind, globals::pearl_catch_enabled);
			toggle_if_keybind(globals::anchor_macro_keybind, globals::anchor_macro_enabled);
			toggle_if_keybind(globals::backtrack_keybind, globals::backtrack_enabled);
			toggle_if_keybind(globals::autojumpreset_keybind, globals::autojumpreset_enabled);
			toggle_if_keybind(globals::esp_keybind, globals::box_enabled);
			toggle_if_keybind(globals::chest_stealer_keybind, globals::chest_stealer_enabled);
			toggle_if_keybind(globals::fullbright_keybind, globals::fullbright_enabled);
			toggle_if_keybind(globals::base_finder_keybind, globals::base_finder_enabled);
		}

		void run_all()
		{
			// No module/JNI work after unhook has started: the classloader
			// globals are being destroyed and get_env may be gone.
			if (Hook::get_unhooked()) return;

			// Guard: if the cheat hasn't finished init yet (constructor still
			// running, or init failed), don't touch any JNI/sdk state — the
			// Render thread can call this via the glfwSwapBuffers hook before
			// flaway::instance->init() has completed.
			if (!flaway::instance || !flaway::instance->initialized) return;


			// Debug FPS / loop timing log (only when enabled in the GUI).
			// Sampled ~every 3 s so the log doesn't spam while diagnosing a
			// stutter.
			static auto g_debug_last_log = std::chrono::steady_clock::now();
			static float g_debug_fps_avg = 0.0f;
			static float g_debug_frame_ms = 0.0f;
			{
				auto now = std::chrono::steady_clock::now();
				float ms = (float)std::chrono::duration_cast<std::chrono::microseconds>(now - g_debug_last_log).count() / 1000.0f;
				g_debug_last_log = now;
				if (ms > 0.1f) g_debug_fps_avg = 1000.0f / ms;
				g_debug_frame_ms = ms;
				if (globals::debug_logging_enabled)
				{
					static int g_debug_tick = 0;
					if (++g_debug_tick >= 180)
					{
						g_debug_tick = 0;
						logger::log("[debug] fps=" + std::to_string((int)g_debug_fps_avg) +
							" frame_ms=" + std::to_string(g_debug_frame_ms));
					}
				}
			}

			aimassist::run();
			triggerbot::run();
			hitbox_expander::run();
			esp::run();
			mace::run();
			shield_breaker::run();
			reach::run();
			wtap::run();
			stap::run();
			server_rotation::run();
			anchor_macro::run();
			pearl_catch::run();
			autocrystal::run();
			autototem::run();
			autojumpreset::run();
			storage_esp::run();
			stun_slam::run();
			backtrack::run();
			chest_stealer::run();
			autosprint::run();
			fullbright::run();
			base_finder::run();
		}

		void check_unhook_all()
		{
			if (globals::unhook_all_enabled) {
				// Launch unhook function in a separate thread to avoid blocking
				std::thread([]() {
					if (flaway::instance)
						flaway::instance->unhook_all();
				}).detach();
				// Reset state after triggering
				globals::unhook_all_enabled = false;
			}
		}
	}
}
