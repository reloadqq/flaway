#include "better_minecraft.h"
#include "bm_internal.h"
#include "menu_bg.h"
#include "../../flaway.h"
#include "../../hooks/Hook.h"
#include "../../globals/globals.h"
#include "../../utils/rlog.h"
#include <sdk/mappings/mappings.hpp>
#include <sdk/classloader.h>
#include <sdk/minecraft/minecraft.h>
#include <platform/linux/x11_helper.h>
#include <jnihook.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <unordered_map>

namespace flaway
{
	namespace modules
	{
		namespace better_minecraft
		{
			namespace detail
			{
				float zoom_state = 1.0f;

				namespace
				{
					// JNIHook state is shared with reach/nametag/fog. NEVER call
					// JNIHook_Shutdown here - only track our own init so the
					// central teardown in flaway.cpp shuts everything down once.
					int jnihook_refcount = 0;
					std::atomic<bool> g_ready{false};

					long long now_us()
					{
						return std::chrono::duration_cast<std::chrono::microseconds>(
							std::chrono::steady_clock::now().time_since_epoch()).count();
					}

					jmethodID find_instance(JNIEnv* env, jclass cls, const char* name,
						const char* sig)
					{
						jmethodID m = env->GetMethodID(cls, name, sig);
						if (env->ExceptionCheck()) { env->ExceptionClear(); m = nullptr; }
						return m;
					}

					jfieldID find_field(JNIEnv* env, jclass cls, const char* name,
						const char* sig)
					{
						jfieldID f = env->GetFieldID(cls, name, sig);
						if (env->ExceptionCheck()) { env->ExceptionClear(); f = nullptr; }
						return f;
					}

					jclass find_global(JNIEnv* env, const char* name)
					{
						jclass c = sdk::classloader::find_class(env, name);
						if (!c) return nullptr;
						jclass g = reinterpret_cast<jclass>(env->NewGlobalRef(c));
						env->DeleteLocalRef(c);
						return g;
					}

					bool attach(jmethodID target, void* hook, jmethodID* orig)
					{
						if (!target || !hook || !orig) return false;
						*orig = nullptr;
						jnihook_result_t rc = JNIHook_Attach(target, hook, orig);
						if (rc != JNIHOOK_OK)
						{
							rlog::logf("bm: jnihook fail rc=%d target=%p", (int)rc, (void*)target);
							*orig = nullptr;
							return false;
						}
						return *orig != nullptr;
					}

					// ----------------------------------------------------------
					// feature gates
					// ----------------------------------------------------------
					bool chat_anim_active()
					{
						return !Hook::get_unhooked() && globals::better_minecraft_enabled &&
							globals::bm_chat_anim;
					}

					bool items_anim_active()
					{
						return !Hook::get_unhooked() && globals::better_minecraft_enabled &&
							globals::bm_items_anim;
					}

					bool gui_anim_active()
					{
						return !Hook::get_unhooked() && globals::better_minecraft_enabled &&
							globals::bm_gui_anim;
					}

					// ----------------------------------------------------------
					// zoom / smooth F5 (unchanged)
					// ----------------------------------------------------------
					jmethodID g_o_get_fov = nullptr;
					jmethodID g_o_camera_update = nullptr;
					jclass g_renderer_cls = nullptr;
					jclass g_camera_cls = nullptr;
					bool s_got_fov = false, s_got_cam = false;

					jmethodID g_m_set_pos = nullptr;
					jmethodID g_m_clip = nullptr;
					jmethodID g_m_move_by = nullptr;
					jmethodID g_m_get_cam_pos = nullptr;
					jfieldID g_f_vec_x = nullptr;
					jfieldID g_f_vec_y = nullptr;
					jfieldID g_f_vec_z = nullptr;

					bool zoom_active()
					{
						return !Hook::get_unhooked() && globals::better_minecraft_enabled &&
							globals::bm_zoom_enabled;
					}

					bool f5_active()
					{
						return !Hook::get_unhooked() && globals::better_minecraft_enabled &&
							globals::bm_f5_enabled;
					}

					// GameRenderer.getFov(Camera, float, boolean) -> float.
					// Vanilla computes the FOV first, we divide it by the animated
					// zoom factor (1.0 = untouched) afterwards.
					jfloat hkGetFov(JNIEnv* env, jobject thiz, jobject camera,
						jfloat tick_delta, jboolean changing_fov)
					{
						jfloat ret = 0.0f;
						if (g_o_get_fov && g_renderer_cls)
						{
							ret = env->CallNonvirtualFloatMethod(thiz, g_renderer_cls,
								g_o_get_fov, camera, tick_delta, changing_fov);
							if (env->ExceptionCheck()) env->ExceptionClear();
						}
						if (zoom_active() && zoom_state > 1.0005f)
							ret = ret / zoom_state;
						return ret;
					}

					// Camera.update(World, Entity, thirdPerson, inverse, tickDelta).
					// Vanilla places the camera at the eye and, in third person,
					// pushes it back by -clipToSpace(dist). We replay exactly that
					// with an animated distance while the perspective is switching,
					// and stay out of the way once the distance settles (vanilla is
					// authoritative then - entity attributes, boat cameras, ...).
					void hkCameraUpdate(JNIEnv* env, jobject thiz, jobject world, jobject entity,
						jboolean third_person, jboolean inverse, jfloat tick_delta)
					{
						static bool s_inited = false;
						static float s_dist = 0.0f;
						static auto s_last = std::chrono::steady_clock::now();

						if (g_o_camera_update && g_camera_cls)
						{
							env->CallNonvirtualVoidMethod(thiz, g_camera_cls, g_o_camera_update,
								world, entity, third_person, inverse, tick_delta);
							if (env->ExceptionCheck()) env->ExceptionClear();
						}

						if (!f5_active() || !thiz || !entity)
						{
							// Forget the animation so re-enabling snaps to the
							// current vanilla state instead of sliding in.
							s_inited = false;
							return;
						}
						if (!g_m_set_pos || !g_m_clip || !g_m_move_by || !g_m_get_cam_pos ||
							!g_f_vec_x || !g_f_vec_y || !g_f_vec_z)
						{
							s_inited = false;
							return;
						}

						auto now = std::chrono::steady_clock::now();
						float dt = std::chrono::duration<float>(now - s_last).count();
						s_last = now;
						if (dt < 0.0f || dt > 0.25f) dt = 0.0f;

						const float target = third_person ? 4.0f : 0.0f;
						if (!s_inited)
						{
							s_dist = target;
							s_inited = true;
							return;
						}
						if (fabsf(s_dist - target) < 0.0005f)
						{
							s_dist = target;
							return;   // steady state - vanilla already placed the camera
						}

						float rate = globals::bm_f5_speed * 2.0f;
						if (rate < 0.5f) rate = 0.5f;
						s_dist += (target - s_dist) * (1.0f - expf(-rate * dt));
						if (fabsf(s_dist - target) < 0.0005f) s_dist = target;

						// Replay vanilla placement at the animated distance:
						// setPos(eye) -> moveBy(-clipToSpace(dist), 0, 0).
						jobject eye = env->CallObjectMethod(entity, g_m_get_cam_pos, tick_delta);
						if (env->ExceptionCheck()) { env->ExceptionClear(); return; }
						if (!eye) return;
						jdouble ex = env->GetDoubleField(eye, g_f_vec_x);
						jdouble ey = env->GetDoubleField(eye, g_f_vec_y);
						jdouble ez = env->GetDoubleField(eye, g_f_vec_z);
						if (env->ExceptionCheck())
						{
							env->ExceptionClear();
							env->DeleteLocalRef(eye);
							return;
						}
						env->CallNonvirtualVoidMethod(thiz, g_camera_cls, g_m_set_pos, ex, ey, ez);
						if (!env->ExceptionCheck())
						{
							jfloat clipped = env->CallNonvirtualFloatMethod(thiz, g_camera_cls,
								g_m_clip, s_dist);
							if (!env->ExceptionCheck())
							{
								env->CallNonvirtualVoidMethod(thiz, g_camera_cls, g_m_move_by,
									-clipped, 0.0f, 0.0f);
							}
						}
						if (env->ExceptionCheck()) env->ExceptionClear();
						env->DeleteLocalRef(eye);
					}

					// ----------------------------------------------------------
					// DrawContext matrix stack (1.21.6+ GUI is org.joml.Matrix3x2fStack)
					// ----------------------------------------------------------
					// Version: 12110 = 1.21.6+ (JOML Matrix3x2fStack), 1214 = <=1.21.5
					// (PoseStack). Intermediary class names are stable; only some
					// method ids/signatures differ between the two families.
					std::atomic<int> g_ver{0}; // 0 unknown, 1 = 12110+, 2 = 1214

					bool is_legacy()
					{
						int v = g_ver.load(std::memory_order_acquire);
						if (v == 0)
						{
							// Probe the actual getMatrices return type: the JOML
							// library class exists on both versions (bundled jar),
							// so checking for it is useless. GetMethodID with the
							// joml signature only succeeds on 1.21.6+.
							JNIEnv* env = flaway::instance ? flaway::instance->get_env() : nullptr;
							if (env)
							{
								jclass dc = sdk::classloader::find_class(env,
									sdk::mappings::draw_context_class_sig);
								if (dc)
								{
									jmethodID m = env->GetMethodID(dc,
										sdk::mappings::draw_context_matrices_name,
										sdk::mappings::draw_context_matrices_sig);
									if (env->ExceptionCheck()) { env->ExceptionClear(); m = nullptr; }
									v = m ? 1 : 2;
									env->DeleteLocalRef(dc);
								}
								else v = 1; // optimistic; resolve_matrix_ids retries
								g_ver.store(v, std::memory_order_release);
							}
							else v = 1;
						}
						return v == 2;
					}

					bool s_matrix_ok = false;
					jclass g_c_draw_ctx = nullptr;
					jclass g_c_joml = nullptr;
					jmethodID g_m_matrices = nullptr;
					jmethodID g_m_fill_gradient = nullptr;
					jmethodID g_m_push = nullptr;
					jmethodID g_m_pop = nullptr;
					jmethodID g_m_translate = nullptr;
					jmethodID g_m_scale = nullptr;
					bool s_matrix_legacy = false;

