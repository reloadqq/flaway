#include "chat_command.h"
#include "../../flaway.h"
#include "../friend_manager/friend_manager.h"
#include <sdk/mappings/mappings.hpp>
#include <sdk/classloader.h>
#include <jni.h>
#include <string>
#include <algorithm>
#include <cctype>
#include <cstdio>

static jmethodID ORIG_send_message = nullptr;
static jclass g_chat_screen_class = nullptr;
static bool g_hooked = false;
static int jnihook_refcount = 0;

static std::string trim(const std::string& s)
{
	size_t start = s.find_first_not_of(" \t\r\n");
	if (start == std::string::npos) return "";
	size_t end = s.find_last_not_of(" \t\r\n");
	return s.substr(start, end - start + 1);
}

static std::string to_lower(const std::string& s)
{
	std::string r = s;
	std::transform(r.begin(), r.end(), r.begin(),
		[](unsigned char c) { return std::tolower(c); });
	return r;
}

static bool handle_friend_command(const std::string& msg)
{
	std::string trimmed = trim(msg);
	if (trimmed.empty() || trimmed[0] != '.') return false;

	std::string without_dot = trimmed.substr(1);
	std::string cmd_word;
	size_t space = without_dot.find(' ');
	if (space != std::string::npos)
	{
		cmd_word = to_lower(without_dot.substr(0, space));
	}
	else
	{
		cmd_word = to_lower(without_dot);
	}

	if (cmd_word != "friend" && cmd_word != "friends") return false;

	std::string rest;
	if (space != std::string::npos)
	{
		rest = trim(without_dot.substr(space + 1));
	}

	if (rest.empty())
	{
		fprintf(stderr, "[chat_command] .friend usage: .friend <add|remove|list|clear> [nick]\n"); fflush(stderr);
		return true;
	}

	std::string sub_cmd;
	std::string arg;
	size_t arg_space = rest.find(' ');
	if (arg_space != std::string::npos)
	{
		sub_cmd = to_lower(rest.substr(0, arg_space));
		arg = trim(rest.substr(arg_space + 1));
	}
	else
	{
		sub_cmd = to_lower(rest);
	}

	if (sub_cmd == "add")
	{
		if (arg.empty())
		{
			fprintf(stderr, "[chat_command] usage: .friend add <nick>\n"); fflush(stderr);
			return true;
		}
		flaway::modules::friend_manager::add(arg);
		fprintf(stderr, "[chat_command] Friend added: %s\n", arg.c_str()); fflush(stderr);
		return true;
	}
	else if (sub_cmd == "remove" || sub_cmd == "del" || sub_cmd == "delete")
	{
		if (arg.empty())
		{
			fprintf(stderr, "[chat_command] usage: .friend remove <nick>\n"); fflush(stderr);
			return true;
		}
		flaway::modules::friend_manager::remove(arg);
		fprintf(stderr, "[chat_command] Friend removed: %s\n", arg.c_str()); fflush(stderr);
		return true;
	}
	else if (sub_cmd == "list")
	{
		const auto friends = flaway::modules::friend_manager::get_list();
		fprintf(stderr, "[chat_command] Friends list (%zu):\n", friends.size()); fflush(stderr);
		for (const auto& f : friends)
		{
			// `f` is only used by the (no-op under no_log.h) fprintf below.
			(void)f.c_str();
			fprintf(stderr, "[chat_command]   - %s\n", f.c_str()); fflush(stderr);
		}
		return true;
	}
	else if (sub_cmd == "clear")
	{
		flaway::modules::friend_manager::clear();
		fprintf(stderr, "[chat_command] Friends list cleared\n"); fflush(stderr);
		return true;
	}

	fprintf(stderr, "[chat_command] unknown subcommand: %s\n", sub_cmd.c_str()); fflush(stderr);
	return true;
}

