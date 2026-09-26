#include "chest_stealer.h"
#include "../../flaway.h"
#include "../../globals/globals.h"
#include "../../utils/logger.h"
#include <sdk/minecraft/minecraft.h>
#include <sdk/minecraft/player/player.h>
#include <sdk/mappings/mappings.hpp>
#include <sdk/classloader.h>

#include <ctime>
#include <random>
#include <vector>
#include <algorithm>

namespace flaway
{
	namespace modules
	{
		namespace
		{
			std::mt19937 cs_rng(0);

			// State machine (ported from Zenith Cheststealer): one start timer is
			// reused for both the initial start delay and the per-item delay, plus
			// a separate close timer for the auto-close step.
			jobject current_container = nullptr;   // global ref, for change detection
			uint64_t start_timer_ms = 0;
			uint64_t close_timer_ms = 0;
			bool has_started = false;
			bool is_closing = false;
			std::vector<int> slot_order;

			uint64_t now_ms()
			{
				struct timespec ts;
				clock_gettime(CLOCK_MONOTONIC, &ts);
				return (uint64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
			}

			bool has_passed(uint64_t start, uint64_t delay_ms)
			{
				return now_ms() - start >= delay_ms;
			}

			// ---- Mode presets (ticks) ----
			int get_start_ticks()
			{
				switch (globals::chest_stealer_mode)
				{
				case 0: return 8;   // FunTime
				case 1: return 1;   // HolyWorld
				case 2: return 5;   // ReallyWorld
				default: return globals::chest_stealer_start_delay;
				}
			}
			int get_min_ticks()
			{
				switch (globals::chest_stealer_mode)
				{
				case 0: return 4;
				case 1: return 1;
				case 2: return 1;
				default: return globals::chest_stealer_min_delay;
				}
			}
			int get_max_ticks()
			{
				switch (globals::chest_stealer_mode)
				{
				case 0: return 8;
				case 1: return 1;
				case 2: return 4;
				default: return globals::chest_stealer_max_delay;
				}
			}
			int get_close_ticks()
			{
				switch (globals::chest_stealer_mode)
				{
				case 0: return 7;
				case 1: return 0;
				case 2: return 2;
				default: return globals::chest_stealer_close_delay;
				}
			}

			uint64_t random_item_delay_ms()
			{
				int lo = std::min(get_min_ticks(), get_max_ticks());
				int hi = std::max(get_min_ticks(), get_max_ticks());
				std::uniform_int_distribution<int> dist(lo, hi);
				return (uint64_t)dist(cs_rng) * 50ULL;
			}

			void reset_state(JNIEnv* env, int container_slots)
			{
				has_started = false;
				is_closing = false;
				uint64_t t = now_ms();
				start_timer_ms = t;
				close_timer_ms = t;

				slot_order.clear();
				for (int i = 0; i < container_slots; i++)
					slot_order.push_back(i);
				std::shuffle(slot_order.begin(), slot_order.end(), cs_rng);
			}
		}