					bool resolve_matrix_ids(JNIEnv* env)
					{
						if (s_matrix_ok) return true;
						if (!g_c_draw_ctx)
							g_c_draw_ctx = find_global(env, sdk::mappings::draw_context_class_sig);
						if (g_c_draw_ctx)
						{
							if (!g_m_matrices)
							{
								g_m_matrices = find_instance(env, g_c_draw_ctx,
									sdk::mappings::draw_context_matrices_name,
									sdk::mappings::draw_context_matrices_sig);
								if (!g_m_matrices)
								{
									g_m_matrices = find_instance(env, g_c_draw_ctx,
										sdk::mappings::draw_context_matrices_name,
										sdk::mappings::draw_context_matrices_sig_legacy);
									if (g_m_matrices)
									{
										s_matrix_legacy = true;
										g_ver.store(2, std::memory_order_release);
									}
								}
								else g_ver.store(1, std::memory_order_release);
							}
							if (!g_m_fill_gradient)
								g_m_fill_gradient = find_instance(env, g_c_draw_ctx,
									sdk::mappings::fill_gradient_name,
									sdk::mappings::fill_gradient_sig);
						}
						if (!g_c_joml)
						{
							if (s_matrix_legacy)
								g_c_joml = find_global(env, sdk::mappings::matrix_stack_class_sig_legacy);
							else
								g_c_joml = find_global(env, sdk::mappings::matrix_stack_class_sig);
							if (g_c_joml)
							{
								if (s_matrix_legacy)
								{
									if (!g_m_push)
										g_m_push = find_instance(env, g_c_joml,
											sdk::mappings::matrix_push_name_legacy,
											sdk::mappings::matrix_push_sig_legacy);
									if (!g_m_pop)
										g_m_pop = find_instance(env, g_c_joml,
											sdk::mappings::matrix_pop_name_legacy,
											sdk::mappings::matrix_pop_sig_legacy);
									if (!g_m_translate)
										g_m_translate = find_instance(env, g_c_joml,
											sdk::mappings::matrix_translate_name_legacy,
											sdk::mappings::matrix_translate_sig_legacy);
									if (!g_m_scale)
										g_m_scale = find_instance(env, g_c_joml,
											sdk::mappings::matrix_scale_name_legacy,
											sdk::mappings::matrix_scale_sig_legacy);
								}
								else
								{
									if (!g_m_push)
										g_m_push = find_instance(env, g_c_joml,
											sdk::mappings::matrix_push_name, sdk::mappings::matrix_push_sig);
									if (!g_m_pop)
										g_m_pop = find_instance(env, g_c_joml,
											sdk::mappings::matrix_pop_name, sdk::mappings::matrix_pop_sig);
									if (!g_m_translate)
										g_m_translate = find_instance(env, g_c_joml,
											sdk::mappings::matrix_translate_name,
											sdk::mappings::matrix_translate_sig);
									if (!g_m_scale)
										g_m_scale = find_instance(env, g_c_joml,
											sdk::mappings::matrix_scale_name,
											sdk::mappings::matrix_scale_sig);
								}
							}
						}
						s_matrix_ok = g_c_draw_ctx && g_m_matrices && g_c_joml &&
							g_m_push && g_m_pop && g_m_translate && g_m_scale;
						return s_matrix_ok;
					}

					jobject matrices_of(JNIEnv* env, jobject ctx)
					{
						jobject st = env->CallObjectMethod(ctx, g_m_matrices);
						if (env->ExceptionCheck()) { env->ExceptionClear(); st = nullptr; }
						return st;
					}

					void m_push(JNIEnv* env, jobject st)
					{
						if (s_matrix_legacy)
						{
							env->CallVoidMethod(st, g_m_push);
							if (env->ExceptionCheck()) env->ExceptionClear();
							return;
						}
						jobject r = env->CallObjectMethod(st, g_m_push);
						if (env->ExceptionCheck()) env->ExceptionClear();
						if (r) env->DeleteLocalRef(r);
					}

					void m_pop(JNIEnv* env, jobject st)
					{
						if (s_matrix_legacy)
						{
							env->CallVoidMethod(st, g_m_pop);
							if (env->ExceptionCheck()) env->ExceptionClear();
							return;
						}
						jobject r = env->CallObjectMethod(st, g_m_pop);
						if (env->ExceptionCheck()) env->ExceptionClear();
						if (r) env->DeleteLocalRef(r);
					}

					void m_translate(JNIEnv* env, jobject st, jfloat dx, jfloat dy)
					{
						if (s_matrix_legacy)
						{
							// PoseStack.translate(x, y, z) = (FFF)V — args are (x,y,z).
							env->CallVoidMethod(st, g_m_translate, dx, dy, 0.0f);
							if (env->ExceptionCheck()) env->ExceptionClear();
							return;
						}
						jobject r = env->CallObjectMethod(st, g_m_translate, dx, dy);
						if (env->ExceptionCheck()) env->ExceptionClear();
						if (r) env->DeleteLocalRef(r);
					}

					void m_scale(JNIEnv* env, jobject st, jfloat sx, jfloat sy)
					{
						if (s_matrix_legacy)
						{
							env->CallVoidMethod(st, g_m_scale, sx, sy, 1.0f);
							if (env->ExceptionCheck()) env->ExceptionClear();
							return;
						}
						jobject r = env->CallObjectMethod(st, g_m_scale, sx, sy);
						if (env->ExceptionCheck()) env->ExceptionClear();
						if (r) env->DeleteLocalRef(r);
					}

					// ----------------------------------------------------------
					// ChatAnimation port - vanilla ChatHud + per-line alpha
					// ----------------------------------------------------------
					bool s_got_chat_anim = false;
					jclass g_c_chathud = nullptr;
					jclass g_c_visible = nullptr;
					jmethodID g_o_chat_render = nullptr;
					jmethodID g_o_add_message3 = nullptr;
					jmethodID g_o_line_bg = nullptr;
					jmethodID g_o_line_text = nullptr;
					jmethodID g_o_opacity = nullptr; // 1.21.4: static getMessageOpacityMultiplier
					jmethodID g_m_line_height = nullptr;
					jfieldID g_f_scrolled = nullptr;
					jfieldID g_f_added_time = nullptr;

					long long s_chat_last_msg_us = 0;
					int s_chat_tick = 0;
					long long s_chat_tick_boundary_us = 0;
					int s_chat_scrolled = 1;

					float sine_ease(float t)
					{
						return 0.5f - cosf(t * 3.14159265f) / 2.0f;
					}

					// ChatAnimation.getOpacityFactor: min(age / 150ms, 1), with the
					// age rebuilt from the line's addedTime tick + the wall clock of
					// the current render (we cannot store wall time on the record).
					float chat_fade(JNIEnv* env, jobject visible)
					{
						if (!visible || !g_f_added_time || s_chat_tick_boundary_us == 0)
							return 1.0f;
						jint added = env->GetIntField(visible, g_f_added_time);
						if (env->ExceptionCheck()) { env->ExceptionClear(); return 1.0f; }
						long long age_ms = 0;
						int d = s_chat_tick - (int)added;
						if (d > 0)
						{
							age_ms = (long long)d * 50 +
								(now_us() - s_chat_tick_boundary_us) / 1000;
						}
						else if (d == 0)
						{
							if (s_chat_last_msg_us == 0) return 1.0f;
							age_ms = (s_chat_tick_boundary_us - s_chat_last_msg_us) / 1000;
						}
						if (age_ms < 0) age_ms = 0;
						if (age_ms >= 150) return 1.0f;
						return (float)age_ms / 150.0f;
					}

					// ChatHud.addMessage(Text, MessageSignature, GuiMessageTag) - the
					// funnel every incoming message goes through.
					void hkAddMessage3(JNIEnv* env, jobject thiz, jobject text,
						jobject signature, jobject tag)
					{
						if (g_o_add_message3 && g_c_chathud)
						{
							env->CallNonvirtualVoidMethod(thiz, g_c_chathud, g_o_add_message3,
								text, signature, tag);
							if (env->ExceptionCheck()) env->ExceptionClear();
						}
						s_chat_last_msg_us = now_us();
					}

					// ChatHud.render(...) - wraps the vanilla chat with the
					// ChatAnimation stack displacement (translate down -> rises up).
					void hkChatRender(JNIEnv* env, jobject thiz, jobject ctx, jint tick,
						jint mouse_x, jint mouse_y, jboolean focused)
					{
						static bool s_logged = false;
						if (!s_logged) { s_logged = true; rlog::logf("bm: chat render fired"); }
						jobject st = nullptr;
						float disp = 0.0f;
						bool wrapped = false;
						if (chat_anim_active())
						{
							s_chat_tick = (int)tick;
							s_chat_tick_boundary_us = now_us();
							if (g_f_scrolled && g_m_line_height)
							{
								jint scrolled = env->GetIntField(thiz, g_f_scrolled);
								if (env->ExceptionCheck()) { env->ExceptionClear(); scrolled = 1; }
								s_chat_scrolled = (int)scrolled;
								if (scrolled == 0 && s_chat_last_msg_us != 0)
								{
									long long age = s_chat_tick_boundary_us - s_chat_last_msg_us;
									float k = age >= 150000 ? 1.0f : (float)age / 150000.0f;
									jint lh = env->CallIntMethod(thiz, g_m_line_height);
									if (env->ExceptionCheck()) { env->ExceptionClear(); lh = 0; }
									disp = (float)lh * 0.8f * (1.0f - k);
								}
							}
							if (disp > 0.01f && resolve_matrix_ids(env))
							{
								st = matrices_of(env, ctx);
								if (st)
								{
									m_push(env, st);
									m_translate(env, st, 0.0f, disp);
									wrapped = true;
								}
							}
						}
						if (g_o_chat_render && g_c_chathud)
						{
							env->CallNonvirtualVoidMethod(thiz, g_c_chathud, g_o_chat_render,
								ctx, tick, mouse_x, mouse_y, focused);
							if (env->ExceptionCheck()) env->ExceptionClear();
						}
						if (wrapped)
						{
							m_translate(env, st, 0.0f, -disp);
							m_pop(env, st);
							env->DeleteLocalRef(st);
						}
					}

					// Per-line background quad: trailing float is the vanilla alpha,
					// we multiply by the fade factor and re-apply ChatAnimation's
					// SINE easing (mirrors the mixin's ModifyVariable on forEachLine).
					void hkLineBg(JNIEnv* env, jobject thiz, jint index, jobject ctx,
						jfloat some_f, jint a, jint b, jint c, jobject visible,
						jint line_index, jfloat alpha)
					{
						jfloat out = alpha;
						if (chat_anim_active())
							out = sine_ease(alpha * chat_fade(env, visible));
						if (g_o_line_bg && g_c_chathud)
						{
							env->CallNonvirtualVoidMethod(thiz, g_c_chathud, g_o_line_bg,
								index, ctx, some_f, a, b, c, visible, line_index, out);
							if (env->ExceptionCheck()) env->ExceptionClear();
						}
					}

