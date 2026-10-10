#include "fog.h"
#include "../../flaway.h"
#include "../../hooks/Hook.h"
#include "../../globals/globals.h"
#include "../../utils/rlog.h"
#include <sdk/mappings/mappings.hpp>
#include <sdk/classloader.h>
#include <atomic>

namespace flaway
{
	namespace modules
	{
		namespace fog
		{
			namespace
			{
				enum class ApplyKind { Off, Record, Ubo };

				jclass g_fog_cls = nullptr;
				jmethodID g_m_get_color = nullptr;
				jmethodID g_o_get_color = nullptr;
				bool g_get_color_static = false;
				ApplyKind g_apply_kind = ApplyKind::Off;
				jmethodID g_o_apply_record = nullptr;
				jmethodID g_o_apply_ubo = nullptr;
				bool g_apply_static = false;
				jfieldID g_fx = nullptr;
				jfieldID g_fy = nullptr;
				jfieldID g_fz = nullptr;
				jweak g_tinted = nullptr;
				int jnihook_refcount = 0;
				std::atomic<bool> g_ready{false};

				// Blend the vanilla fog colour towards the user's colour. Guarded by
				// object identity so one Vector4f flowing from getFogColor() into
				// applyFog() is tinted exactly once per frame.
				void tint(JNIEnv* env, jobject vec)
				{
					if (!vec || !g_fx || !g_fy || !g_fz) return;
					// Unhook flips this FIRST, before any teardown wait, so the
					// game's very next frame renders vanilla fog. The FogRenderer
					// methods stay bound to us after a gate-only unload, so this
					// is what actually turns the tint off.
					if (Hook::get_unhooked()) return;
					if (!globals::fog_enabled) return;
					float k = globals::fog_strength / 100.0f;
					if (k < 0.0f) k = 0.0f;
					if (k > 1.0f) k = 1.0f;
					if (k <= 0.001f) return;
					if (g_tinted && env->IsSameObject(vec, g_tinted)) return;

					float target[3] = {
						globals::fog_color[0],
						globals::fog_color[1],
						globals::fog_color[2]
					};
					float cur[3];
					cur[0] = env->GetFloatField(vec, g_fx);
					cur[1] = env->GetFloatField(vec, g_fy);
					cur[2] = env->GetFloatField(vec, g_fz);
					if (env->ExceptionCheck())
					{
						env->ExceptionClear();
						return;
					}
					env->SetFloatField(vec, g_fx, cur[0] + (target[0] - cur[0]) * k);
					env->SetFloatField(vec, g_fy, cur[1] + (target[1] - cur[1]) * k);
					env->SetFloatField(vec, g_fz, cur[2] + (target[2] - cur[2]) * k);
					if (env->ExceptionCheck())
					{
						env->ExceptionClear();
						return;
					}
					if (g_tinted) env->DeleteWeakGlobalRef(g_tinted);
					g_tinted = env->NewWeakGlobalRef(vec);
				}

				// 1.21.4 BackgroundRenderer.getFogColor(Camera, float, World, int, float)
				jobject hkGetFogColor(JNIEnv* env, jobject thiz, jobject camera,
					jfloat tick_progress, jobject world, jint view_distance,
					jfloat sky_darkness)
				{
					jobject ret = nullptr;
					if (g_get_color_static)
						ret = env->CallStaticObjectMethod(g_fog_cls, g_o_get_color,
							camera, tick_progress, world, view_distance, sky_darkness);
					else
						ret = env->CallNonvirtualObjectMethod(thiz, g_fog_cls, g_o_get_color,
							camera, tick_progress, world, view_distance, sky_darkness);
					if (env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }
					if (ret) tint(env, ret);
					return ret;
				}

