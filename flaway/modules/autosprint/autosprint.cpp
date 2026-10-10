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
		namespace
		{
			bool was_swimming = false;
			// Cached JNI ids — resolved once, reused every frame.
			jmethodID s_mid_set_sprint = nullptr;
			jmethodID s_mid_is_sprint = nullptr;
			jmethodID s_mid_set_swim = nullptr;
			jmethodID s_mid_in_water = nullptr;
			bool s_mids_resolved = false;
			jclass s_ent_cls = nullptr;

			void resolve_mids(JNIEnv* env)
			{
				if (s_mids_resolved) return;
				s_ent_cls = reinterpret_cast<jclass>(
					env->NewGlobalRef(sdk::classloader::find_class(env,
						sdk::mappings::entity_class_sig)));
				if (!s_ent_cls) return;
				s_mid_set_sprint = env->GetMethodID(s_ent_cls,
					sdk::mappings::set_sprinting_name,
					sdk::mappings::set_sprinting_sig);
				if (env->ExceptionCheck()) { env->ExceptionClear(); s_mid_set_sprint = nullptr; }
				s_mid_is_sprint = env->GetMethodID(s_ent_cls,
					sdk::mappings::is_sprinting_name,
					sdk::mappings::is_sprinting_sig);
				if (env->ExceptionCheck()) { env->ExceptionClear(); s_mid_is_sprint = nullptr; }
				s_mid_set_swim = env->GetMethodID(s_ent_cls,
					sdk::mappings::set_swimming_name,
					sdk::mappings::set_swimming_sig);
				if (env->ExceptionCheck()) { env->ExceptionClear(); s_mid_set_swim = nullptr; }
				s_mid_in_water = env->GetMethodID(s_ent_cls,
					sdk::mappings::is_touching_water_name,
					sdk::mappings::is_touching_water_sig);
				if (env->ExceptionCheck()) { env->ExceptionClear(); s_mid_in_water = nullptr; }
				s_mids_resolved = s_mid_set_sprint != nullptr;
			}
		}

		void autosprint::run()
		{
			auto env = flaway::instance->get_env();
			if (!env) return;

			if (!globals::sprint_enabled)
			{
				if (was_swimming)
				{
					jobject player = sdk::instance->get_player();
					if (player && s_mid_set_swim)
					{
						env->CallVoidMethod(player, s_mid_set_swim, JNI_FALSE);
						if (env->ExceptionCheck()) env->ExceptionClear();
					}
					if (player) env->DeleteLocalRef(player);
					was_swimming = false;
				}
				return;
			}

			resolve_mids(env);
			if (!s_mids_resolved) return;

			jobject player = sdk::instance->get_player();
			if (!player) return;

			sdk::entity_client ec(player);

			// Only touch the sprint flag when it is actually off — calling
			// setSprinting every frame would spam JNI and fight vanilla's own
			// sprint state machine (lag / freeze).
			if (ec.is_on_ground())
			{
				bool sprinting = false;
				if (s_mid_is_sprint)
				{
					sprinting = env->CallBooleanMethod(player, s_mid_is_sprint) == JNI_TRUE;
					if (env->ExceptionCheck()) { env->ExceptionClear(); sprinting = true; }
				}
				if (!sprinting && s_mid_set_sprint)
				{
					env->CallVoidMethod(player, s_mid_set_sprint, JNI_TRUE);
					if (env->ExceptionCheck()) env->ExceptionClear();
				}
			}

			if (globals::autosprint_keep_swimming)
			{
				bool wet = false;
				if (s_mid_in_water)
				{
					wet = env->CallBooleanMethod(player, s_mid_in_water) == JNI_TRUE;
					if (env->ExceptionCheck()) { env->ExceptionClear(); wet = false; }
				}
				if (wet && s_mid_set_swim)
				{
					env->CallVoidMethod(player, s_mid_set_swim, JNI_TRUE);
					if (env->ExceptionCheck()) env->ExceptionClear();
					was_swimming = true;
				}
				else if (was_swimming && s_mid_set_swim)
				{
					env->CallVoidMethod(player, s_mid_set_swim, JNI_FALSE);
					if (env->ExceptionCheck()) env->ExceptionClear();
					was_swimming = false;
				}
			}
			else if (was_swimming && s_mid_set_swim)
			{
				env->CallVoidMethod(player, s_mid_set_swim, JNI_FALSE);
				if (env->ExceptionCheck()) env->ExceptionClear();
				was_swimming = false;
			}

			env->DeleteLocalRef(player);
		}

		void autosprint::cleanup()
		{
			// Best-effort: clear sprint if we set it.
			auto env = flaway::instance ? flaway::instance->get_env() : nullptr;
			if (env && s_mid_set_sprint)
			{
				jobject p = sdk::instance ? sdk::instance->get_player() : nullptr;
				if (p)
				{
					env->CallVoidMethod(p, s_mid_set_sprint, JNI_FALSE);
					if (env->ExceptionCheck()) env->ExceptionClear();
					env->DeleteLocalRef(p);
				}
			}
			was_swimming = false;
		}
	}
}