					// Per-line text: same treatment, alpha is the trailing float.
					void hkLineText(JNIEnv* env, jobject thiz, jobject ctx, jfloat some_f,
						jfloat other_f, jint a, jint b, jint c, jint d, jint e, jint f,
						jobject visible, jint line_index, jfloat alpha)
					{
						jfloat out = alpha;
						if (chat_anim_active())
							out = sine_ease(alpha * chat_fade(env, visible));
						if (g_o_line_text && g_c_chathud)
						{
							env->CallNonvirtualVoidMethod(thiz, g_c_chathud, g_o_line_text,
								ctx, some_f, other_f, a, b, c, d, e, f, visible, line_index, out);
							if (env->ExceptionCheck()) env->ExceptionClear();
						}
					}

					// 1.21.4: static ChatHud.getMessageOpacityMultiplier(int age).
					// Vanilla body (from bytecode): d=1-age/200; d*=10; d=clamp(d,0,1);
					// return d*d. We reimplement it exactly (no original call — the
					// original lives on the clone class and CallStaticXMethod with a
					// mismatched declaring class is fragile) and fold in the fade.
					jdouble hkOpacity(JNIEnv* env, jclass /*chathud*/, jint age)
					{
						static bool s_logged = false;
						if (!s_logged) { s_logged = true; rlog::logf("bm: opacity fired age=%d", (int)age); }
						double d = 1.0 - (double)age / 200.0;
						d *= 10.0;
						if (d < 0.0) d = 0.0;
						if (d > 1.0) d = 1.0;
						jdouble orig = (jdouble)(d * d);
						if (!chat_anim_active() || s_chat_scrolled != 0 ||
							s_chat_last_msg_us == 0)
							return orig;
						long long ms = (now_us() - s_chat_last_msg_us) / 1000;
						float k = ms >= 150 ? 1.0f : (float)ms / 150.0f;
						return orig * (jdouble)sine_ease(k);
					}

					// ----------------------------------------------------------
					// SmoothGUI port - setScreen tracking + screen render wrap
					// ----------------------------------------------------------
					bool s_got_gui_anim = false;
					jclass g_c_mc = nullptr;
					jclass g_c_screen = nullptr;
					jclass g_c_entry_list = nullptr;
					jmethodID g_o_screen_render = nullptr;
					jmethodID g_o_transparent_bg = nullptr;
					jfieldID g_f_current_screen = nullptr;
					jfieldID g_f_world = nullptr;
					jfieldID g_f_player = nullptr;
					jfieldID g_f_children = nullptr;
					jfieldID g_f_width = nullptr;
					jfieldID g_f_height = nullptr;
					jmethodID g_m_list_size = nullptr;
					jmethodID g_m_list_get = nullptr;

					// Transition screens the mod never animates (intermediary ids).
					jclass g_c_blocked[6] = {};
					const char* k_blocked[6] = {
						"net/minecraft/class_408",   // ChatScreen
						"net/minecraft/class_3928",  // LevelLoadingScreen
						"net/minecraft/class_412",   // ConnectScreen
						"net/minecraft/class_435",   // ProgressScreen
						"net/minecraft/class_423",   // SleepingChatScreen (InBedChatScreen)
						"net/minecraft/class_424",   // MessageScreen (GenericMessageScreen)
					};
					bool s_blocked_resolved = false;

					void resolve_blocked()
					{
						if (s_blocked_resolved) return;
						JNIEnv* env = flaway::instance ? flaway::instance->get_env() : nullptr;
						if (!env) return;
						for (int i = 0; i < 6; i++)
							if (!g_c_blocked[i]) g_c_blocked[i] = find_global(env, k_blocked[i]);
						s_blocked_resolved = true;
					}

					bool screen_blocked(JNIEnv* env, jobject screen)
					{
						resolve_blocked();
						if (!screen) return false;
						bool blocked = false;
						for (int i = 0; i < 6; i++)
						{
							if (g_c_blocked[i] && env->IsInstanceOf(screen, g_c_blocked[i]))
							{
								blocked = true;
								break;
							}
						}
						return blocked;
					}

					// SmoothGui.shouldAnimateScreen: not a blocked screen, in-game,
					// and no AbstractSelectionList child (settings/creative lists).
					bool should_animate_screen(JNIEnv* env, jobject screen)
					{
						if (!screen || screen_blocked(env, screen)) return false;

						bool in_game = false;
						if (sdk::instance && g_f_world && g_f_player)
						{
							jobject mc = sdk::instance->get_minecraft();
							if (mc)
							{
								jobject w = env->GetObjectField(mc, g_f_world);
								if (env->ExceptionCheck()) { env->ExceptionClear(); w = nullptr; }
								jobject p = env->GetObjectField(mc, g_f_player);
								if (env->ExceptionCheck()) { env->ExceptionClear(); p = nullptr; }
								in_game = (w != nullptr) || (p != nullptr);
								if (w) env->DeleteLocalRef(w);
								if (p) env->DeleteLocalRef(p);
								env->DeleteLocalRef(mc);
							}
						}
						if (!in_game) return false;

						if (g_f_children && g_c_entry_list && g_m_list_size && g_m_list_get)
						{
							jobject list = env->GetObjectField(screen, g_f_children);
							if (env->ExceptionCheck()) { env->ExceptionClear(); list = nullptr; }
							if (list)
							{
								jint n = env->CallIntMethod(list, g_m_list_size);
								if (env->ExceptionCheck()) { env->ExceptionClear(); n = 0; }
								bool has_list = false;
								for (jint i = 0; i < n && !has_list; i++)
								{
									jobject ch = env->CallObjectMethod(list, g_m_list_get, i);
									if (env->ExceptionCheck()) { env->ExceptionClear(); ch = nullptr; }
									if (ch)
									{
										if (env->IsInstanceOf(ch, g_c_entry_list)) has_list = true;
										env->DeleteLocalRef(ch);
									}
								}
								env->DeleteLocalRef(list);
								if (has_list) return false;
							}
						}
						return true;
					}

					// 9 * BACK(1 - alpha) with BACK(x) = 2.70158x^3 - 1.70158x^2
					// (animationTime 220ms, scale 1, direction DOWN).
					float gui_disp_since(long long since_us)
					{
						long long age = now_us() - since_us;
						if (age < 0) age = 0;
						float alpha = age >= 220000 ? 1.0f : (float)age / 220000.0f;
						float t = 1.0f - alpha;
						float back = 2.70158f * t * t * t - 1.70158f * t * t;
						return 9.0f * back;
					}

					// Screen changes tracked by polling currentScreen in render hooks.
					// (Direct setScreen hook bypasses Fabric Screen API mixin and
					// crashes with "screen has not been correctly initialised".)
					jobject s_last_screen = nullptr;
					long long s_screen_changed_us = 0;
					long long s_screen_opened_us = 0;
					bool s_applied = false;
					float s_screen_disp = 0.0f;

					void track_screen_change(JNIEnv* env, jobject current)
					{
						if (!current)
						{
							if (s_last_screen) { env->DeleteGlobalRef(s_last_screen); s_last_screen = nullptr; }
							return;
						}
						if (s_last_screen && env->IsSameObject(s_last_screen, current))
							return;
						if (s_last_screen) env->DeleteGlobalRef(s_last_screen);
						s_last_screen = env->NewGlobalRef(current);
						s_screen_changed_us = now_us();
					}

					// Screen.renderWithTooltip (final) - pushes the screen content
					// up while the open animation plays and shifts the mouse Y so
					// widget hit-tests follow the visual position.
					void hkScreenRender(JNIEnv* env, jobject thiz, jobject ctx,
						jint mouse_x, jint mouse_y, jfloat tick_delta)
					{
						static bool s_logged = false;
						if (!s_logged) { s_logged = true; rlog::logf("bm: screen render fired"); }
						// Poll current screen to detect changes (replaces setScreen hook).
						if (sdk::instance && g_f_current_screen)
						{
							jobject mc = sdk::instance->get_minecraft();
							if (mc)
							{
								jobject cur = env->GetObjectField(mc, g_f_current_screen);
								if (env->ExceptionCheck()) { env->ExceptionClear(); cur = nullptr; }
								if (cur)
								{
									if (!s_last_screen)
										s_screen_opened_us = now_us();
									track_screen_change(env, cur);
									env->DeleteLocalRef(cur);
								}
								else
								{
									track_screen_change(env, nullptr);
								}
								env->DeleteLocalRef(mc);
							}
						}
						jobject st = nullptr;
						float disp = 0.0f;
						bool wrapped = false;
						if (gui_anim_active() && should_animate_screen(env, thiz))
						{
							disp = gui_disp_since(s_screen_changed_us);
							if (disp > 0.01f && resolve_matrix_ids(env))
							{
								st = matrices_of(env, ctx);
								if (st)
								{
									m_push(env, st);
									m_translate(env, st, 0.0f, -disp);
									s_applied = true;
									s_screen_disp = disp;
									wrapped = true;
								}
							}
						}
						if (g_o_screen_render && g_c_screen)
						{
							env->CallNonvirtualVoidMethod(thiz, g_c_screen, g_o_screen_render,
								ctx, mouse_x, mouse_y + (jint)disp, tick_delta);
							if (env->ExceptionCheck()) env->ExceptionClear();
						}
						if (wrapped)
						{
							s_applied = false;
							s_screen_disp = 0.0f;
							m_translate(env, st, 0.0f, disp);
							m_pop(env, st);
							env->DeleteLocalRef(st);
						}
						else
						{
							s_applied = false;
							s_screen_disp = 0.0f;
						}
					}

