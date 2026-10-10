#include "modules.h"

#include "../flaway.h"
#include "../hooks/Hook.h"
#include "../globals/globals.h"
#include "../utils/logger.h"
#include <thread>
#include <chrono>
#include "triggerbot/triggerbot.h"
#include "aimtarget/aimtarget.h"
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
#include "fog/fog.h"
#include "better_minecraft/better_minecraft.h"
#include "chat_command/chat_command.h"
#include "../../platform/linux/x11_helper.h"
#include <sdk/minecraft/minecraft.h>

namespace flaway
{
	namespace modules
	{
		static void toggle_if_keybind(int keybind, bool& enabled)
		{
			if (keybind > 0 && x11_helper::is_key_just_pressed(keybind))
				enabled = !enabled;
		}

		// Modules that consume the same keybind inside their own run()
		// (Hold/Toggle/Always activation). Flipping `enabled` on every press
		// here fought that logic: the press toggled the module on AND the
		// module's internal mode at once, so the next press turned it off or
		// left it permanently disabled. The key only wakes the module up when
		// it is off - the module itself decides what the press does.
		static void wake_if_keybind(int keybind, bool& enabled)
		{
			if (keybind > 0 && x11_helper::is_key_just_pressed(keybind))
			{
				if (!enabled) enabled = true;
			}
		}

		void handle_keybinds()
		{
			// Any open game screen (chat, inventory, anvil rename, sign edit, ...)
			// owns the keyboard: pressing keys while typing must not toggle
			// modules. Drop the presses as well, otherwise everything typed
			// would fire as one burst the moment the screen closes.
			if (flaway::instance && sdk::instance && sdk::instance->is_screen_open())
			{
				x11_helper::drain_key_presses();
				return;
			}

			toggle_if_keybind(globals::triggerbot_keybind, globals::triggerbot_enabled);
			toggle_if_keybind(globals::aimtarget_keybind, globals::aimtarget_enabled);
			wake_if_keybind(globals::reach_keybind, globals::reach_enabled);
			toggle_if_keybind(globals::hitbox_keybind, globals::hitbox_enabled);
			toggle_if_keybind(globals::shield_breaker_keybind, globals::shield_breaker_enabled);
			toggle_if_keybind(globals::mace_keybind, globals::mace_enabled);
			wake_if_keybind(globals::autocrystal_keybind, globals::autocrystal_enabled);
			toggle_if_keybind(globals::autototem_keybind, globals::autototem_enabled);
			wake_if_keybind(globals::pearl_catch_keybind, globals::pearl_catch_enabled);
			wake_if_keybind(globals::anchor_macro_keybind, globals::anchor_macro_enabled);
			wake_if_keybind(globals::backtrack_keybind, globals::backtrack_enabled);
			toggle_if_keybind(globals::autojumpreset_keybind, globals::autojumpreset_enabled);
			toggle_if_keybind(globals::esp_keybind, globals::box_enabled);
			toggle_if_keybind(globals::chest_stealer_keybind, globals::chest_stealer_enabled);
			toggle_if_keybind(globals::fullbright_keybind, globals::fullbright_enabled);
			toggle_if_keybind(globals::base_finder_keybind, globals::base_finder_enabled);
			// Shown in the menu (Theme -> Unhook) and in the HUD keybind list,
			// but never consumed before: the unbindable "Unload" keybind did
			// nothing.
			toggle_if_keybind(globals::unhook_all_keybind, globals::unhook_all_enabled);

			// Drop anything this frame did not consume. Nothing else reads the
			// queue while the menu is closed (the GUI only drains it when the
			// overlay renders), so presses used to pile up until the 64-entry
			// cap dropped every later keybind press - binds silently died.
			// While a screen is open or the capture is pending the caller
			// skips us entirely, so capture/typing is unaffected.
			x11_helper::drain_key_presses();
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

			triggerbot::run();
			aimtarget::run();
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
			fog::run();
			base_finder::run();
			better_minecraft::run();

			// chat_command needs a retry: ChatScreen may not be loaded yet
			// when flaway::initialize() first calls init().
			static bool s_chat_cmd_ready = false;
			if (!s_chat_cmd_ready)
				s_chat_cmd_ready = chat_command::init();
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
