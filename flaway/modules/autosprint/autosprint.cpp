#include "autosprint.h"
#include "../../flaway.h"
#include "../../globals/globals.h"
#include "../../utils/logger.h"
#include <sdk/minecraft/minecraft.h>
#include <sdk/minecraft/entity/entity.h>
#include <sdk/mappings/mappings.hpp>
#include <sdk/classloader.h>

namespace flaway
{
	namespace modules
	{
		void autosprint::run()
		{
			if (!globals::sprint_enabled) return;

			auto env = flaway::instance->get_env();
			if (!env) return;

			jobject player = sdk::instance->get_player();
			if (!player) return;

			jclass ent_cls = sdk::classloader::find_class(env, sdk::mappings::entity_class_sig);
			if (!ent_cls)
			{
				env->DeleteLocalRef(player);
				return;
			}

			// Check if player is on ground
			jmethodID is_on_ground_mid = env->GetMethodID(ent_cls,
				sdk::mappings::is_on_ground_name, sdk::mappings::is_on_ground_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			bool on_ground = false;
			if (is_on_ground_mid)
			{
				jboolean og = env->CallBooleanMethod(player, is_on_ground_mid);
				if (env->ExceptionCheck()) env->ExceptionClear();
				on_ground = (og == JNI_TRUE);
			}

			// Check if player is moving forward (W held)
			bool w_held = (GetAsyncKeyState('W') & 0x8000) != 0;
			if (!w_held)
			{
				env->DeleteLocalRef(ent_cls);
				env->DeleteLocalRef(player);
				return;
			}

			// Only sprint on ground to avoid Grim SprintG detection
			// (holding Ctrl airborne causes vanilla to send sprinting=true during falls)
			if (!on_ground)
			{
				env->DeleteLocalRef(ent_cls);
				env->DeleteLocalRef(player);
				return;
			}

			// Check if already sprinting
			jmethodID is_sprinting_mid = env->GetMethodID(ent_cls,
				sdk::mappings::is_sprinting_name, sdk::mappings::is_sprinting_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			if (is_sprinting_mid)
			{
				jboolean sprinting = env->CallBooleanMethod(player, is_sprinting_mid);
				if (env->ExceptionCheck()) env->ExceptionClear();
				if (sprinting == JNI_TRUE)
				{
					// Already sprinting, nothing to do
					env->DeleteLocalRef(ent_cls);
					env->DeleteLocalRef(player);
					return;
				}
			}

			// Force sprint on
			jmethodID set_sprinting_mid = env->GetMethodID(ent_cls,
				sdk::mappings::set_sprinting_name, sdk::mappings::set_sprinting_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			if (set_sprinting_mid)
			{
				env->CallVoidMethod(player, set_sprinting_mid, JNI_TRUE);
				if (env->ExceptionCheck()) env->ExceptionClear();
			}

			env->DeleteLocalRef(ent_cls);
			env->DeleteLocalRef(player);
		}

		void autosprint::cleanup()
		{
		}
	}
}