					// Screen.renderInGameBackground (renderTransparentBackground):
					// cancel the vanilla dark gradient and draw SmoothGUI's own
					// that fades in over backgroundFadeTime=150ms (cubic reverse),
					// at backgroundOpacity=0.65 over #080808.
					void hkTransparentBg(JNIEnv* env, jobject thiz, jobject ctx)
					{
						if (!gui_anim_active())
						{
							if (g_o_transparent_bg && g_c_screen)
							{
								env->CallNonvirtualVoidMethod(thiz, g_c_screen,
									g_o_transparent_bg, ctx);
								if (env->ExceptionCheck()) env->ExceptionClear();
							}
							return;
						}

						float f = 1.0f;
						if (should_animate_screen(env, thiz))
						{
							long long age = now_us() - s_screen_opened_us;
							float a = age <= 0 ? 1.0f :
								(age >= 150000 ? 1.0f : (float)age / 150000.0f);
							f = 1.0f - (1.0f - a) * (1.0f - a) * (1.0f - a);
						}
						int a_top = (int)(f * 0.9f * 0.65f * 255.0f);
						int a_bot = (int)(f * 1.0f * 0.65f * 255.0f);
						if (a_top < 0) a_top = 0;
						if (a_top > 255) a_top = 255;
						if (a_bot < 0) a_bot = 0;
						if (a_bot > 255) a_bot = 255;
						jint col_top = (a_top << 24) | (8 << 16) | (8 << 8) | 8;
						jint col_bot = (a_bot << 24) | (8 << 16) | (8 << 8) | 8;

						jint w = 0, h = 0;
						if (g_f_width)
						{
							w = env->GetIntField(thiz, g_f_width);
							if (env->ExceptionCheck()) { env->ExceptionClear(); w = 0; }
						}
						if (g_f_height)
						{
							h = env->GetIntField(thiz, g_f_height);
							if (env->ExceptionCheck()) { env->ExceptionClear(); h = 0; }
						}

						// We are inside the render wrap: undo the displacement for
						// the background (wrapInverse), draw, put it back.
						bool inv = s_applied;
						if (inv && resolve_matrix_ids(env))
						{
							jobject st = matrices_of(env, ctx);
							if (st)
							{
								m_translate(env, st, 0.0f, s_screen_disp);
								if (g_m_fill_gradient)
								{
									env->CallVoidMethod(ctx, g_m_fill_gradient, 0, 0, w, h,
										col_top, col_bot);
									if (env->ExceptionCheck()) env->ExceptionClear();
								}
								m_translate(env, st, 0.0f, -s_screen_disp);
								env->DeleteLocalRef(st);
								return;
							}
						}
						if (g_m_fill_gradient)
						{
							env->CallVoidMethod(ctx, g_m_fill_gradient, 0, 0, w, h,
								col_top, col_bot);
							if (env->ExceptionCheck()) env->ExceptionClear();
						}
					}

					// ----------------------------------------------------------
					// Custom menu background + loading gif hooks.
					// Screen (class_437) is too large for jnihook's class-copy
					// mechanism — the copy fails verification (the blanket
					// rewrite of Lnet/minecraft/class_437; in NameAndType
					// descriptors also corrupts foreign refs such as
					// MinecraftClient.setScreen). Concrete subclasses are hooked
					// instead; their names never appear in foreign descriptors,
					// so their copies verify:
					//   TitleScreen.method_25420    -> menu bg + stars
					//   ProgressScreen.method_25394 -> loading gif
					//   SplashOverlay.method_25394  -> loading gif (boot screen)
					// ----------------------------------------------------------
					constexpr const char* k_splash_class_sig = "net/minecraft/class_425";
					constexpr const char* k_logo_class_sig = "net/minecraft/class_8020";
					jclass g_c_title = nullptr;
					jclass g_c_progress = nullptr;
					jclass g_c_splash = nullptr;
					jclass g_c_logo = nullptr;
					jmethodID g_o_title_bg = nullptr;
					jmethodID g_o_progress_render = nullptr;
					jmethodID g_o_splash_render = nullptr;
					jmethodID g_m_ctx_flush = nullptr;
					jmethodID g_o_logo_render = nullptr;
					long long s_menu_last_us = 0;

					// DrawContext batches part of the GUI; its tessellator is
					// flushed AFTER our raw-GL draw, which would paint vanilla
					// geometry (panorama tint, Mojang logo, ...) over us.
					// Flushing right before we draw puts us on top of everything
					// submitted so far; later vanilla draws still land above us.
					void flush_gui(JNIEnv* env, jobject ctx)
					{
						if (!ctx) return;
						if (!g_m_ctx_flush)
						{
							if (!g_c_draw_ctx)
								g_c_draw_ctx = find_global(env,
									sdk::mappings::draw_context_class_sig);
							if (!g_c_draw_ctx) return;
							g_m_ctx_flush = find_instance(env, g_c_draw_ctx, "method_51452", "()V");
							if (!g_m_ctx_flush)
								rlog::logf("bm: DrawContext.draw() not found (overdraw possible)");
						}
						if (!g_m_ctx_flush) return;
						env->CallVoidMethod(ctx, g_m_ctx_flush);
						if (env->ExceptionCheck()) env->ExceptionClear();
					}

					void menu_tick()
					{
						long long now = now_us();
						if (s_menu_last_us == 0) { s_menu_last_us = now; return; }
						float dt = (float)(now - s_menu_last_us) / 1000000.0f;
						s_menu_last_us = now;
						if (dt > 0.0f && dt < 0.25f)
							menu_bg::tick(dt);
					}

					// ProgressScreen.render may close itself (setScreen(null))
					// when done, but still returns into our hook — don't paint
					// the gif over the world for that last frame.
					bool is_current_screen(JNIEnv* env, jobject thiz)
					{
						if (!thiz) return false;
						if (!sdk::instance || !g_f_current_screen) return true;
						jobject mc = sdk::instance->get_minecraft();
						if (!mc) return true;
						jobject cur = env->GetObjectField(mc, g_f_current_screen);
						if (env->ExceptionCheck()) { env->ExceptionClear(); cur = nullptr; }
						bool ok = cur ? (env->IsSameObject(cur, thiz) != JNI_FALSE) : false;
						if (cur) env->DeleteLocalRef(cur);
						env->DeleteLocalRef(mc);
						return ok;
					}

					// TitleScreen.renderBackground — runs inside Screen.render,
					// after the vanilla panorama, before the widgets/logo.
					void hkTitleBg(JNIEnv* env, jobject thiz, jobject ctx,
						jint mouse_x, jint mouse_y, jfloat tick_delta)
					{
						static bool s_logged = false;
						if (!s_logged) { s_logged = true; rlog::logf("bm: title bg fired"); }
						(void)ctx; (void)mouse_x; (void)mouse_y; (void)tick_delta;
						if (!g_o_title_bg || !g_c_title)
							return;
						env->CallNonvirtualVoidMethod(thiz, g_c_title, g_o_title_bg,
							ctx, mouse_x, mouse_y, tick_delta);
						if (env->ExceptionCheck()) env->ExceptionClear();
						if (Hook::get_unhooked()) return;
						if (!menu_bg::is_custom_bg_screen(env, thiz)) return;
						flush_gui(env, ctx);
						menu_tick();
						menu_bg::draw_background();
					}

					// ProgressScreen.render — vanilla already drew its own bg,
					// so paint the gif on top of the original.
					void hkProgressRender(JNIEnv* env, jobject thiz, jobject ctx,
						jint mouse_x, jint mouse_y, jfloat tick_delta)
					{
						static bool s_logged = false;
						if (!s_logged) { s_logged = true; rlog::logf("bm: progress screen fired"); }
						if (!g_o_progress_render || !g_c_progress)
							return;
						env->CallNonvirtualVoidMethod(thiz, g_c_progress, g_o_progress_render,
							ctx, mouse_x, mouse_y, tick_delta);
						if (env->ExceptionCheck()) env->ExceptionClear();
						if (Hook::get_unhooked()) return;
						if (!is_current_screen(env, thiz)) return;
						flush_gui(env, ctx);
						menu_bg::draw_loading();
					}

					// SplashOverlay.render — the boot "Mojang Studios" screen.
					// Fires only while the overlay is on screen, so no extra
					// current-screen check is needed.
					void hkSplashRender(JNIEnv* env, jobject thiz, jobject ctx,
						jint mouse_x, jint mouse_y, jfloat tick_delta)
					{
						static bool s_logged = false;
						if (!s_logged) { s_logged = true; rlog::logf("bm: splash overlay fired"); }
						(void)thiz;
						if (!g_o_splash_render || !g_c_splash)
							return;
						env->CallNonvirtualVoidMethod(thiz, g_c_splash, g_o_splash_render,
							ctx, mouse_x, mouse_y, tick_delta);
						if (env->ExceptionCheck()) env->ExceptionClear();
						if (Hook::get_unhooked()) return;
						// The red Mojang logo is the last (unflushed) draw inside
						// SplashOverlay.render — flush, then cover it with the gif.
						flush_gui(env, ctx);
						menu_bg::draw_loading();
					}

					// LogoRenderer.render — replace the vanilla coloured logo with
					// a black silhouette at the same place.
					void hkLogoRender(JNIEnv* env, jobject thiz, jobject ctx,
						jint gui_w, jfloat alpha)
					{
						static bool s_logged = false;
						if (!s_logged) { s_logged = true; rlog::logf("bm: logo render fired"); }
						(void)thiz;
						if (g_o_logo_render && g_c_logo)
						{
							env->CallNonvirtualVoidMethod(thiz, g_c_logo, g_o_logo_render,
								ctx, gui_w, alpha);
							if (env->ExceptionCheck()) env->ExceptionClear();
						}
						if (Hook::get_unhooked()) return;
						if (gui_w <= 0) return;
						jint gui_h = 0;
						if (sdk::instance && g_f_current_screen && g_f_height)
						{
							jobject mc = sdk::instance->get_minecraft();
							if (mc)
							{
								jobject cur = env->GetObjectField(mc, g_f_current_screen);
								if (env->ExceptionCheck()) { env->ExceptionClear(); cur = nullptr; }
								if (cur)
								{
									gui_h = env->GetIntField(cur, g_f_height);
									if (env->ExceptionCheck()) { env->ExceptionClear(); gui_h = 0; }
									env->DeleteLocalRef(cur);
								}
								env->DeleteLocalRef(mc);
							}
						}
						flush_gui(env, ctx);
						menu_bg::draw_logo_black((int)gui_w, (int)gui_h);
					}

					// ----------------------------------------------------------
					// Tiny Item Animations port - slot pop + floating item grow
					// ----------------------------------------------------------
					bool s_got_items_anim = false;
					jclass g_c_hs = nullptr;
					jclass g_c_menu = nullptr;
					jclass g_c_slot = nullptr;
					jmethodID g_o_draw_slot = nullptr;
					jmethodID g_o_draw_cursor = nullptr;
					jmethodID g_o_click_slot = nullptr;
					jmethodID g_o_draw_item[4] = {};
					jmethodID g_m_get_slot = nullptr;
					jfieldID g_f_slot_x = nullptr;
					jfieldID g_f_slot_y = nullptr;

