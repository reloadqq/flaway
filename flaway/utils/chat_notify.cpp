#include "chat_notify.h"

#include "../flaway.h"
#include <sdk/classloader.h>
#include <sdk/minecraft/minecraft.h>
#include <sdk/mappings/mappings.hpp>

#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// This TU must stay free of non-constexpr static objects: the .so is loaded
// via dlopen and its constructors run in an order we do not control (see the
// comment at the top of utils/logger.cpp).

namespace chat_notify
{
void add(const std::string& line)
{
	if (line.empty()) return;
	if (!flaway::instance || !sdk::instance) return;

	// Text.literal() does NOT parse § color codes — they would show as
	// literal "§a" in chat. Strip every §X pair before building the Text.
	std::string clean;
	clean.reserve(line.size());
	for (size_t i = 0; i < line.size(); i++)
	{
		if (line[i] == '\xC2' && i + 1 < line.size() && line[i + 1] == '\xA7')
		{
			i++; // skip the §
			if (i + 1 < line.size()) i++; // skip the code char
			continue;
		}
		clean += line[i];
	}
	if (clean.empty()) return;

	JNIEnv* env = flaway::instance->get_env();
	if (!env) return;

	jobject mc = sdk::instance->get_minecraft();
	if (!mc) return;

	jclass mc_cls = sdk::classloader::find_class(env, sdk::mappings::minecraftclass_sig);
	if (!mc_cls) { env->DeleteLocalRef(mc); return; }

	jfieldID hud_fid = env->GetFieldID(mc_cls,
		sdk::mappings::minecraftclient_ingamehud_field,
		sdk::mappings::minecraftclient_ingamehud_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();
	env->DeleteLocalRef(mc_cls);
	if (!hud_fid) { env->DeleteLocalRef(mc); return; }

	jobject hud = env->GetObjectField(mc, hud_fid);
	if (env->ExceptionCheck()) { env->ExceptionClear(); hud = nullptr; }
	env->DeleteLocalRef(mc);
	if (!hud) return;

	jclass hud_cls = sdk::classloader::find_class(env, sdk::mappings::ingamehud_class_sig);
	jobject chat = nullptr;
	jmethodID get_chat_mid = nullptr;
	if (hud_cls)
	{
		get_chat_mid = env->GetMethodID(hud_cls,
			sdk::mappings::ingamehud_get_chat_hud_name,
			sdk::mappings::ingamehud_get_chat_hud_sig);
		if (env->ExceptionCheck()) { env->ExceptionClear(); get_chat_mid = nullptr; }
		if (get_chat_mid)
		{
			chat = env->CallObjectMethod(hud, get_chat_mid);
			if (env->ExceptionCheck()) { env->ExceptionClear(); chat = nullptr; }
		}
		env->DeleteLocalRef(hud_cls);
	}
	env->DeleteLocalRef(hud);
	if (!chat || !get_chat_mid) { if (chat) env->DeleteLocalRef(chat); return; }

	jclass chat_cls = sdk::classloader::find_class(env, sdk::mappings::chat_hud_class_sig);
	jmethodID add_mid = nullptr;
	if (chat_cls)
	{
		add_mid = env->GetMethodID(chat_cls,
			sdk::mappings::chat_hud_add_message_name,
			sdk::mappings::chat_hud_add_message_sig);
		if (env->ExceptionCheck()) { env->ExceptionClear(); add_mid = nullptr; }
		env->DeleteLocalRef(chat_cls);
	}

	jclass text_cls = sdk::classloader::find_class(env, sdk::mappings::text_class_sig);
	jmethodID literal_mid = nullptr;
	if (text_cls)
	{
		literal_mid = env->GetStaticMethodID(text_cls,
			sdk::mappings::text_literal_name,
			sdk::mappings::text_literal_sig);
		if (env->ExceptionCheck()) { env->ExceptionClear(); literal_mid = nullptr; }
	}

	jstring jline = env->NewStringUTF(line.c_str());
	jobject text = nullptr;
	if (jline && text_cls && literal_mid)
	{
		text = env->CallStaticObjectMethod(text_cls, literal_mid, jline);
		if (env->ExceptionCheck()) { env->ExceptionClear(); text = nullptr; }
	}
	if (jline) env->DeleteLocalRef(jline);
	if (text_cls) env->DeleteLocalRef(text_cls);

	if (text && add_mid)
	{
		env->CallVoidMethod(chat, add_mid, text);
		if (env->ExceptionCheck())
		{
			// A failed notification must not leave a pending exception behind:
			// it would surface on the next JNI call on this thread.
			env->ExceptionClear();
			add_mid = nullptr;
		}
	}
	if (text) env->DeleteLocalRef(text);
	env->DeleteLocalRef(chat);
}

void clear_flaway_messages()
{
	if (!flaway::instance || !sdk::instance) return;
	JNIEnv* env = flaway::instance->get_env();
	if (!env) return;

	jobject mc = sdk::instance->get_minecraft();
	if (!mc) return;

	jclass mc_cls = sdk::classloader::find_class(env, sdk::mappings::minecraftclass_sig);
	if (!mc_cls) { env->DeleteLocalRef(mc); return; }

	jfieldID hud_fid = env->GetFieldID(mc_cls,
		sdk::mappings::minecraftclient_ingamehud_field,
		sdk::mappings::minecraftclient_ingamehud_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();
	env->DeleteLocalRef(mc_cls);
	if (!hud_fid) { env->DeleteLocalRef(mc); return; }

	jobject hud = env->GetObjectField(mc, hud_fid);
	if (env->ExceptionCheck()) { env->ExceptionClear(); hud = nullptr; }
	env->DeleteLocalRef(mc);
	if (!hud) return;

	jclass hud_cls = sdk::classloader::find_class(env, sdk::mappings::ingamehud_class_sig);
	jobject chat = nullptr;
	if (hud_cls)
	{
		jmethodID get_chat_mid = env->GetMethodID(hud_cls,
			sdk::mappings::ingamehud_get_chat_hud_name,
			sdk::mappings::ingamehud_get_chat_hud_sig);
		if (env->ExceptionCheck()) { env->ExceptionClear(); get_chat_mid = nullptr; }
		if (get_chat_mid)
		{
			chat = env->CallObjectMethod(hud, get_chat_mid);
			if (env->ExceptionCheck()) { env->ExceptionClear(); chat = nullptr; }
		}
		env->DeleteLocalRef(hud_cls);
	}
	env->DeleteLocalRef(hud);
	if (!chat) return;

	jclass chat_cls = sdk::classloader::find_class(env, sdk::mappings::chat_hud_class_sig);
	if (!chat_cls) { env->DeleteLocalRef(chat); return; }

	jfieldID msgs_fid = env->GetFieldID(chat_cls,
		sdk::mappings::chat_hud_messages_name,
		sdk::mappings::chat_hud_messages_sig);
	if (env->ExceptionCheck()) { env->ExceptionClear(); msgs_fid = nullptr; }
	env->DeleteLocalRef(chat_cls);
	if (!msgs_fid) { env->DeleteLocalRef(chat); return; }

	jobject msgs = env->GetObjectField(chat, msgs_fid);
	if (env->ExceptionCheck()) { env->ExceptionClear(); msgs = nullptr; }
	env->DeleteLocalRef(chat);
	if (!msgs) return;

	jclass list_cls = env->GetObjectClass(msgs);
	if (!list_cls) { env->DeleteLocalRef(msgs); return; }

	jmethodID size_mid = env->GetMethodID(list_cls, "size", "()I");
	jmethodID get_mid = env->GetMethodID(list_cls, "get", "(I)Ljava/lang/Object;");
	jmethodID remove_mid = env->GetMethodID(list_cls, "remove", "(I)Ljava/lang/Object;");
	if (env->ExceptionCheck()) { env->ExceptionClear(); size_mid = get_mid = remove_mid = nullptr; }
	env->DeleteLocalRef(list_cls);
	if (!size_mid || !get_mid || !remove_mid) { env->DeleteLocalRef(msgs); return; }

	// ChatHudLine.content is a Text; getString() renders it to a C string.
	jclass line_cls = sdk::classloader::find_class(env, sdk::mappings::chat_hud_line_class_sig);
	jfieldID content_fid = nullptr;
	if (line_cls)
	{
		content_fid = env->GetFieldID(line_cls,
			sdk::mappings::chat_hud_line_content_name, "Lnet/minecraft/class_2561;");
		if (env->ExceptionCheck()) { env->ExceptionClear(); content_fid = nullptr; }
		env->DeleteLocalRef(line_cls);
	}
	jclass text_cls = sdk::classloader::find_class(env, sdk::mappings::text_class_sig);
	jmethodID str_mid = nullptr;
	if (text_cls)
	{
		str_mid = env->GetMethodID(text_cls,
			sdk::mappings::text_get_string_name,
			sdk::mappings::text_get_string_sig);
		if (env->ExceptionCheck()) { env->ExceptionClear(); str_mid = nullptr; }
		env->DeleteLocalRef(text_cls);
	}

	// Iterate backwards so remove() indices stay valid.
	jint n = env->CallIntMethod(msgs, size_mid);
	if (env->ExceptionCheck()) { env->ExceptionClear(); n = 0; }
	for (jint i = n - 1; i >= 0; i--)
	{
		jobject line = env->CallObjectMethod(msgs, get_mid, i);
		if (env->ExceptionCheck()) { env->ExceptionClear(); line = nullptr; }
		if (!line) continue;

		bool match = false;
		if (content_fid && str_mid)
		{
			jobject content = env->GetObjectField(line, content_fid);
			if (env->ExceptionCheck()) { env->ExceptionClear(); content = nullptr; }
			if (content)
			{
				jstring s = (jstring)env->CallObjectMethod(content, str_mid, 0x7FFFFFFF);
				if (env->ExceptionCheck()) { env->ExceptionClear(); s = nullptr; }
				if (s)
				{
					const char* utf = env->GetStringUTFChars(s, nullptr);
					if (utf)
					{
						if (strstr(utf, "[flaway]") || strstr(utf, "[Flway]"))
							match = true;
						env->ReleaseStringUTFChars(s, utf);
					}
					env->DeleteLocalRef(s);
				}
				env->DeleteLocalRef(content);
			}
		}
		env->DeleteLocalRef(line);

		if (match)
		{
			jobject removed = env->CallObjectMethod(msgs, remove_mid, i);
			if (env->ExceptionCheck()) env->ExceptionClear();
			if (removed) env->DeleteLocalRef(removed);
		}
	}
	env->DeleteLocalRef(msgs);
}

bool append_file(const std::string& file_name, const std::string& line)
{
	// Only a plain file name inside ~/.minecraft — never let a caller turn
	// this into an arbitrary path write.
	if (file_name.empty()) return false;
	if (file_name.find('/') != std::string::npos) return false;
	if (file_name.find('\\') != std::string::npos) return false;
	if (file_name.find("..") != std::string::npos) return false;

	std::string out = line;
	if (out.empty() || out.back() != '\n') out.push_back('\n');

	char path[512];
	int fd = -1;
	const char* home = getenv("HOME");
	if (home)
	{
		snprintf(path, sizeof(path), "%s/.minecraft/%s", home, file_name.c_str());
		fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
	}
	if (fd < 0)
	{
		snprintf(path, sizeof(path), "/tmp/%s", file_name.c_str());
		fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
	}
	if (fd < 0) return false;

	size_t done = 0;
	while (done < out.size())
	{
		ssize_t w = write(fd, out.data() + done, out.size() - done);
		if (w <= 0) break;
		done += (size_t)w;
	}
	close(fd);
	return done == out.size();
}

} // namespace chat_notify
