#include "ah_utils.h"
// These two were written as if the file lived in flaway/modules/<x>/ :
// from flaway/utils/ they resolved to <repo>/flaway.h and <repo>/utils/logger.h,
// neither of which exists, so the TU could not be compiled at all.
#include "../flaway.h"
#include "logger.h"
#include <sdk/minecraft/minecraft.h>
#include <sdk/mappings/mappings.hpp>
#include <sdk/classloader.h>

#include <chrono>
#include <cctype>
#include <string>
#include <vector>

// Yarn 1.21.10 (intermediary) names used below (verified from the
// yarn-1.21.10+build.1 mapping jars):
//   ClientPlayNetworkHandler.sendChatCommand(String)        = method_45730
//   MinecraftClient.currentScreen                           = field_1755
//   Screen.getTitle()                                       = method_25440
//   HandledScreen.handler                                   = field_2797
//   ScreenHandler.getStacks()                               = method_7602
//   ScreenHandler.getSlot(int)                              = method_7611
//   Slot.getStack()                                         = method_7677
//   Item.TooltipContext.DEFAULT                             = field_51353
//   TooltipType.BASIC                                       = field_41070
//   ItemStack.getTooltip(Item.TooltipContext,Player,TooltipType) = method_7950
//   ItemStack.getName()                                     = method_7964
//   ItemStack.getCount()                                    = method_7947
//   PlayerEntity.closeHandledScreen()                       = method_7346
//   ScreenHandler.onSlotClick(int,int,SlotActionType,Player)= method_7593
//   SlotActionType (enum)                                   = class_1713
//   World.getScoreboard()                                   = method_8428
//   Scoreboard.getObjectiveForSlot(ScoreboardDisplaySlot)   = method_1189
//   Scoreboard.getScoreboardEntries(ScoreboardObjective)    = method_1184
//   ScoreboardEntry.formatted(ScoreboardObjective)          = method_55386
//   ScoreboardDisplaySlot (enum, SIDEBAR=ordinal 1)         = class_8646

namespace flaway
{
	namespace ah_utils
	{
		namespace
		{
			const char* k_screen_class_sig = "net/minecraft/class_437";
			const char* k_handler_class_sig = "net/minecraft/class_1703";
			const char* k_item_stack_class_sig = "net/minecraft/class_1799";
			const char* k_tooltip_context_class_sig = "net/minecraft/class_1792$class_9635";
			const char* k_tooltip_type_class_sig = "net/minecraft/class_1836";
			const char* k_slot_class_sig = "net/minecraft/class_1735";
			const char* k_slot_action_type_class_sig = "net/minecraft/class_1713";
			const char* k_scoreboard_class_sig = "net/minecraft/class_269";
			const char* k_scoreboard_objective_class_sig = "net/minecraft/class_266";
			const char* k_scoreboard_display_slot_class_sig = "net/minecraft/class_8646";
			const char* k_scoreboard_entry_class_sig = "net/minecraft/class_9011";
			const char* k_text_class_sig = "net/minecraft/class_2561";
		}

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

		void send_command(JNIEnv* env, const char* command)
		{
			if (!env || !command || !*command) return;
			jobject network_handler = sdk::instance->get_network_handler();
			if (!network_handler) return;

			jclass handler_class = env->GetObjectClass(network_handler);
			if (!handler_class) { env->DeleteLocalRef(network_handler); return; }
			jmethodID send_chat_command = env->GetMethodID(handler_class,
				"method_45730", "(Ljava/lang/String;)V");
			if (env->ExceptionCheck()) env->ExceptionClear();
			if (send_chat_command)
			{
				jstring cmd = env->NewStringUTF(command);
				if (cmd)
				{
					env->CallVoidMethod(network_handler, send_chat_command, cmd);
					if (env->ExceptionCheck()) env->ExceptionClear();
					env->DeleteLocalRef(cmd);
				}
			}
			env->DeleteLocalRef(handler_class);
			env->DeleteLocalRef(network_handler);
		}