					// 1 = inside HandledScreen.drawSlot, 2 = drawing the cursor stack.
					int g_item_mode = 0;
					float g_slot_p = 0.0f;
					float g_carried_p = 0.0f;
					std::unordered_map<long long, float> g_slot_anim;
					long long g_slot_last_t_us = 0;
					long long g_cursor_last_t_us = 0;

					long long slot_key(jint x, jint y)
					{
						return ((long long)(unsigned int)x << 32) | (long long)(unsigned int)y;
					}

					float delta_ticks(long long& last)
					{
						long long now = now_us();
						if (last == 0) { last = now; return 0.0f; }
						float dt = (float)(now - last) / 1000000.0f;
						last = now;
						if (dt < 0.0f || dt > 0.25f) dt = 0.0f;
						return dt * 20.0f;
					}

					float item_scale(float p)
					{
						if (p <= 0.0f) return 1.0f;
						if (p > 1.0f) p = 1.0f;
						return 1.0f + 0.4f * (1.0f - powf(1.0f - p, 5.0f));
					}

					// HandledScreen.drawSlot: expose the slot's animation progress to
					// the drawItem hooks and decay it afterwards (animationSpeed=0.5).
					void hkDrawSlot(JNIEnv* env, jobject thiz, jobject ctx, jobject slot)
					{
						static bool s_logged = false;
						if (!s_logged) { s_logged = true; rlog::logf("bm: drawSlot fired"); }
						bool act = items_anim_active() && slot && g_f_slot_x && g_f_slot_y;
						long long key = 0;
						bool track = false;
						if (act)
						{
							jint sx = env->GetIntField(slot, g_f_slot_x);
							if (env->ExceptionCheck()) { env->ExceptionClear(); sx = 0; act = false; }
							jint sy = env->GetIntField(slot, g_f_slot_y);
							if (env->ExceptionCheck()) { env->ExceptionClear(); sy = 0; act = false; }
							if (act)
							{
								key = slot_key(sx, sy);
								auto it = g_slot_anim.find(key);
								g_slot_p = it != g_slot_anim.end() ? it->second : 0.0f;
								g_item_mode = 1;
								track = true;
							}
						}
						if (g_o_draw_slot && g_c_hs)
						{
							env->CallNonvirtualVoidMethod(thiz, g_c_hs, g_o_draw_slot, ctx, slot);
							if (env->ExceptionCheck()) env->ExceptionClear();
						}
						if (track)
						{
							g_item_mode = 0;
							float dt = delta_ticks(g_slot_last_t_us);
							float p = g_slot_p - dt * 0.5f;
							if (p <= 0.0f) g_slot_anim.erase(key);
							else g_slot_anim[key] = p;
							g_slot_p = 0.0f;
						}
					}

					// HandledScreen.drawItem (cursor stack): ramp the growth progress
					// while the floating item is on screen (resets on every click).
					void hkDrawCursor(JNIEnv* env, jobject thiz, jobject ctx, jobject stack,
						jint x, jint y, jobject label)
					{
						bool act = items_anim_active();
						if (act)
						{
							float dt = delta_ticks(g_cursor_last_t_us);
							g_carried_p += dt * 0.5f;
							if (g_carried_p > 1.0f) g_carried_p = 1.0f;
							g_item_mode = 2;
						}
						if (g_o_draw_cursor && g_c_hs)
						{
							env->CallNonvirtualVoidMethod(thiz, g_c_hs, g_o_draw_cursor,
								ctx, stack, x, y, label);
							if (env->ExceptionCheck()) env->ExceptionClear();
						}
						if (act) g_item_mode = 0;
					}

					// DrawContext drawItem family: scale the item about its center
					// (x+8, y+8). The quads are baked with a copy of the current 2D
					// matrix, so the wrap applies; count labels use other methods and
					// stay unscaled - exactly what TIA does.
					bool item_wrap_begin(JNIEnv* env, jobject thiz, jint x, jint y,
						jobject* st_out)
					{
						*st_out = nullptr;
						if (!items_anim_active() || g_item_mode == 0 || !resolve_matrix_ids(env))
							return false;
						float p = g_item_mode == 1 ? g_slot_p : g_carried_p;
						if (p <= 0.001f) return false;
						jobject st = matrices_of(env, thiz);
						if (!st) return false;
						float s = item_scale(p);
						jfloat cx = (jfloat)(x + 8);
						jfloat cy = (jfloat)(y + 8);
						m_push(env, st);
						m_translate(env, st, cx, cy);
						m_scale(env, st, s, s);
						m_translate(env, st, -cx, -cy);
						*st_out = st;
						return true;
					}

					void item_wrap_end(JNIEnv* env, jobject st)
					{
						if (!st) return;
						m_pop(env, st);
						env->DeleteLocalRef(st);
					}

					void hkDrawItem(JNIEnv* env, jobject thiz, jobject stack,
						jint x, jint y)
					{
						jobject st = nullptr;
						bool wrapped = item_wrap_begin(env, thiz, x, y, &st);
						if (g_o_draw_item[0] && g_c_draw_ctx)
						{
							env->CallNonvirtualVoidMethod(thiz, g_c_draw_ctx, g_o_draw_item[0],
								stack, x, y);
							if (env->ExceptionCheck()) env->ExceptionClear();
						}
						item_wrap_end(env, st);
						(void)wrapped;
					}

					void hkDrawItemZ(JNIEnv* env, jobject thiz, jobject stack,
						jint x, jint y, jint seed)
					{
						jobject st = nullptr;
						bool wrapped = item_wrap_begin(env, thiz, x, y, &st);
						if (g_o_draw_item[1] && g_c_draw_ctx)
						{
							env->CallNonvirtualVoidMethod(thiz, g_c_draw_ctx, g_o_draw_item[1],
								stack, x, y, seed);
							if (env->ExceptionCheck()) env->ExceptionClear();
						}
						item_wrap_end(env, st);
						(void)wrapped;
					}

					void hkDrawItemNoEntZ(JNIEnv* env, jobject thiz, jobject stack,
						jint x, jint y, jint seed)
					{
						jobject st = nullptr;
						bool wrapped = item_wrap_begin(env, thiz, x, y, &st);
						if (g_o_draw_item[2] && g_c_draw_ctx)
						{
							env->CallNonvirtualVoidMethod(thiz, g_c_draw_ctx, g_o_draw_item[2],
								stack, x, y, seed);
							if (env->ExceptionCheck()) env->ExceptionClear();
						}
						item_wrap_end(env, st);
						(void)wrapped;
					}

					void hkDrawItemNoEnt(JNIEnv* env, jobject thiz, jobject stack,
						jint x, jint y)
					{
						jobject st = nullptr;
						bool wrapped = item_wrap_begin(env, thiz, x, y, &st);
						if (g_o_draw_item[3] && g_c_draw_ctx)
						{
							env->CallNonvirtualVoidMethod(thiz, g_c_draw_ctx, g_o_draw_item[3],
								stack, x, y);
							if (env->ExceptionCheck()) env->ExceptionClear();
						}
						item_wrap_end(env, st);
						(void)wrapped;
					}

					// AbstractContainerMenu.onSlotClick - TIA's trigger (equivalent to
					// the mixin injected after tryItemClickBehaviourOverride): pop the
					// clicked slot and restart the floating-item growth.
					void hkClickSlot(JNIEnv* env, jobject thiz, jint slot_id, jint button,
						jobject click_type, jobject player)
					{
						static bool s_logged = false;
						if (!s_logged) { s_logged = true; rlog::logf("bm: clickSlot fired id=%d", (int)slot_id); }
						if (g_o_click_slot && g_c_menu)
						{
							env->CallNonvirtualVoidMethod(thiz, g_c_menu, g_o_click_slot,
								slot_id, button, click_type, player);
							if (env->ExceptionCheck()) env->ExceptionClear();
						}
						if (!items_anim_active()) return;
						g_carried_p = 0.0f;
						if (slot_id < 0 || !g_m_get_slot || !g_c_slot ||
							!g_f_slot_x || !g_f_slot_y)
							return;
						jobject slot = env->CallObjectMethod(thiz, g_m_get_slot, (jint)slot_id);
						if (env->ExceptionCheck()) { env->ExceptionClear(); return; }
						if (slot)
						{
							jint sx = env->GetIntField(slot, g_f_slot_x);
							if (env->ExceptionCheck()) { env->ExceptionClear(); sx = -1; }
							jint sy = env->GetIntField(slot, g_f_slot_y);
							if (env->ExceptionCheck()) { env->ExceptionClear(); sy = -1; }
							if (sx >= 0)
								g_slot_anim[slot_key(sx, sy)] = 1.0f;
							env->DeleteLocalRef(slot);
						}
					}

					// ---- mouse sensitivity scaling (zoom mirrors the divisor) ----
					// Mouse reads sensitivity live per event as
					//   eff = value * 0.6 + 0.2;  turn_scale = eff^3 * 8
					// (verified in 1.21.10 Mouse/using decompile). To divide the
					// turn rate by `state` we scale eff by state^(-1/3), clamped by
					// the formula's own floor (eff >= 0.2 -> value >= 0).
					bool s_sens_saved = false;
					double s_sens_orig = 0.0;
					int s_sens_box = 1;   // 1 = Double (vanilla), 2 = Float

					jfieldID g_f_client_options = nullptr;
					jfieldID g_f_options_sens = nullptr;
					jfieldID g_f_option_value = nullptr;