		void chest_stealer::run()
		{
			auto env = flaway::instance->get_env();
			if (!env) return;

			if (!globals::chest_stealer_enabled)
			{
				if (current_container) { env->DeleteGlobalRef(current_container); current_container = nullptr; }
				slot_order.clear();
				has_started = false;
				is_closing = false;
				return;
			}

			// MinecraftClient.currentScreen
			jobject minecraft = sdk::instance->get_minecraft();
			if (!minecraft) return;

			jclass mc_class = env->GetObjectClass(minecraft);
			if (!mc_class) { env->DeleteLocalRef(minecraft); return; }
			jfieldID screen_fid = env->GetFieldID(mc_class,
				sdk::mappings::minecraft_screen_name, sdk::mappings::minecraft_screen_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			env->DeleteLocalRef(mc_class);
			if (!screen_fid) { env->DeleteLocalRef(minecraft); return; }

			jobject current_screen = env->GetObjectField(minecraft, screen_fid);
			env->DeleteLocalRef(minecraft);
			if (!current_screen)
			{
				if (current_container) { env->DeleteGlobalRef(current_container); current_container = nullptr; }
				slot_order.clear();
				has_started = false;
				is_closing = false;
				return;
			}

			// Must be a HandledScreen (container screen).
			jclass handled_screen_class = sdk::classloader::find_class(env,
				sdk::mappings::handled_screen_class_sig);
			if (!handled_screen_class) { env->DeleteLocalRef(current_screen); return; }
			jboolean is_handled = env->IsInstanceOf(current_screen, handled_screen_class);
			env->DeleteLocalRef(handled_screen_class);
			if (!is_handled)
			{
				if (current_container) { env->DeleteGlobalRef(current_container); current_container = nullptr; }
				slot_order.clear();
				has_started = false;
				is_closing = false;
				env->DeleteLocalRef(current_screen);
				return;
			}

			// HandledScreen.handler -> ScreenHandler
			jclass screen_class = env->GetObjectClass(current_screen);
			if (!screen_class) { env->DeleteLocalRef(current_screen); return; }
			jfieldID handler_fid = env->GetFieldID(screen_class,
				sdk::mappings::screen_handler_name, sdk::mappings::screen_handler_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			env->DeleteLocalRef(screen_class);
			if (!handler_fid) { env->DeleteLocalRef(current_screen); return; }

			jobject container = env->GetObjectField(current_screen, handler_fid);
			env->DeleteLocalRef(current_screen);
			if (!container)
			{
				if (current_container) { env->DeleteGlobalRef(current_container); current_container = nullptr; }
				slot_order.clear();
				has_started = false;
				is_closing = false;
				return;
			}

			jclass container_class = env->GetObjectClass(container);
			if (!container_class) { env->DeleteLocalRef(container); return; }

			// ScreenHandler has no size() in 1.21.10; total slot count = getStacks().size().
			jmethodID get_stacks_mid = env->GetMethodID(container_class,
				sdk::mappings::screen_handler_get_stacks_name,
				sdk::mappings::screen_handler_get_stacks_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			if (!get_stacks_mid) { env->DeleteLocalRef(container_class); env->DeleteLocalRef(container); return; }

			jobject stacks_list = env->CallObjectMethod(container, get_stacks_mid);
			if (env->ExceptionCheck()) { env->ExceptionClear(); stacks_list = nullptr; }
			jint slot_count = 0;
			if (stacks_list)
			{
				jclass list_class = env->GetObjectClass(stacks_list);
				if (list_class)
				{
					jmethodID size_mid = env->GetMethodID(list_class, "size", "()I");
					if (env->ExceptionCheck()) env->ExceptionClear();
					if (size_mid)
					{
						slot_count = env->CallIntMethod(stacks_list, size_mid);
						if (env->ExceptionCheck()) env->ExceptionClear();
					}
					env->DeleteLocalRef(list_class);
				}
				env->DeleteLocalRef(stacks_list);
			}
			jint container_slots = slot_count - 36; // container slots only, player inventory starts at slot_count-36
			if (container_slots < 0) container_slots = 0;

			// New container (or same screen but different handler) -> reset state.
			if (current_container && env->IsSameObject(current_container, container) == JNI_FALSE)
			{
				env->DeleteGlobalRef(current_container);
				current_container = nullptr;
			}
			if (!current_container)
			{
				current_container = env->NewGlobalRef(container);
				reset_state(env, container_slots);
			}

			jmethodID get_slot_mid = env->GetMethodID(container_class,
				sdk::mappings::screen_handler_get_slot_name, sdk::mappings::screen_handler_get_slot_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();

			jclass slot_class = sdk::classloader::find_class(env, sdk::mappings::slot_class_sig);
			jmethodID has_stack_mid = nullptr;
			if (slot_class)
			{
				has_stack_mid = env->GetMethodID(slot_class,
					sdk::mappings::slot_has_stack_name, sdk::mappings::slot_has_stack_sig);
				if (env->ExceptionCheck()) env->ExceptionClear();
			}

			// SlotActionType.QUICK_MOVE
			jclass slot_action_class = sdk::classloader::find_class(env,
				sdk::mappings::slot_action_type_class_sig);
			jobject quick_move_action = nullptr;
			if (slot_action_class)
			{
				jfieldID quick_move_fid = env->GetStaticFieldID(slot_action_class,
					sdk::mappings::slot_action_type_quick_move_name,
					sdk::mappings::slot_action_type_class_sig);
				if (quick_move_fid)
					quick_move_action = env->GetStaticObjectField(slot_action_class, quick_move_fid);
				else
				{
					env->ExceptionClear();
					jmethodID values_mid = env->GetStaticMethodID(slot_action_class,
						"values", ("()[L" + std::string(sdk::mappings::slot_action_type_class_sig) + ";").c_str());
					if (values_mid)
					{
						jobjectArray values = (jobjectArray)env->CallStaticObjectMethod(slot_action_class, values_mid);
						if (env->ExceptionCheck()) env->ExceptionClear();
						if (values)
						{
							jsize len = env->GetArrayLength(values);
							if (len > 1)
								quick_move_action = env->GetObjectArrayElement(values, 1);
							env->DeleteLocalRef(values);
						}
					}
				}
				if (env->ExceptionCheck()) env->ExceptionClear();
			}

			jmethodID click_slot_mid = env->GetMethodID(container_class,
				sdk::mappings::screen_handler_click_slot_name,
				sdk::mappings::screen_handler_click_slot_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();

			jobject player = sdk::instance->get_player();

			// Initial start delay before the first item moves.
			if (!has_started)
			{
				if (!has_passed(start_timer_ms, (uint64_t)get_start_ticks() * 50ULL))
				{
					goto cleanup;
				}
				has_started = true;
			}

			if (get_slot_mid && has_stack_mid && click_slot_mid && quick_move_action && player)
			{
				// Steal one non-empty container slot once its random delay has passed.
				for (int slot_id : slot_order)
				{
					if (slot_id >= container_slots) continue;

					jobject slot = env->CallObjectMethod(container, get_slot_mid, slot_id);
					if (env->ExceptionCheck()) { env->ExceptionClear(); continue; }
					if (!slot) continue;

					bool has_item = env->CallBooleanMethod(slot, has_stack_mid) == JNI_TRUE;
					if (env->ExceptionCheck()) env->ExceptionClear();
					env->DeleteLocalRef(slot);

					if (!has_item) continue;

					if (!has_passed(start_timer_ms, random_item_delay_ms()))
						break;

					env->CallVoidMethod(container, click_slot_mid,
						slot_id, 0, quick_move_action, player);
					if (env->ExceptionCheck()) env->ExceptionClear();

					start_timer_ms = now_ms();
					is_closing = false;
					break;
				}

				// Auto-close once the container is empty.
				bool chest_empty = true;
				for (int slot_id = 0; slot_id < container_slots; slot_id++)
				{
					jobject slot = env->CallObjectMethod(container, get_slot_mid, slot_id);
					if (env->ExceptionCheck()) { env->ExceptionClear(); continue; }
					if (!slot) continue;
					bool has_item = env->CallBooleanMethod(slot, has_stack_mid) == JNI_TRUE;
					if (env->ExceptionCheck()) env->ExceptionClear();
					env->DeleteLocalRef(slot);
					if (has_item) { chest_empty = false; break; }
				}

				if (globals::chest_stealer_close_screen && chest_empty)
				{
					if (!is_closing)
					{
						is_closing = true;
						close_timer_ms = now_ms();
					}
					else if (has_passed(close_timer_ms, (uint64_t)get_close_ticks() * 50ULL))
					{
						// ClientPlayerEntity.closeHandledScreen()
						jclass cpe_class = sdk::classloader::find_class(env,
							sdk::mappings::clientplayerentity_class_sig);
						if (cpe_class)
						{
							jmethodID close_mid = env->GetMethodID(cpe_class,
								sdk::mappings::close_handled_screen_name,
								sdk::mappings::close_handled_screen_sig);
							if (env->ExceptionCheck()) env->ExceptionClear();
							if (close_mid)
							{
								env->CallVoidMethod(player, close_mid);
								if (env->ExceptionCheck()) env->ExceptionClear();
							}
							env->DeleteLocalRef(cpe_class);
						}
						is_closing = false;
					}
				}
				else
				{
					is_closing = false;
				}
			}

		cleanup:
			if (player) env->DeleteLocalRef(player);
			if (quick_move_action) env->DeleteLocalRef(quick_move_action);
			if (slot_action_class) env->DeleteLocalRef(slot_action_class);
			if (slot_class) env->DeleteLocalRef(slot_class);
			env->DeleteLocalRef(container_class);
			env->DeleteLocalRef(container);
		}

		void chest_stealer::cleanup()
		{
		}
	}
}