				// 1.21.10+ FogRenderer.getFogColor(..., boolean thick)
				jobject hkGetFogColorLegacy(JNIEnv* env, jobject thiz, jobject camera,
					jfloat tick_progress, jobject world, jint view_distance,
					jfloat sky_darkness, jboolean thick)
				{
					jobject ret = nullptr;
					if (g_get_color_static)
						ret = env->CallStaticObjectMethod(g_fog_cls, g_o_get_color,
							camera, tick_progress, world, view_distance, sky_darkness, thick);
					else
						ret = env->CallNonvirtualObjectMethod(thiz, g_fog_cls, g_o_get_color,
							camera, tick_progress, world, view_distance, sky_darkness, thick);
					if (env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }
					if (ret) tint(env, ret);
					return ret;
				}

				// 1.21.4 BackgroundRenderer.applyFog(Camera, FogType, Vector4f, float, boolean, float)
				// The colour argument is baked into the returned Fog record.
				jobject hkApplyFogRecord(JNIEnv* env, jobject thiz, jobject camera,
					jobject fog_type, jobject fog_color, jfloat view_distance,
					jboolean thick, jfloat tick_progress)
				{
					if (fog_color) tint(env, fog_color);
					jobject ret = nullptr;
					if (g_apply_static)
						ret = env->CallStaticObjectMethod(g_fog_cls, g_o_apply_record, camera,
							fog_type, fog_color, view_distance, thick, tick_progress);
					else
						ret = env->CallNonvirtualObjectMethod(thiz, g_fog_cls, g_o_apply_record,
							camera, fog_type, fog_color, view_distance, thick, tick_progress);
					if (env->ExceptionCheck()) env->ExceptionClear();
					return ret;
				}

				// 1.21.10 FogRenderer.applyFog(ByteBuffer, int, Vector4f, float x6)
				void hkApplyFogUbo(JNIEnv* env, jobject thiz, jobject buffer, jint buf_pos,
					jobject fog_color, jfloat render_distance_start, jfloat environmental_start,
					jfloat environmental_end, jfloat render_distance_end, jfloat sky_end,
					jfloat cloud_end)
				{
					if (fog_color) tint(env, fog_color);
					if (g_apply_static)
						env->CallStaticVoidMethod(g_fog_cls, g_o_apply_ubo, buffer, buf_pos,
							fog_color, render_distance_start, environmental_start, environmental_end,
							render_distance_end, sky_end, cloud_end);
					else
						env->CallNonvirtualVoidMethod(thiz, g_fog_cls, g_o_apply_ubo, buffer, buf_pos,
							fog_color, render_distance_start, environmental_start, environmental_end,
							render_distance_end, sky_end, cloud_end);
					if (env->ExceptionCheck()) env->ExceptionClear();
				}

				jmethodID find_method(JNIEnv* env, jclass cls, const char* name,
					const char* sig, bool* is_static)
				{
					jmethodID m = env->GetStaticMethodID(cls, name, sig);
					if (env->ExceptionCheck()) env->ExceptionClear();
					if (m) { *is_static = true; return m; }
					m = env->GetMethodID(cls, name, sig);
					if (env->ExceptionCheck()) env->ExceptionClear();
					if (m) *is_static = false;
					return m;
				}