					bool sens_chain(JNIEnv* env, jobject* mc_out, jobject* opt_out,
						jobject* so_out)
					{
						*mc_out = nullptr;
						*opt_out = nullptr;
						*so_out = nullptr;
						if (!sdk::instance) return false;

						if (!g_f_client_options)
						{
							jclass c = sdk::classloader::find_class(env,
								sdk::mappings::minecraftclass_sig);
							if (c)
							{
								g_f_client_options = env->GetFieldID(c,
									sdk::mappings::game_options_name,
									sdk::mappings::game_options_sig);
								if (env->ExceptionCheck()) { env->ExceptionClear(); g_f_client_options = nullptr; }
								env->DeleteLocalRef(c);
							}
						}
						if (!g_f_options_sens)
						{
							jclass c = sdk::classloader::find_class(env,
								sdk::mappings::game_options_class_sig);
							if (c)
							{
								g_f_options_sens = env->GetFieldID(c,
									sdk::mappings::game_options_sensitivity_name,
									sdk::mappings::game_options_sensitivity_sig);
								if (env->ExceptionCheck()) { env->ExceptionClear(); g_f_options_sens = nullptr; }
								env->DeleteLocalRef(c);
							}
						}
						if (!g_f_option_value)
						{
							jclass c = sdk::classloader::find_class(env,
								sdk::mappings::simple_option_class_sig);
							if (c)
							{
								g_f_option_value = env->GetFieldID(c,
									sdk::mappings::simple_option_value_name,
									sdk::mappings::simple_option_value_sig);
								if (env->ExceptionCheck()) { env->ExceptionClear(); g_f_option_value = nullptr; }
								env->DeleteLocalRef(c);
							}
						}
						if (!g_f_client_options || !g_f_options_sens || !g_f_option_value)
							return false;

						jobject mc = sdk::instance->get_minecraft();
						if (!mc) return false;
						jobject opt = env->GetObjectField(mc, g_f_client_options);
						if (env->ExceptionCheck() || !opt)
						{
							env->ExceptionClear();
							env->DeleteLocalRef(mc);
							return false;
						}
						jobject so = env->GetObjectField(opt, g_f_options_sens);
						if (env->ExceptionCheck() || !so)
						{
							env->ExceptionClear();
							env->DeleteLocalRef(opt);
							env->DeleteLocalRef(mc);
							return false;
						}
						*mc_out = mc;
						*opt_out = opt;
						*so_out = so;
						return true;
					}

					void chain_release(JNIEnv* env, jobject mc, jobject opt, jobject so)
					{
						if (so) env->DeleteLocalRef(so);
						if (opt) env->DeleteLocalRef(opt);
						if (mc) env->DeleteLocalRef(mc);
					}

					bool read_boxed(JNIEnv* env, jobject so, double& out, int& box)
					{
						jobject val = env->GetObjectField(so, g_f_option_value);
						if (env->ExceptionCheck() || !val)
						{
							env->ExceptionClear();
							return false;
						}
						jclass dc = sdk::classloader::find_class(env, "java/lang/Double");
						jclass fc = sdk::classloader::find_class(env, "java/lang/Float");
						bool ok = false;
						if (dc && env->IsInstanceOf(val, dc))
						{
							static jmethodID mid = nullptr;
							if (!mid) mid = find_instance(env, dc, "doubleValue", "()D");
							if (mid)
							{
								out = env->CallDoubleMethod(val, mid);
								if (env->ExceptionCheck()) { env->ExceptionClear(); }
								else { ok = true; box = 1; }
							}
						}
						else if (fc && env->IsInstanceOf(val, fc))
						{
							static jmethodID mid = nullptr;
							if (!mid) mid = find_instance(env, fc, "floatValue", "()F");
							if (mid)
							{
								out = (double)env->CallFloatMethod(val, mid);
								if (env->ExceptionCheck()) { env->ExceptionClear(); }
								else { ok = true; box = 2; }
							}
						}
						if (dc) env->DeleteLocalRef(dc);
						if (fc) env->DeleteLocalRef(fc);
						env->DeleteLocalRef(val);
						return ok;
					}

					jobject box_value(JNIEnv* env, int box, double v)
					{
						jobject res = nullptr;
						if (box == 2)
						{
							jclass fc = sdk::classloader::find_class(env, "java/lang/Float");
							if (!fc) return nullptr;
							static jmethodID mid = nullptr;
							if (!mid) mid = find_instance(env, fc, "<init>", "(F)V");
							if (mid) res = env->NewObject(fc, mid, (jfloat)v);
							if (env->ExceptionCheck()) { env->ExceptionClear(); res = nullptr; }
							env->DeleteLocalRef(fc);
						}
						else
						{
							jclass dc = sdk::classloader::find_class(env, "java/lang/Double");
							if (!dc) return nullptr;
							static jmethodID mid = nullptr;
							if (!mid) mid = find_instance(env, dc, "<init>", "(D)V");
							if (mid) res = env->NewObject(dc, mid, (jdouble)v);
							if (env->ExceptionCheck()) { env->ExceptionClear(); res = nullptr; }
							env->DeleteLocalRef(dc);
						}
						return res;
					}

					void write_sensitivity(JNIEnv* env, double value)
					{
						jobject mc = nullptr, opt = nullptr, so = nullptr;
						if (!sens_chain(env, &mc, &opt, &so)) return;
						jobject box = box_value(env, s_sens_box, value);
						if (box)
						{
							env->SetObjectField(so, g_f_option_value, box);
							if (env->ExceptionCheck()) env->ExceptionClear();
							env->DeleteLocalRef(box);
						}
						chain_release(env, mc, opt, so);
					}

					void apply_sensitivity(float state)
					{
						if (state <= 1.01f) return;
						JNIEnv* env = flaway::instance ? flaway::instance->get_env() : nullptr;
						if (!env) return;
						jobject mc = nullptr, opt = nullptr, so = nullptr;
						if (!sens_chain(env, &mc, &opt, &so)) return;
						if (!s_sens_saved)
						{
							if (!read_boxed(env, so, s_sens_orig, s_sens_box))
							{
								chain_release(env, mc, opt, so);
								return;
							}
							s_sens_saved = true;
						}
						double eff = s_sens_orig * 0.6 + 0.2;
						double want = eff / cbrt((double)state);
						double v = (want - 0.2) / 0.6;
						if (v < 0.0) v = 0.0;
						if (v > 1.0) v = 1.0;
						jobject box = box_value(env, s_sens_box, v);
						if (box)
						{
							env->SetObjectField(so, g_f_option_value, box);
							if (env->ExceptionCheck()) env->ExceptionClear();
							env->DeleteLocalRef(box);
						}
						chain_release(env, mc, opt, so);
					}

					void restore_sensitivity()
					{
						if (!s_sens_saved) return;
						JNIEnv* env = flaway::instance ? flaway::instance->get_env() : nullptr;
						s_sens_saved = false;
						if (!env) return;
						write_sensitivity(env, s_sens_orig);
					}

					// ----------------------------------------------------------
					// hook groups
					// ----------------------------------------------------------
					bool init_chat_hooks(JNIEnv* env)
					{
						if (!g_c_chathud)
							g_c_chathud = find_global(env, sdk::mappings::chat_hud_class_sig);
						if (!g_c_chathud) return false;
						if (!g_c_visible)
							g_c_visible = find_global(env, sdk::mappings::chat_visible_class_sig);
						if (g_c_visible && !g_f_added_time)
							g_f_added_time = find_field(env, g_c_visible,
								sdk::mappings::chat_visible_added_time_name,
								sdk::mappings::chat_visible_added_time_sig);
						if (!g_m_line_height)
							g_m_line_height = find_instance(env, g_c_chathud,
								sdk::mappings::chat_hud_line_height_name,
								sdk::mappings::chat_hud_line_height_sig);
						if (!g_f_scrolled)
							g_f_scrolled = find_field(env, g_c_chathud,
								sdk::mappings::chat_hud_scrolled_name,
								sdk::mappings::chat_hud_scrolled_sig);

						if (!g_o_chat_render)
						{
							jmethodID m = find_instance(env, g_c_chathud,
								sdk::mappings::chat_hud_render_name,
								sdk::mappings::chat_hud_render_sig);
							if (!attach(m, reinterpret_cast<void*>(hkChatRender), &g_o_chat_render))
								return false;
						}
						if (!g_o_add_message3)
						{
							jmethodID m = find_instance(env, g_c_chathud,
								sdk::mappings::chat_hud_add_message3_name,
								sdk::mappings::chat_hud_add_message3_sig);
							if (!attach(m, reinterpret_cast<void*>(hkAddMessage3), &g_o_add_message3))
								return false;
						}
						// 1.21.10: per-line lambdas carry the alpha. 1.21.4: none
						// of them exist — fall back to the static opacity hook.
						if (!g_o_line_bg && !g_o_opacity)
						{
							jmethodID m = find_instance(env, g_c_chathud,
								sdk::mappings::chat_line_bg_name,
								sdk::mappings::chat_line_bg_sig);
							if (!attach(m, reinterpret_cast<void*>(hkLineBg), &g_o_line_bg))
							{
								jmethodID op = env->GetStaticMethodID(g_c_chathud,
									sdk::mappings::chat_hud_opacity_name,
									sdk::mappings::chat_hud_opacity_sig);
								if (env->ExceptionCheck()) { env->ExceptionClear(); op = nullptr; }
								if (!attach(op, reinterpret_cast<void*>(hkOpacity), &g_o_opacity))
								{
									rlog::logf("bm: chat fail lambdas+opacity lm=%p op=%p",
										(void*)m, (void*)op);
									return false;
								}
								rlog::logf("bm: chat using opacity fallback (1.21.4)");
							}
						}
						if (!g_o_line_text && !g_o_opacity)
						{
							jmethodID m = find_instance(env, g_c_chathud,
								sdk::mappings::chat_line_text_name,
								sdk::mappings::chat_line_text_sig);
							if (!attach(m, reinterpret_cast<void*>(hkLineText), &g_o_line_text))
							{
								jmethodID op = env->GetStaticMethodID(g_c_chathud,
									sdk::mappings::chat_hud_opacity_name,
									sdk::mappings::chat_hud_opacity_sig);
								if (env->ExceptionCheck()) { env->ExceptionClear(); op = nullptr; }
								if (!attach(op, reinterpret_cast<void*>(hkOpacity), &g_o_opacity))
								{
									rlog::logf("bm: chat fail text+opacity tm=%p op=%p",
										(void*)m, (void*)op);
									return false;
								}
								rlog::logf("bm: chat using opacity fallback (1.21.4)");
							}
						}
						rlog::logf("bm: chat ok render=%p add=%p bg=%p text=%p op=%p",
							(void*)g_o_chat_render, (void*)g_o_add_message3,
							(void*)g_o_line_bg, (void*)g_o_line_text, (void*)g_o_opacity);
						return true;
					}

