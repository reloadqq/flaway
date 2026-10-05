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