				bool init_hook()
				{
					if (g_ready.load(std::memory_order_acquire)) return true;
					if (!flaway::instance) return false;
					auto env = flaway::instance->get_env();
					auto jvm = flaway::instance->get_java_vm();
					if (!env || !jvm) return false;

					// Shared, idempotent with reach_hook / nametag_hook. Never shut it
					// down here - the central teardown owns that.
					if (jnihook_refcount == 0)
					{
						if (JNIHook_Init(jvm) != JNIHOOK_OK)
						{
							rlog::logf("fog: JNIHook_Init failed");
							return false;
						}
					}

					jclass fog_cls = sdk::classloader::find_class(env,
						sdk::mappings::fog_renderer_class_sig);
					if (!fog_cls)
					{
						rlog::logf("fog: FogRenderer class not found");
						return false;
					}

					bool attached = false;

					// --- getFogColor: 1.21.4 (5 args) first, then 1.21.10 (6 args) ---
					bool color_static = false;
					bool color_legacy = false;
					jmethodID m_color = find_method(env, fog_cls,
						sdk::mappings::fog_get_color_name, sdk::mappings::fog_get_color_sig,
						&color_static);
					if (!m_color)
					{
						color_legacy = true;
						m_color = find_method(env, fog_cls, sdk::mappings::fog_get_color_name,
							sdk::mappings::fog_get_color_sig_legacy, &color_static);
					}
					if (m_color)
					{
						void* hook = color_legacy
							? reinterpret_cast<void*>(hkGetFogColorLegacy)
							: reinterpret_cast<void*>(hkGetFogColor);
						if (JNIHook_Attach(m_color, hook, &g_o_get_color) == JNIHOOK_OK)
						{
							g_m_get_color = m_color;
							g_get_color_static = color_static;
							attached = true;
						}
						else
						{
							g_o_get_color = nullptr;
							g_m_get_color = nullptr;
						}
					}

					// --- applyFog: 1.21.4 record variant, else 1.21.10 UBO variant ---
					bool apply_static = false;
					jmethodID m_rec = find_method(env, fog_cls,
						sdk::mappings::fog_apply_record_name, sdk::mappings::fog_apply_record_sig,
						&apply_static);
					if (m_rec &&
						JNIHook_Attach(m_rec, reinterpret_cast<void*>(hkApplyFogRecord),
							&g_o_apply_record) == JNIHOOK_OK)
					{
						g_apply_static = apply_static;
						g_apply_kind = ApplyKind::Record;
						attached = true;
					}
					else
					{
						g_o_apply_record = nullptr;
						apply_static = false;
						jmethodID m_ubo = find_method(env, fog_cls,
							sdk::mappings::fog_apply_ubo_name, sdk::mappings::fog_apply_ubo_sig,
							&apply_static);
						if (m_ubo &&
							JNIHook_Attach(m_ubo, reinterpret_cast<void*>(hkApplyFogUbo),
								&g_o_apply_ubo) == JNIHOOK_OK)
						{
							g_apply_static = apply_static;
							g_apply_kind = ApplyKind::Ubo;
							attached = true;
						}
						else
						{
							g_o_apply_ubo = nullptr;
						}
					}

					jclass vec_cls = sdk::classloader::find_class(env,
						sdk::mappings::vector4f_class_sig);
					if (vec_cls)
					{
						g_fx = env->GetFieldID(vec_cls, "x", "F");
						g_fy = env->GetFieldID(vec_cls, "y", "F");
						g_fz = env->GetFieldID(vec_cls, "z", "F");
						if (env->ExceptionCheck()) env->ExceptionClear();
						env->DeleteLocalRef(vec_cls);
					}

					if (!attached)
					{
						env->DeleteLocalRef(fog_cls);
						rlog::logf("fog: no FogRenderer method could be hooked");
						return false;
					}

					g_fog_cls = reinterpret_cast<jclass>(env->NewGlobalRef(fog_cls));
					env->DeleteLocalRef(fog_cls);
					if (!g_fog_cls) return false;

					jnihook_refcount++;
					g_ready.store(true, std::memory_order_release);
					rlog::logf("fog: hooked getFogColor=%d apply=%d vec=%d",
						(int)(g_m_get_color != nullptr), (int)g_apply_kind,
						(int)(g_fx != nullptr));
					return true;
				}
			}

			void run()
			{
				if (!g_ready.load(std::memory_order_acquire)) init_hook();

				// Frame boundary for the double-tint guard: the Java render pass that
				// just finished already consumed it, the next one starts clean.
				auto env = flaway::instance ? flaway::instance->get_env() : nullptr;
				if (env && g_tinted)
				{
					env->DeleteWeakGlobalRef(g_tinted);
					g_tinted = nullptr;
				}
			}

			void draw() {}

			void cleanup() {}

			void shutdown()
			{
				// Fast off-switch: tint() already bails on Hook::get_unhooked(),
				// dropping the tint on the next frame; clearing the enabled flag
				// too keeps it off for the rest of the session (a re-inject
				// reloads fog.enabled from the saved config).
				globals::fog_enabled = false;
			}
		}
	}
}