					bool init_gui_hooks(JNIEnv* env)
					{
						if (!g_c_screen)
							g_c_screen = find_global(env, sdk::mappings::screen_class_sig);
						if (!g_c_screen) { rlog::logf("bm: gui fail screen class"); return false; }
						if (!g_c_entry_list)
							g_c_entry_list = find_global(env, sdk::mappings::entry_list_class_sig);
						if (!g_f_children)
							g_f_children = find_field(env, g_c_screen,
								sdk::mappings::screen_children_field,
								sdk::mappings::screen_children_sig);
						if (!g_f_width)
							g_f_width = find_field(env, g_c_screen,
								sdk::mappings::screen_width_field, "I");
						if (!g_f_height)
							g_f_height = find_field(env, g_c_screen,
								sdk::mappings::screen_height_field, "I");
						if (!g_m_list_size)
						{
							jclass lc = sdk::classloader::find_class(env, "java/util/List");
							if (lc)
							{
								g_m_list_size = find_instance(env, lc, "size", "()I");
								g_m_list_get = find_instance(env, lc, "get",
									"(I)Ljava/lang/Object;");
								env->DeleteLocalRef(lc);
							}
						}

						if (!g_o_screen_render)
						{
							jmethodID m = find_instance(env, g_c_screen,
								sdk::mappings::screen_render_name,
								sdk::mappings::screen_render_sig);
							// Optional: if attach fails, render_bg + setScreen
							// still work for the menu background feature.
							if (m && !attach(m, reinterpret_cast<void*>(hkScreenRender),
								&g_o_screen_render))
							{
								rlog::logf("bm: gui warn screen_render m=%p", (void*)m);
							}
						}
						// renderInGameBackground / renderBackground are optional —
						// if either fails (different signature on a version) we still
						// return true so setScreen + screen_render stay active.
						if (!g_o_transparent_bg)
						{
							jmethodID m = find_instance(env, g_c_screen,
								sdk::mappings::screen_transparent_bg_name,
								sdk::mappings::screen_transparent_bg_sig);
							if (m && !attach(m, reinterpret_cast<void*>(hkTransparentBg),
								&g_o_transparent_bg))
							{
								rlog::logf("bm: gui warn transparent_bg m=%p", (void*)m);
							}
						}
						// Screen (class_437) itself cannot be hooked (class copy
						// fails verification). Concrete subclasses are hooked
						// instead — see hkTitleBg / hkProgressRender.
						if (!g_c_title)
							g_c_title = find_global(env, sdk::mappings::title_screen_class_sig);
						if (g_c_title && !g_o_title_bg)
						{
							jmethodID m = find_instance(env, g_c_title,
								sdk::mappings::screen_render_bg_name,
								sdk::mappings::screen_render_bg_sig);
							if (m && !attach(m, reinterpret_cast<void*>(hkTitleBg), &g_o_title_bg))
								rlog::logf("bm: title bg attach failed (custom menu bg off)");
						}
						else if (!g_c_title)
							rlog::logf("bm: title screen class not found");

						if (!g_c_progress)
							g_c_progress = find_global(env, sdk::mappings::progress_screen_class_sig);
						if (g_c_progress && !g_o_progress_render)
						{
							// ProgressScreen declares render (method_25394) only —
							// renderWithTooltip (method_47413) lives on Screen.
							jmethodID m = find_instance(env, g_c_progress,
								sdk::mappings::screen_render_base_name,
								sdk::mappings::screen_render_base_sig);
							if (!m)
								rlog::logf("bm: progress render method not found (gif off on reload)");
							else if (!attach(m, reinterpret_cast<void*>(hkProgressRender),
								&g_o_progress_render))
								rlog::logf("bm: progress render attach failed (gif off on reload)");
						}
						else if (!g_c_progress)
							rlog::logf("bm: progress screen class not found");

						if (!g_c_logo)
							g_c_logo = find_global(env, k_logo_class_sig);
						if (g_c_logo && !g_o_logo_render)
						{
							// LogoRenderer.render(DrawContext, int, float)
							jmethodID m = find_instance(env, g_c_logo, "method_48209",
								"(Lnet/minecraft/class_332;IF)V");
							if (!m)
								rlog::logf("bm: logo render method not found (vanilla logo kept)");
							else if (!attach(m, reinterpret_cast<void*>(hkLogoRender),
								&g_o_logo_render))
								rlog::logf("bm: logo render attach failed (vanilla logo kept)");
						}
						else if (!g_c_logo)
							rlog::logf("bm: logo renderer class not found");

						if (!g_c_splash)
							g_c_splash = find_global(env, k_splash_class_sig);
						if (g_c_splash && !g_o_splash_render)
						{
							// SplashOverlay declares render (method_25394) only.
							jmethodID m = find_instance(env, g_c_splash,
								sdk::mappings::screen_render_base_name,
								sdk::mappings::screen_render_base_sig);
							if (!m)
								rlog::logf("bm: splash render method not found (gif off on boot)");
							else if (!attach(m, reinterpret_cast<void*>(hkSplashRender),
								&g_o_splash_render))
								rlog::logf("bm: splash render attach failed (gif off on boot)");
						}
						else if (!g_c_splash)
							rlog::logf("bm: splash overlay class not found");

						if (!g_c_mc)
							g_c_mc = find_global(env, sdk::mappings::minecraftclass_sig);
						if (g_c_mc)
						{
							if (!g_f_current_screen)
								g_f_current_screen = find_field(env, g_c_mc,
									sdk::mappings::minecraft_screen_name,
									sdk::mappings::minecraft_screen_sig);
							if (!g_f_world)
								g_f_world = find_field(env, g_c_mc,
									sdk::mappings::minecraft_world_field,
									sdk::mappings::minecraft_world_sig);
							if (!g_f_player)
								g_f_player = find_field(env, g_c_mc,
									sdk::mappings::minecraft_player_field,
									sdk::mappings::minecraft_player_sig);
							// setScreen is NOT hooked: calling the original via
							// CallNonvirtualVoidMethod bypasses Fabric Screen API's
							// mixin wrapper and crashes with "screen not initialised".
							// Screen changes are polled in hkScreenRender instead.
						}
						else rlog::logf("bm: gui warn mc class null");
						resolve_blocked();
						// Initialize custom menu background textures/shaders.
						// Lazy: loads menu2.png / stars.png on first draw.
						if (!menu_bg::init())
							rlog::logf("bm: menu_bg init failed (custom bg disabled)");
						rlog::logf("bm: gui ok render=%p trans=%p titlebg=%p progress=%p splash=%p logo=%p flush=%p child=%p w=%d h=%d",
							(void*)g_o_screen_render, (void*)g_o_transparent_bg,
							(void*)g_o_title_bg, (void*)g_o_progress_render,
							(void*)g_o_splash_render, (void*)g_o_logo_render,
							(void*)g_m_ctx_flush,
							(void*)g_f_children, (int)(g_f_width != nullptr),
							(int)(g_f_height != nullptr));
						return true;
					}

					bool init_item_hooks(JNIEnv* env)
					{
						if (!resolve_matrix_ids(env)) return false;

						if (!g_c_hs)
							g_c_hs = find_global(env, sdk::mappings::handled_screen_class_sig);
						if (!g_c_menu)
							g_c_menu = find_global(env, sdk::mappings::screen_handler_class_sig);
						if (!g_c_slot)
							g_c_slot = find_global(env, sdk::mappings::slot_class_sig);
						if (!g_c_hs || !g_c_menu || !g_c_slot) return false;

						if (!g_f_slot_x)
							g_f_slot_x = find_field(env, g_c_slot,
								sdk::mappings::slot_x_field, sdk::mappings::slot_coord_sig);
						if (!g_f_slot_y)
							g_f_slot_y = find_field(env, g_c_slot,
								sdk::mappings::slot_y_field, sdk::mappings::slot_coord_sig);
						if (!g_m_get_slot)
							g_m_get_slot = find_instance(env, g_c_menu,
								sdk::mappings::screen_handler_get_slot_name,
								sdk::mappings::screen_handler_get_slot_sig);

						if (!g_o_draw_slot)
						{
							jmethodID m = find_instance(env, g_c_hs,
								sdk::mappings::hs_draw_slot_name,
								sdk::mappings::hs_draw_slot_sig);
							if (!attach(m, reinterpret_cast<void*>(hkDrawSlot), &g_o_draw_slot))
								return false;
						}
						if (!g_o_draw_cursor)
						{
							jmethodID m = find_instance(env, g_c_hs,
								sdk::mappings::hs_draw_cursor_name,
								sdk::mappings::hs_draw_cursor_sig);
							if (!attach(m, reinterpret_cast<void*>(hkDrawCursor),
								&g_o_draw_cursor))
								return false;
						}
						if (!g_o_click_slot)
						{
							jmethodID m = find_instance(env, g_c_menu,
								sdk::mappings::screen_handler_click_slot_name,
								sdk::mappings::screen_handler_click_slot_sig);
							if (!attach(m, reinterpret_cast<void*>(hkClickSlot),
								&g_o_click_slot))
								return false;
						}
						if (!g_o_draw_item[0])
						{
							jmethodID m = find_instance(env, g_c_draw_ctx,
								sdk::mappings::draw_item_name, sdk::mappings::draw_item_sig);
							if (!attach(m, reinterpret_cast<void*>(hkDrawItem), &g_o_draw_item[0]))
								return false;
						}
						if (!g_o_draw_item[1])
						{
							jmethodID m = find_instance(env, g_c_draw_ctx,
								sdk::mappings::draw_item_z_name, sdk::mappings::draw_item_z_sig);
							if (!attach(m, reinterpret_cast<void*>(hkDrawItemZ), &g_o_draw_item[1]))
								return false;
						}
						if (!g_o_draw_item[2])
						{
							jmethodID m = find_instance(env, g_c_draw_ctx,
								sdk::mappings::draw_item_noent_z_name,
								sdk::mappings::draw_item_noent_z_sig);
							if (!attach(m, reinterpret_cast<void*>(hkDrawItemNoEntZ),
								&g_o_draw_item[2]))
								return false;
						}
						if (!g_o_draw_item[3])
						{
							jmethodID m = find_instance(env, g_c_draw_ctx,
								sdk::mappings::draw_item_noent_name,
								sdk::mappings::draw_item_noent_sig);
							if (!attach(m, reinterpret_cast<void*>(hkDrawItemNoEnt),
								&g_o_draw_item[3]))
								return false;
						}
						return true;
					}

					// ----------------------------------------------------------
					// TAB panel (PlayerListHud) — smooth slide-down animation
					// ----------------------------------------------------------
					bool s_got_tab_anim = false;
					jclass g_c_player_list = nullptr;
					jmethodID g_o_pl_render = nullptr;
					jfieldID g_f_pl_visible = nullptr;
					bool s_tab_was_visible = false;
					long long s_tab_opened_us = 0;