		jobject get_current_screen(JNIEnv* env)
		{
			if (!env) return nullptr;
			jobject minecraft = sdk::instance->get_minecraft();
			if (!minecraft) return nullptr;

			jclass mc_class = env->GetObjectClass(minecraft);
			if (!mc_class) { env->DeleteLocalRef(minecraft); return nullptr; }
			jfieldID screen_fid = env->GetFieldID(mc_class,
				sdk::mappings::minecraft_screen_name, sdk::mappings::minecraft_screen_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			env->DeleteLocalRef(mc_class);
			if (!screen_fid) { env->DeleteLocalRef(minecraft); return nullptr; }

			jobject screen = env->GetObjectField(minecraft, screen_fid);
			if (env->ExceptionCheck()) { env->ExceptionClear(); screen = nullptr; }
			env->DeleteLocalRef(minecraft);
			return screen; // null or owned local ref
		}

		static jstring get_text_string(JNIEnv* env, jobject text)
		{
			if (!env || !text) return nullptr;
			jclass text_class = sdk::classloader::find_class(env, k_text_class_sig);
			if (!text_class) return nullptr;
			jmethodID get_string = env->GetMethodID(text_class,
				sdk::mappings::text_get_string_name, sdk::mappings::text_get_string_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			env->DeleteLocalRef(text_class);
			if (!get_string) return nullptr;
			jstring str = (jstring)env->CallObjectMethod(text, get_string, 2147483647);
			if (env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }
			return str;
		}

		static std::string jstring_to_std(JNIEnv* env, jstring str)
		{
			if (!env || !str) return "";
			const char* utf = env->GetStringUTFChars(str, nullptr);
			if (!utf) { if (env->ExceptionCheck()) env->ExceptionClear(); return ""; }
			std::string out(utf);
			env->ReleaseStringUTFChars(str, utf);
			return out;
		}

		std::string get_screen_title(JNIEnv* env, jobject screen)
		{
			if (!env || !screen) return "";
			jclass screen_class = sdk::classloader::find_class(env, k_screen_class_sig);
			if (!screen_class) return "";
			jmethodID get_title = env->GetMethodID(screen_class, "method_25440",
				"()Lnet/minecraft/class_2561;");
			if (env->ExceptionCheck()) env->ExceptionClear();
			env->DeleteLocalRef(screen_class);
			if (!get_title) return "";

			jobject title = env->CallObjectMethod(screen, get_title);
			if (env->ExceptionCheck()) { env->ExceptionClear(); return ""; }
			if (!title) return "";
			jstring str = get_text_string(env, title);
			env->DeleteLocalRef(title);
			std::string out = jstring_to_std(env, str);
			if (str) env->DeleteLocalRef(str);
			return out;
		}

		jobject get_screen_handler(JNIEnv* env, jobject screen)
		{
			if (!env || !screen) return nullptr;
			jclass screen_class = env->GetObjectClass(screen);
			if (!screen_class) return nullptr;
			jfieldID handler_fid = env->GetFieldID(screen_class,
				sdk::mappings::screen_handler_name, sdk::mappings::screen_handler_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			env->DeleteLocalRef(screen_class);
			if (!handler_fid) return nullptr;
			jobject handler = env->GetObjectField(screen, handler_fid);
			if (env->ExceptionCheck()) { env->ExceptionClear(); handler = nullptr; }
			return handler;
		}

		int screen_handler_slot_count(JNIEnv* env, jobject handler)
		{
			if (!env || !handler) return 0;
			jclass handler_class = env->GetObjectClass(handler);
			if (!handler_class) return 0;
			jmethodID get_stacks = env->GetMethodID(handler_class,
				sdk::mappings::screen_handler_get_stacks_name,
				sdk::mappings::screen_handler_get_stacks_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			env->DeleteLocalRef(handler_class);
			if (!get_stacks) return 0;

			jobject stacks = env->CallObjectMethod(handler, get_stacks);
			if (env->ExceptionCheck()) { env->ExceptionClear(); return 0; }
			if (!stacks) return 0;
			jclass list_class = env->GetObjectClass(stacks);
			jmethodID size_mid = nullptr;
			if (list_class)
			{
				size_mid = env->GetMethodID(list_class, "size", "()I");
				if (env->ExceptionCheck()) env->ExceptionClear();
			}
			jint size = 0;
			if (size_mid)
			{
				size = env->CallIntMethod(stacks, size_mid);
				if (env->ExceptionCheck()) env->ExceptionClear();
			}
			if (list_class) env->DeleteLocalRef(list_class);
			env->DeleteLocalRef(stacks);
			return (int)size;
		}

		jobject get_handler_slot_stack(JNIEnv* env, jobject handler, int slot)
		{
			if (!env || !handler) return nullptr;
			jclass handler_class = env->GetObjectClass(handler);
			if (!handler_class) return nullptr;
			jmethodID get_slot = env->GetMethodID(handler_class,
				sdk::mappings::screen_handler_get_slot_name,
				sdk::mappings::screen_handler_get_slot_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			env->DeleteLocalRef(handler_class);
			if (!get_slot) return nullptr;

			jobject slot_obj = env->CallObjectMethod(handler, get_slot, slot);
			if (env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }
			if (!slot_obj) return nullptr;

			jclass slot_class = sdk::classloader::find_class(env, k_slot_class_sig);
			jmethodID get_stack = nullptr;
			if (slot_class)
			{
				get_stack = env->GetMethodID(slot_class,
					sdk::mappings::slot_get_stack_name, sdk::mappings::slot_get_stack_sig);
				if (env->ExceptionCheck()) env->ExceptionClear();
				env->DeleteLocalRef(slot_class);
			}
			if (!get_stack) { env->DeleteLocalRef(slot_obj); return nullptr; }
			jobject stack = env->CallObjectMethod(slot_obj, get_stack);
			if (env->ExceptionCheck()) { env->ExceptionClear(); stack = nullptr; }
			env->DeleteLocalRef(slot_obj);
			return stack;
		}

		static jobject get_stack_tooltip(JNIEnv* env, jobject stack, jobject player)
		{
			if (!env || !stack || !player) return nullptr;

			jclass context_class = sdk::classloader::find_class(env, k_tooltip_context_class_sig);
			jobject context = nullptr;
			if (context_class)
			{
				jfieldID default_fid = env->GetStaticFieldID(context_class, "field_51353",
					"Lnet/minecraft/class_1792$class_9635;");
				if (env->ExceptionCheck()) env->ExceptionClear();
				if (default_fid)
				{
					context = env->GetStaticObjectField(context_class, default_fid);
					if (env->ExceptionCheck()) { env->ExceptionClear(); context = nullptr; }
				}
				env->DeleteLocalRef(context_class);
			}
			if (!context) return nullptr;

			jclass tooltip_type_class = sdk::classloader::find_class(env, k_tooltip_type_class_sig);
			jobject basic = nullptr;
			if (tooltip_type_class)
			{
				jfieldID basic_fid = env->GetStaticFieldID(tooltip_type_class, "field_41070",
					"Lnet/minecraft/class_1836;");
				if (env->ExceptionCheck()) env->ExceptionClear();
				if (basic_fid)
				{
					basic = env->GetStaticObjectField(tooltip_type_class, basic_fid);
					if (env->ExceptionCheck()) { env->ExceptionClear(); basic = nullptr; }
				}
				env->DeleteLocalRef(tooltip_type_class);
			}
			if (!basic) { env->DeleteLocalRef(context); return nullptr; }

			jclass stack_class = sdk::classloader::find_class(env, k_item_stack_class_sig);
			if (!stack_class)
			{
				env->DeleteLocalRef(basic);
				env->DeleteLocalRef(context);
				return nullptr;
			}
			jmethodID get_tooltip = env->GetMethodID(stack_class, "method_7950",
				"(Lnet/minecraft/class_1792$class_9635;Lnet/minecraft/class_1657;Lnet/minecraft/class_1836;)Ljava/util/List;");
			if (env->ExceptionCheck()) env->ExceptionClear();
			env->DeleteLocalRef(stack_class);
			if (!get_tooltip)
			{
				env->DeleteLocalRef(basic);
				env->DeleteLocalRef(context);
				return nullptr;
			}

			jobject tooltip = env->CallObjectMethod(stack, get_tooltip, context, player, basic);
			if (env->ExceptionCheck()) { env->ExceptionClear(); tooltip = nullptr; }
			env->DeleteLocalRef(basic);
			env->DeleteLocalRef(context);
			return tooltip; // java.util.List<Text>
		}

		std::string stack_tooltip_text(JNIEnv* env, jobject stack, jobject player)
		{
			if (!env || !stack || !player) return "";
			jobject tooltip = get_stack_tooltip(env, stack, player);
			if (!tooltip) return "";

			jclass list_class = env->GetObjectClass(tooltip);
			jmethodID size_mid = nullptr;
			jmethodID get_mid = nullptr;
			if (list_class)
			{
				size_mid = env->GetMethodID(list_class, "size", "()I");
				if (env->ExceptionCheck()) env->ExceptionClear();
				get_mid = env->GetMethodID(list_class, "get", "(I)Ljava/lang/Object;");
				if (env->ExceptionCheck()) env->ExceptionClear();
				env->DeleteLocalRef(list_class);
			}
			if (!size_mid || !get_mid) { env->DeleteLocalRef(tooltip); return ""; }

			jint size = env->CallIntMethod(tooltip, size_mid);
			if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(tooltip); return ""; }

			std::string out;
			for (jint i = 0; i < size; i++)
			{
				jobject line = env->CallObjectMethod(tooltip, get_mid, i);
				if (env->ExceptionCheck()) { env->ExceptionClear(); continue; }
				if (!line) continue;
				jstring str = get_text_string(env, line);
				env->DeleteLocalRef(line);
				if (str)
				{
					std::string s = jstring_to_std(env, str);
					env->DeleteLocalRef(str);
					out += s;
					out += '\n';
				}
			}
			env->DeleteLocalRef(tooltip);
			return out;
		}

		std::string stack_display_name(JNIEnv* env, jobject stack)
		{
			if (!env || !stack) return "";
			jclass stack_class = sdk::classloader::find_class(env, k_item_stack_class_sig);
			if (!stack_class) return "";
			jmethodID get_name = env->GetMethodID(stack_class,
				sdk::mappings::itemstack_get_name_name, sdk::mappings::itemstack_get_name_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			env->DeleteLocalRef(stack_class);
			if (!get_name) return "";
			jobject text = env->CallObjectMethod(stack, get_name);
			if (env->ExceptionCheck()) { env->ExceptionClear(); return ""; }
			if (!text) return "";
			jstring str = get_text_string(env, text);
			env->DeleteLocalRef(text);
			std::string out = jstring_to_std(env, str);
			if (str) env->DeleteLocalRef(str);
			return out;
		}

		int stack_count(JNIEnv* env, jobject stack)
		{
			if (!env || !stack) return 0;
			jclass stack_class = sdk::classloader::find_class(env, k_item_stack_class_sig);
			if (!stack_class) return 0;
			jmethodID get_count = env->GetMethodID(stack_class, "method_7947", "()I");
			if (env->ExceptionCheck()) env->ExceptionClear();
			env->DeleteLocalRef(stack_class);
			if (!get_count) return 0;
			jint count = env->CallIntMethod(stack, get_count);
			if (env->ExceptionCheck()) { env->ExceptionClear(); return 0; }
			return (int)count;
		}

		long parse_price(const std::string& text)
		{
			if (text.empty()) return -1;

			// Collect every digit run (thousand separators ' ' and ',' allowed).
			// Preferred: a run immediately followed by the gold coin symbol '¤'.
			auto extract = [&text](size_t i, size_t& next) -> long
			{
				long value = 0;
				size_t j = i;
				for (; j < text.size(); j++)
				{
					char c = text[j];
					if (std::isdigit((unsigned char)c))
						value = value * 10 + (c - '0');
					else if ((c == ' ' || c == ',') && j + 1 < text.size() && std::isdigit((unsigned char)text[j + 1]))
						continue; // thousand separator inside the number
					else
						break;
				}
				next = j;
				return value;
			};

			long fallback = -1;
			size_t i = 0;
			while (i < text.size())
			{
				char c = text[i];
				if (!std::isdigit((unsigned char)c)) { i++; continue; }
				size_t next = i;
				long value = extract(i, next);
				if (value > 0 && fallback < 0) fallback = value;
				// Skip spaces, then check for the coin symbol.
				size_t k = next;
				while (k < text.size() && text[k] == ' ') k++;
				// '¤' is U+00A4 = C2 A4 in UTF-8. '\xC2\xA4' is a multi-char
				// constant (= 49828) and never equals a sign-extended char.
				if (k + 1 < text.size() &&
				    (unsigned char)text[k] == 0xC2 && (unsigned char)text[k + 1] == 0xA4)
					return value;
				i = next;
			}
			return fallback;
		}

		long parse_stack_price(JNIEnv* env, jobject stack, jobject player)
		{
			return parse_price(stack_tooltip_text(env, stack, player));
		}

		int get_balance(JNIEnv* env, jobject player, jobject world)
		{
			if (!env || !player || !world) return -1;

			jclass world_class = sdk::classloader::find_class(env, sdk::mappings::world_class_sig);
			jobject scoreboard = nullptr;
			if (world_class)
			{
				jmethodID get_scoreboard = env->GetMethodID(world_class, "method_8428",
					"()Lnet/minecraft/class_269;");
				if (env->ExceptionCheck()) env->ExceptionClear();
				if (get_scoreboard)
				{
					scoreboard = env->CallObjectMethod(world, get_scoreboard);
					if (env->ExceptionCheck()) { env->ExceptionClear(); scoreboard = nullptr; }
				}
				env->DeleteLocalRef(world_class);
			}
			if (!scoreboard) return -1;

			jclass display_slot_class = sdk::classloader::find_class(env, k_scoreboard_display_slot_class_sig);
			jobject sidebar = nullptr;
			if (display_slot_class)
			{
				jmethodID values = env->GetStaticMethodID(display_slot_class, "values",
					("()[L" + std::string(k_scoreboard_display_slot_class_sig) + ";").c_str());
				if (env->ExceptionCheck()) env->ExceptionClear();
				if (values)
				{
					jobjectArray arr = (jobjectArray)env->CallStaticObjectMethod(display_slot_class, values);
					if (env->ExceptionCheck()) env->ExceptionClear();
					if (arr)
					{
						jsize len = env->GetArrayLength(arr);
						if (len > 1) sidebar = env->GetObjectArrayElement(arr, 1); // SIDEBAR
						env->DeleteLocalRef(arr);
					}
				}
				env->DeleteLocalRef(display_slot_class);
			}
			if (!sidebar) { env->DeleteLocalRef(scoreboard); return -1; }

			jclass scoreboard_class = sdk::classloader::find_class(env, k_scoreboard_class_sig);
			jobject objective = nullptr;
			if (scoreboard_class)
			{
				jmethodID get_objective = env->GetMethodID(scoreboard_class, "method_1189",
					"(Lnet/minecraft/class_8646;)Lnet/minecraft/class_266;");
				if (env->ExceptionCheck()) env->ExceptionClear();
				if (get_objective)
				{
					objective = env->CallObjectMethod(scoreboard, get_objective, sidebar);
					if (env->ExceptionCheck()) { env->ExceptionClear(); objective = nullptr; }
				}
			}
			env->DeleteLocalRef(sidebar);
			if (!objective) { env->DeleteLocalRef(scoreboard); return -1; }

			jobject entries = nullptr;
			if (scoreboard_class)
			{
				jmethodID get_entries = env->GetMethodID(scoreboard_class, "method_1184",
					"(Lnet/minecraft/class_266;)Ljava/util/Collection;");
				if (env->ExceptionCheck()) env->ExceptionClear();
				if (get_entries)
				{
					entries = env->CallObjectMethod(scoreboard, get_entries, objective);
					if (env->ExceptionCheck()) { env->ExceptionClear(); entries = nullptr; }
				}
			}
			env->DeleteLocalRef(scoreboard_class);
			if (!entries) { env->DeleteLocalRef(objective); env->DeleteLocalRef(scoreboard); return -1; }

			jclass entry_class = sdk::classloader::find_class(env, k_scoreboard_entry_class_sig);
			jmethodID formatted_mid = nullptr;
			if (entry_class)
			{
				formatted_mid = env->GetMethodID(entry_class, "method_55386",
					"(Lnet/minecraft/class_266;)Lnet/minecraft/class_2561;");
				if (env->ExceptionCheck()) env->ExceptionClear();
				env->DeleteLocalRef(entry_class);
			}

			int balance = -1;

			jclass iterable_class = env->GetObjectClass(entries);
			jmethodID iterator_mid = nullptr;
			if (iterable_class)
			{
				iterator_mid = env->GetMethodID(iterable_class, "iterator", "()Ljava/util/Iterator;");
				if (env->ExceptionCheck()) env->ExceptionClear();
				env->DeleteLocalRef(iterable_class);
			}
			if (iterator_mid)
			{
				jobject it = env->CallObjectMethod(entries, iterator_mid);
				if (env->ExceptionCheck()) { env->ExceptionClear(); it = nullptr; }
				if (it)
				{
					jclass it_class = env->GetObjectClass(it);
					jmethodID has_next = nullptr, next_mid = nullptr;
					if (it_class)
					{
						has_next = env->GetMethodID(it_class, "hasNext", "()Z");
						if (env->ExceptionCheck()) env->ExceptionClear();
						next_mid = env->GetMethodID(it_class, "next", "()Ljava/lang/Object;");
						if (env->ExceptionCheck()) env->ExceptionClear();
						env->DeleteLocalRef(it_class);
					}
					if (has_next && next_mid)
					{
						while (balance < 0)
						{
							jboolean has = env->CallBooleanMethod(it, has_next);
							if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
							if (has == JNI_FALSE) break;
							jobject entry = env->CallObjectMethod(it, next_mid);
							if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
							if (!entry) continue;
							jstring formatted = nullptr;
							if (formatted_mid)
							{
								jobject text = env->CallObjectMethod(entry, formatted_mid, objective);
								if (env->ExceptionCheck()) { env->ExceptionClear(); text = nullptr; }
								if (text)
								{
									formatted = get_text_string(env, text);
									env->DeleteLocalRef(text);
								}
							}
							env->DeleteLocalRef(entry);
							if (formatted)
							{
								std::string s = jstring_to_std(env, formatted);
								env->DeleteLocalRef(formatted);
								// Coin balance line: contains the gold coin or a
								// money marker; take the largest digit run.
								std::string lower = s;
								for (auto& c : lower) c = (char)std::tolower((unsigned char)c);
								if (s.find("\xC2\xA4") != std::string::npos ||
									lower.find("coin") != std::string::npos ||
									lower.find("монет") != std::string::npos ||
									lower.find("баланс") != std::string::npos)
								{
									long price = parse_price(s);
									if (price > 0) balance = (int)price;
								}
							}
						}
					}
					env->DeleteLocalRef(it);
				}
			}

			env->DeleteLocalRef(entries);
			env->DeleteLocalRef(objective);
			env->DeleteLocalRef(scoreboard);
			return balance;
		}

		void close_screen(JNIEnv* env, jobject player)
		{
			if (!env || !player) return;
			jclass cpe_class = sdk::classloader::find_class(env,
				sdk::mappings::clientplayerentity_class_sig);
			if (!cpe_class) return;
			jmethodID close_mid = env->GetMethodID(cpe_class,
				sdk::mappings::close_handled_screen_name, sdk::mappings::close_handled_screen_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			env->DeleteLocalRef(cpe_class);
			if (!close_mid) return;
			env->CallVoidMethod(player, close_mid);
			if (env->ExceptionCheck()) env->ExceptionClear();
		}

		jobject get_slot_action_type(JNIEnv* env, int ordinal)
		{
			if (!env) return nullptr;
			jclass action_class = sdk::classloader::find_class(env, k_slot_action_type_class_sig);
			if (!action_class) return nullptr;
			jmethodID values = env->GetStaticMethodID(action_class, "values",
				("()[L" + std::string(k_slot_action_type_class_sig) + ";").c_str());
			if (env->ExceptionCheck()) env->ExceptionClear();
			// Delete only AFTER the static call: the local ref is live until then.
			if (!values) { env->DeleteLocalRef(action_class); return nullptr; }
			jobjectArray arr = (jobjectArray)env->CallStaticObjectMethod(action_class, values);
			env->DeleteLocalRef(action_class);
			if (env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }
			if (!arr) return nullptr;
			jsize len = env->GetArrayLength(arr);
			jobject out = nullptr;
			if (ordinal >= 0 && ordinal < len)
				out = env->GetObjectArrayElement(arr, ordinal);
			env->DeleteLocalRef(arr);
			return out;
		}

		void click_slot(JNIEnv* env, jobject handler, int slot, int button,
			int action_ordinal, jobject player)
		{
			if (!env || !handler || !player) return;
			jclass handler_class = env->GetObjectClass(handler);
			if (!handler_class) return;
			jmethodID click_mid = env->GetMethodID(handler_class,
				sdk::mappings::screen_handler_click_slot_name,
				sdk::mappings::screen_handler_click_slot_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			env->DeleteLocalRef(handler_class);
			if (!click_mid) return;

			jobject action = get_slot_action_type(env, action_ordinal);
			if (!action) return;
			env->CallVoidMethod(handler, click_mid, slot, button, action, player);
			if (env->ExceptionCheck()) env->ExceptionClear();
			env->DeleteLocalRef(action);
		}
	}
}
