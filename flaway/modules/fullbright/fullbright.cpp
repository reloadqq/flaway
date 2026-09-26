#include "fullbright.h"
#include "../../flaway.h"
#include "../../globals/globals.h"
#include "../../utils/logger.h"
#include <sdk/minecraft/minecraft.h>
#include <sdk/classloader.h>
#include <sdk/mappings/mappings.hpp>
#include <cmath>

namespace flaway
{
	namespace modules
	{
		namespace fullbright
		{
			// Cached JNI handles so we only resolve classes/fields once instead
			// of every frame. gamma_fid/value_fid are valid for the lifetime of
			// their classes; classes are cached globally by the classloader.
			static jfieldID g_options_fid = nullptr;
			static jfieldID g_gamma_fid = nullptr;
			static jfieldID g_value_fid = nullptr;
			static jclass g_double_class = nullptr;   // global ref
			static jmethodID g_double_ctor = nullptr;
			static bool g_lookup_done = false;

			// Last value actually written + last enabled state, so run() can
			// skip all JNI once the gamma option matches what we want (the
			// common steady-state case: module on, slider untouched).
			static double g_last_written_gamma = -1.0;
			static bool g_last_enabled = false;

			static void release_cache()
			{
				auto env = flaway::instance ? flaway::instance->get_env() : nullptr;
				if (env && g_double_class)
				{
					if (env->functions) env->DeleteGlobalRef(g_double_class);
				}
				g_double_class = nullptr;
				g_options_fid = nullptr;
				g_gamma_fid = nullptr;
				g_value_fid = nullptr;
				g_double_ctor = nullptr;
				g_lookup_done = false;
				g_last_written_gamma = -1.0;
				g_last_enabled = false;
			}

			// Resolve and cache all field/method ids used to write gamma.
			// Returns true if the whole chain resolved (so the caller can
			// proceed to apply the value).
			static bool ensure_lookup(JNIEnv* env)
			{
				if (g_lookup_done) return g_options_fid && g_gamma_fid && g_value_fid && g_double_class && g_double_ctor;

				jclass minecraft_class = sdk::classloader::find_class(env, sdk::mappings::minecraftclass_sig);
				if (!minecraft_class) return false;

				g_options_fid = env->GetFieldID(minecraft_class, sdk::mappings::game_options_name, sdk::mappings::game_options_sig);
				if (env->ExceptionCheck()) env->ExceptionClear();
				env->DeleteLocalRef(minecraft_class);
				if (!g_options_fid) return false;

				jclass options_class = sdk::classloader::find_class(env, sdk::mappings::game_options_class_sig);
				if (!options_class) return false;
				g_gamma_fid = env->GetFieldID(options_class, sdk::mappings::game_options_gamma_name, sdk::mappings::game_options_gamma_sig);
				if (env->ExceptionCheck()) env->ExceptionClear();
				env->DeleteLocalRef(options_class);
				if (!g_gamma_fid) return false;

				jclass simple_option_class = sdk::classloader::find_class(env, sdk::mappings::simple_option_class_sig);
				if (!simple_option_class) return false;
				g_value_fid = env->GetFieldID(simple_option_class, sdk::mappings::simple_option_value_name, sdk::mappings::simple_option_value_sig);
				if (env->ExceptionCheck()) env->ExceptionClear();
				env->DeleteLocalRef(simple_option_class);
				if (!g_value_fid) return false;

				jclass double_class = env->FindClass("java/lang/Double");
				if (env->ExceptionCheck()) env->ExceptionClear();
				if (!double_class) return false;
				g_double_ctor = env->GetMethodID(double_class, "<init>", "(D)V");
				if (env->ExceptionCheck()) env->ExceptionClear();
				if (!g_double_ctor)
				{
					env->DeleteLocalRef(double_class);
					return false;
				}
				g_double_class = reinterpret_cast<jclass>(env->NewGlobalRef(double_class));
				env->DeleteLocalRef(double_class);
				if (!g_double_class)
				{
					g_double_ctor = nullptr;
					return false;
				}

				g_lookup_done = true;
				return true;
			}

			void run()
			{
				if (!globals::fullbright_enabled)
				{
					g_last_enabled = false;
					return;
				}

				double gamma = globals::fullbright_gamma;
				if (gamma > 10.0) gamma = 10.0;
				if (gamma < 0.1) gamma = 0.1;

				// Steady state: value already applied and module stays enabled,
				// so there is no per-frame JNI at all. Re-write on (re)enable to
				// cover the case where the game reloaded its options object.
				bool just_enabled = !g_last_enabled;
				g_last_enabled = true;
				if (!just_enabled && gamma == g_last_written_gamma)
					return;

				auto env = flaway::instance->get_env();
				if (!env) return;

				if (!ensure_lookup(env)) return;

				// Re-fetch the live objects each frame (they can be replaced on
				// world/settings changes), but the field ids stay cached.
				jobject minecraft = sdk::instance->get_minecraft();
				if (!minecraft) return;

				jobject options = env->GetObjectField(minecraft, g_options_fid);
				if (env->ExceptionCheck()) { env->ExceptionClear(); options = nullptr; }
				if (!options)
				{
					env->DeleteLocalRef(minecraft);
					return;
				}

				jobject gamma_option = env->GetObjectField(options, g_gamma_fid);
				if (env->ExceptionCheck()) { env->ExceptionClear(); gamma_option = nullptr; }
				if (!gamma_option)
				{
					env->DeleteLocalRef(options);
					env->DeleteLocalRef(minecraft);
					return;
				}

				jobject value = env->NewObject(g_double_class, g_double_ctor, gamma);
				if (env->ExceptionCheck()) { env->ExceptionClear(); value = nullptr; }
				if (value)
				{
					env->SetObjectField(gamma_option, g_value_fid, value);
					if (env->ExceptionCheck()) env->ExceptionClear();
					env->DeleteLocalRef(value);
					g_last_written_gamma = gamma;
				}

				env->DeleteLocalRef(gamma_option);
				env->DeleteLocalRef(options);
				env->DeleteLocalRef(minecraft);
			}

			void cleanup()
			{
				release_cache();
			}
		}
	}
}