					// Same BACK easing as SmoothGUI: slide down from above over 220ms.
					float tab_disp_since(long long since_us)
					{
						long long age = now_us() - since_us;
						if (age < 0) age = 0;
						float alpha = age >= 220000 ? 1.0f : (float)age / 220000.0f;
						float t = 1.0f - alpha;
						float back = 2.70158f * t * t * t - 1.70158f * t * t;
						return 9.0f * back;
					}

					bool tab_anim_active()
					{
						return !Hook::get_unhooked() && globals::better_minecraft_enabled &&
							globals::bm_gui_anim; // reuse the GUI toggle
					}

					// PlayerListHud.render(...) — wrap with matrix translate so the
					// panel slides down from above when TAB is first pressed.
					void hkPlayerListRender(JNIEnv* env, jobject thiz, jobject ctx,
						jint rows, jobject server, jobject owner)
					{
						static bool s_logged = false;
						if (!s_logged) { s_logged = true; rlog::logf("bm: tab render fired"); }

						jobject st = nullptr;
						float disp = 0.0f;
						bool wrapped = false;
						if (tab_anim_active() && g_f_pl_visible)
						{
							jboolean vis = env->GetBooleanField(thiz, g_f_pl_visible);
							if (env->ExceptionCheck()) { env->ExceptionClear(); vis = JNI_FALSE; }
							bool vis_b = vis == JNI_TRUE;
							if (vis_b && !s_tab_was_visible)
								s_tab_opened_us = now_us(); // just opened — start anim
							s_tab_was_visible = vis_b;
							if (vis_b)
							{
								disp = tab_disp_since(s_tab_opened_us);
								if (disp > 0.01f && resolve_matrix_ids(env))
								{
									st = matrices_of(env, ctx);
									if (st)
									{
										m_push(env, st);
										m_translate(env, st, 0.0f, -disp); // slide from above
										wrapped = true;
									}
								}
							}
						}
						if (g_o_pl_render && g_c_player_list)
						{
							env->CallNonvirtualVoidMethod(thiz, g_c_player_list, g_o_pl_render,
								ctx, rows, server, owner);
							if (env->ExceptionCheck()) env->ExceptionClear();
						}
						if (wrapped)
						{
							m_translate(env, st, 0.0f, disp); // undo
							m_pop(env, st);
							env->DeleteLocalRef(st);
						}
					}

					bool init_tab_hooks(JNIEnv* env)
					{
						if (!g_c_player_list)
							g_c_player_list = find_global(env,
								sdk::mappings::player_list_hud_class_sig);
						if (!g_c_player_list)
						{
							rlog::logf("bm: tab fail player_list class");
							return false;
						}
						if (!g_f_pl_visible)
							g_f_pl_visible = find_field(env, g_c_player_list,
								sdk::mappings::player_list_hud_visible_field,
								sdk::mappings::player_list_hud_visible_sig);
						if (!g_o_pl_render)
						{
							jmethodID m = find_instance(env, g_c_player_list,
								sdk::mappings::player_list_hud_render_name,
								sdk::mappings::player_list_hud_render_sig);
							if (!attach(m, reinterpret_cast<void*>(hkPlayerListRender),
								&g_o_pl_render))
							{
								rlog::logf("bm: tab fail pl_render m=%p", (void*)m);
								return false;
							}
						}
						rlog::logf("bm: tab ok render=%p visible=%p",
							(void*)g_o_pl_render, (void*)g_f_pl_visible);
						return true;
					}
				}

				void zoom_tick()
				{
					static auto s_last = std::chrono::steady_clock::now();
					auto now = std::chrono::steady_clock::now();
					float dt = std::chrono::duration<float>(now - s_last).count();
					s_last = now;
					if (dt < 0.0f || dt > 0.25f) dt = 0.0f;

					const bool on = zoom_active();
					if (!on)
					{
						zoom_state = 1.0f;
						restore_sensitivity();
						return;
					}

					static float s_divisor = globals::bm_zoom_divisor;

					const bool blocked = globals::show_gui ||
						(sdk::instance && sdk::instance->is_screen_open());
					bool held = false;
					if (!blocked && globals::bm_zoom_keybind > 0)
						held = x11_helper::is_key_held(globals::bm_zoom_keybind);

					if (held)
					{
						if (globals::bm_zoom_scroll)
						{
							float wheel = x11_helper::peek_wheel();
							if (wheel != 0.0f)
							{
								s_divisor += wheel;
								if (s_divisor < 2.0f) s_divisor = 2.0f;
								if (s_divisor > 20.0f) s_divisor = 20.0f;
							}
						}
					}
					else
					{
						// Slider owns the base divisor whenever we are not zooming.
						s_divisor = globals::bm_zoom_divisor;
						if (s_divisor < 2.0f) s_divisor = 2.0f;
						if (s_divisor > 20.0f) s_divisor = 20.0f;
					}

					float target = held ? s_divisor : 1.0f;
					if (target < 1.0f) target = 1.0f;
					float rate = globals::bm_zoom_speed * 2.0f;   // 1..20 -> 2..40 / s
					if (rate < 0.5f) rate = 0.5f;
					zoom_state += (target - zoom_state) * (1.0f - expf(-rate * dt));
					if (fabsf(zoom_state - target) < 0.001f) zoom_state = target;
					if (zoom_state < 1.0f) zoom_state = 1.0f;

					if (held && globals::bm_zoom_sensitivity && zoom_state > 1.01f)
						apply_sensitivity(zoom_state);
					else
						restore_sensitivity();
				}

				void zoom_reset()
				{
					zoom_state = 1.0f;
					restore_sensitivity();
				}

				bool init_hooks()
				{
					if (g_ready.load(std::memory_order_acquire)) return true;
					if (!flaway::instance) return false;
					auto env = flaway::instance->get_env();
					auto jvm = flaway::instance->get_java_vm();
					if (!env || !jvm) return false;

					// Shared, idempotent with reach/nametag/fog. Never shut it
					// down here - the central teardown owns that.
					if (jnihook_refcount == 0)
					{
						if (JNIHook_Init(jvm) != JNIHOOK_OK)
						{
							rlog::logf("bm: JNIHook_Init failed");
							return false;
						}
					}

					// Resolve the matrix/DrawContext ids FIRST so the version family
					// is known before the feature inits pick lambda vs opacity etc.
					resolve_matrix_ids(env);
					is_legacy();
					rlog::logf("bm: mappings family = %s (legacy_matrix=%d)",
						g_ver.load() == 2 ? "1.21.4" : "1.21.10+",
						(int)s_matrix_legacy);

					// --- GameRenderer.getFov ---
					if (!s_got_fov)
					{
						jclass cls = sdk::classloader::find_class(env,
							sdk::mappings::game_renderer_class_sig);
						if (cls)
						{
							jmethodID m = find_instance(env, cls,
								sdk::mappings::get_fov_name, sdk::mappings::get_fov_sig);
							if (attach(m, reinterpret_cast<void*>(hkGetFov), &g_o_get_fov))
							{
								g_renderer_cls = reinterpret_cast<jclass>(env->NewGlobalRef(cls));
								s_got_fov = true;
							}
							env->DeleteLocalRef(cls);
						}
					}

					// --- Camera.update (+ helpers) ---
					if (!s_got_cam)
					{
						jclass cls = sdk::classloader::find_class(env,
							sdk::mappings::camera_class_sig);
						if (cls)
						{
							jmethodID m = find_instance(env, cls,
								sdk::mappings::camera_update_name, sdk::mappings::camera_update_sig);
							if (!m)
							{
								m = find_instance(env, cls, sdk::mappings::camera_update_name,
									sdk::mappings::camera_update_sig_legacy);
							}
							if (attach(m, reinterpret_cast<void*>(hkCameraUpdate),
								&g_o_camera_update))
							{
								g_camera_cls = reinterpret_cast<jclass>(env->NewGlobalRef(cls));
								g_m_set_pos = find_instance(env, cls,
									sdk::mappings::camera_set_pos_name,
									sdk::mappings::camera_set_pos_sig);
								g_m_clip = find_instance(env, cls,
									sdk::mappings::camera_clip_to_space_name,
									sdk::mappings::camera_clip_to_space_sig);
								g_m_move_by = find_instance(env, cls,
									sdk::mappings::camera_move_by_name,
									sdk::mappings::camera_move_by_sig);
								s_got_cam = true;
							}
							env->DeleteLocalRef(cls);
						}

						jclass ec = sdk::classloader::find_class(env,
							sdk::mappings::entity_class_sig);
						if (ec)
						{
							g_m_get_cam_pos = find_instance(env, ec,
								sdk::mappings::entity_get_camera_pos_name,
								sdk::mappings::entity_get_camera_pos_sig);
							env->DeleteLocalRef(ec);
						}
						jclass vc = sdk::classloader::find_class(env,
							sdk::mappings::vec3d_class_sig);
						if (vc)
						{
							g_f_vec_x = env->GetFieldID(vc, sdk::mappings::vec3d_x_name, "D");
							g_f_vec_y = env->GetFieldID(vc, sdk::mappings::vec3d_y_name, "D");
							g_f_vec_z = env->GetFieldID(vc, sdk::mappings::vec3d_z_name, "D");
							if (env->ExceptionCheck()) env->ExceptionClear();
							env->DeleteLocalRef(vc);
						}
					}

					// --- vanilla animation ports ---
					if (!s_got_chat_anim)
						s_got_chat_anim = init_chat_hooks(env);
					if (!s_got_gui_anim)
						s_got_gui_anim = init_gui_hooks(env);
					if (!s_got_items_anim)
						s_got_items_anim = init_item_hooks(env);
					if (!s_got_tab_anim)
						s_got_tab_anim = init_tab_hooks(env);

					if (s_got_fov || s_got_cam || s_got_chat_anim ||
						s_got_gui_anim || s_got_items_anim || s_got_tab_anim)
						jnihook_refcount = 1;

					// A missing method id is a permanent version mismatch - stop
					// retrying after a few frames and run with whatever attached
					// (the missing features simply pass vanilla through).
					static int s_attempts = 0;
					if ((s_got_fov && s_got_cam && s_got_chat_anim &&
						s_got_gui_anim && s_got_items_anim && s_got_tab_anim) ||
						++s_attempts > 60)
					{
						g_ready.store(true, std::memory_order_release);
						rlog::logf("bm: hooks fov=%d cam=%d chat=%d gui=%d items=%d tab=%d",
							(int)s_got_fov, (int)s_got_cam, (int)s_got_chat_anim,
							(int)s_got_gui_anim, (int)s_got_items_anim, (int)s_got_tab_anim);
						return true;
					}
					return false;
				}
			}
		}
	}
}