// ORIG_send_message belongs to the COPY of ChatScreen that jnihook creates
// (net/minecraft/class_408_<uuid>), NOT to the real ChatScreen that `thiz` is
// an instance of. A virtual call (CallVoidMethod) resolves it against
// thiz's class, which lands back on the hooked native ChatScreen.sendMessage
// -> infinite recursion -> StackOverflowError -> the pending exception was
// then cleared, so the chat screen just closed and the message was dropped.
// CallNonvirtualVoidMethod invokes the methodID itself (HotSpot
// jni_invoke_nonstatic: `if (call_type != JNI_VIRTUAL) selected_method = m;`),
// which runs the original bytecode with the real instance as `this`.
static void call_original_send_message(JNIEnv* env, jobject thiz, jstring chatText, jboolean addToHistory)
{
	if (!env || !thiz || !ORIG_send_message || !g_chat_screen_class)
		return;

	env->CallNonvirtualVoidMethod(thiz, g_chat_screen_class, ORIG_send_message,
		chatText, addToHistory);
	if (env->ExceptionCheck())
	{
		// Never swallow this silently again: an exception here used to make
		// the outgoing message vanish without a trace.
		fprintf(stderr, "[chat_command] exception while forwarding sendMessage:\n");
		fflush(stderr);
		env->ExceptionDescribe();
		env->ExceptionClear();
	}
}

void hkSendMessage(JNIEnv* env, jobject thiz, jstring chatText, jboolean addToHistory)
{
	if (!thiz || !ORIG_send_message || !g_chat_screen_class)
		return;

	if (g_hooked && chatText)
	{
		const char* utf = env->GetStringUTFChars(chatText, nullptr);
		if (utf)
		{
			std::string msg(utf);
			env->ReleaseStringUTFChars(chatText, utf);

			if (!msg.empty() && msg[0] == '.')
			{
				if (handle_friend_command(msg))
				{
					return;
				}
			}
		}
	}

	call_original_send_message(env, thiz, chatText, addToHistory);
}

bool flaway::modules::chat_command::init()
{
	if (g_hooked) return true;

	if (!flaway::instance) return false;
	auto env = flaway::instance->get_env();
	auto jvm = flaway::instance->get_java_vm();
	if (!env || !jvm) return false;

	// JNIHook_Init is idempotent; the refcount is incremented only on the
	// success path below so a failing init() cannot inflate it every frame.
	if (jnihook_refcount == 0)
	{
		jnihook_result_t result = JNIHook_Init(jvm);
		if (result != JNIHOOK_OK)
		{
			fprintf(stderr, "[chat_command] JNIHook_Init failed: %d\n", result); fflush(stderr);
			return false;
		}
	}

 jclass chat_screen_class = sdk::classloader::find_class(env, sdk::mappings::chat_screen_class_sig);
	if (!chat_screen_class)
	{
		fprintf(stderr, "[chat_command] failed to find chat screen class %s\n",
			sdk::mappings::chat_screen_class_sig); fflush(stderr);
		return false;
	}

	jmethodID method_id = env->GetMethodID(chat_screen_class,
		sdk::mappings::chat_screen_send_message_name,
		sdk::mappings::chat_screen_send_message_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();

	if (!method_id)
	{
		fprintf(stderr, "[chat_command] failed to find sendMessage method\n"); fflush(stderr);
		env->DeleteLocalRef(chat_screen_class);
		return false;
	}

	// Create the global ref BEFORE the hook goes live so hkSendMessage can
	// never observe a partially-initialised state.
	g_chat_screen_class = reinterpret_cast<jclass>(env->NewGlobalRef(chat_screen_class));
	if (!g_chat_screen_class)
	{
		fprintf(stderr, "[chat_command] NewGlobalRef(chat_screen_class) failed\n"); fflush(stderr);
		env->DeleteLocalRef(chat_screen_class);
		return false;
	}

	jnihook_result_t result = JNIHook_Attach(method_id, reinterpret_cast<void*>(hkSendMessage), &ORIG_send_message);
	if (result != JNIHOOK_OK || !ORIG_send_message)
	{
		fprintf(stderr, "[chat_command] JNIHook_Attach failed: %d orig=%p\n",
			result, static_cast<void*>(ORIG_send_message)); fflush(stderr);
		env->DeleteGlobalRef(g_chat_screen_class);
		g_chat_screen_class = nullptr;
		env->DeleteLocalRef(chat_screen_class);
		return false;
	}

	env->DeleteLocalRef(chat_screen_class);

	g_hooked = true;
	jnihook_refcount++;
	fprintf(stderr, "[chat_command] ChatScreen.sendMessage hook installed (orig=%p)\n",
		static_cast<void*>(ORIG_send_message)); fflush(stderr);
	return true;
}

void flaway::modules::chat_command::shutdown()
{
	g_hooked = false;
}
