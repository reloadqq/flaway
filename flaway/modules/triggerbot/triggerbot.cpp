#include "triggerbot.h"
#include "../../flaway.h"
#include "../../globals/globals.h"
#include "../../utils/logger.h"
#include "../../utils/rlog.h"
#include "../friend_manager/friend_manager.h"
#include <sdk/minecraft/minecraft.h>
#include <sdk/minecraft/entity/entity.h>
#include <sdk/classloader.h>
#include "../../hooks/Hook.h"
#include "../stap/stap.h"
#include "../wtap/wtap.h"
#include "../autojumpreset/autojumpreset.h"
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <random>
#include <platform/linux/x11_helper.h>

static uint64_t last_attack_ms = 0;
static std::mt19937 rng_engine(0);
static bool s_sprint_active = false;
static uint64_t s_sprint_time = 0;

static uint64_t now_ms()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void tb_send_key(unsigned int vk, bool down)
{
	x11_helper::send_key_press(vk, down);
}

static bool tb_is_sprinting(JNIEnv* env, jobject player)
{
	jclass cls = sdk::classloader::find_class(env, sdk::mappings::entity_class_sig);
	if (!cls) return false;
	jmethodID mid = env->GetMethodID(cls, sdk::mappings::is_sprinting_name, sdk::mappings::is_sprinting_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();
	env->DeleteLocalRef(cls);
	if (!mid) return false;
	jboolean s = env->CallBooleanMethod(player, mid);
	if (env->ExceptionCheck()) env->ExceptionClear();
	return s == JNI_TRUE;
}

static void tb_set_sprinting(JNIEnv* env, jobject player, bool sprint)
{
	jclass cls = sdk::classloader::find_class(env, sdk::mappings::entity_class_sig);
	if (!cls) return;
	jmethodID mid = env->GetMethodID(cls, sdk::mappings::set_sprinting_name, sdk::mappings::set_sprinting_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();
	env->DeleteLocalRef(cls);
	if (!mid) return;
	env->CallVoidMethod(player, mid, sprint ? JNI_TRUE : JNI_FALSE);
	if (env->ExceptionCheck()) env->ExceptionClear();
}

static void tb_send_sprinting_packet(JNIEnv* env, jobject player)
{
	jclass cls = sdk::classloader::find_class(env, sdk::mappings::clientplayerentity_class_sig);
	if (!cls) return;
	jmethodID mid = env->GetMethodID(cls,
		sdk::mappings::send_sprinting_packet_name, sdk::mappings::send_sprinting_packet_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();
	env->DeleteLocalRef(cls);
	if (!mid) return;
	env->CallVoidMethod(player, mid);
	if (env->ExceptionCheck()) env->ExceptionClear();
}

static void tb_start_sprint_reset(JNIEnv* env)
{
	if (s_sprint_active) return;
	if ((GetAsyncKeyState('W') & 0x8000) == 0) return;
	jobject player = sdk::instance->get_player();
	if (!player) return;
	bool sprinting = tb_is_sprinting(env, player);
	env->DeleteLocalRef(player);
	if (!sprinting) return;
	s_sprint_active = true;
	s_sprint_time = now_ms();
	tb_send_key('W', false);
}

static void tb_tick_sprint_reset()
{
	if (!s_sprint_active) return;
	if (globals::show_gui) { tb_send_key('W', true); s_sprint_active = false; return; }
	if (now_ms() - s_sprint_time >= 60ULL)
	{
		tb_send_key('W', true);
		s_sprint_active = false;
	}
}

static bool tb_is_using_item(JNIEnv* env, jobject player)
{
	jclass lc = sdk::classloader::find_class(env, sdk::mappings::living_entity_class_sig);
	if (!lc) return false;
	jmethodID active_mid = env->GetMethodID(lc,
		sdk::mappings::living_get_active_item_name, sdk::mappings::living_get_active_item_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();
	env->DeleteLocalRef(lc);
	if (!active_mid) return false;
	jobject stack = env->CallObjectMethod(player, active_mid);
	if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
	if (!stack) return false;
	bool using_item = false;
	jclass sc = sdk::classloader::find_class(env, "net/minecraft/class_1799");
	if (sc)
	{
		jmethodID empty_mid = env->GetMethodID(sc,
			sdk::mappings::itemstack_is_empty_name, sdk::mappings::itemstack_is_empty_sig);
		if (env->ExceptionCheck()) env->ExceptionClear();
		if (empty_mid)
		{
			jboolean empty = env->CallBooleanMethod(stack, empty_mid);
			if (env->ExceptionCheck()) env->ExceptionClear();
			using_item = empty != JNI_TRUE;
		}
		env->DeleteLocalRef(sc);
	}
	env->DeleteLocalRef(stack);
	return using_item;
}

static bool tb_touching_water(JNIEnv* env, jobject player)
{
	jclass cls = sdk::classloader::find_class(env, sdk::mappings::entity_class_sig);
	if (!cls) return false;
	jmethodID mid = env->GetMethodID(cls, sdk::mappings::is_touching_water_name,
		sdk::mappings::is_touching_water_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();
	env->DeleteLocalRef(cls);
	if (!mid) return false;
	jboolean w = env->CallBooleanMethod(player, mid);
	if (env->ExceptionCheck()) env->ExceptionClear();
	return w == JNI_TRUE;
}

static bool tb_is_flying(JNIEnv* env, jobject player)
{
	jclass pc = env->GetObjectClass(player);
	if (!pc) return false;
	jfieldID abilities_fid = env->GetFieldID(pc, sdk::mappings::abilities_name,
		sdk::mappings::abilities_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();
	bool flying = false;
	if (abilities_fid)
	{
		jobject ab = env->GetObjectField(player, abilities_fid);
		if (env->ExceptionCheck()) { env->ExceptionClear(); ab = nullptr; }
		if (ab)
		{
			jclass ac = env->GetObjectClass(ab);
			jfieldID fly_fid = ac ? env->GetFieldID(ac, sdk::mappings::fly_name,
				sdk::mappings::fly_sig) : nullptr;
			if (env->ExceptionCheck()) env->ExceptionClear();
			if (fly_fid)
			{
				jboolean v = env->GetBooleanField(ab, fly_fid);
				if (env->ExceptionCheck()) env->ExceptionClear();
				else flying = v == JNI_TRUE;
			}
			if (ac) env->DeleteLocalRef(ac);
			env->DeleteLocalRef(ab);
		}
	}
	env->DeleteLocalRef(pc);
	return flying;
}

static void tb_handle_sprint_before(JNIEnv* env, jobject player)
{
	if (globals::triggerbot_sprint_mode == 3) return;
	if (globals::triggerbot_sprint_mode == 2) return;
	bool sprinting = tb_is_sprinting(env, player);
	if (!sprinting) return;
	if (globals::triggerbot_sprint_mode == 0)
	{
		tb_set_sprinting(env, player, false);
		tb_send_sprinting_packet(env, player);
	}
}

static bool tb_eye_box_dist(JNIEnv* env, jobject entity, double ox, double oy, double oz, double* out)
{
	sdk::entity_client ec(entity);
	jobject box = ec.get_bounding_box();
	if (!box) return false;
	jclass bc = env->GetObjectClass(box);
	if (!bc) { env->DeleteLocalRef(box); return false; }
	static const char* fn[6] = {
		sdk::mappings::box_min_x_name, sdk::mappings::box_min_y_name, sdk::mappings::box_min_z_name,
		sdk::mappings::box_max_x_name, sdk::mappings::box_max_y_name, sdk::mappings::box_max_z_name };
	static const char* fs[6] = {
		sdk::mappings::box_min_x_sig, sdk::mappings::box_min_y_sig, sdk::mappings::box_min_z_sig,
		sdk::mappings::box_max_x_sig, sdk::mappings::box_max_y_sig, sdk::mappings::box_max_z_sig };
	jfieldID f[6];
	for (int i = 0; i < 6; i++)
	{
		f[i] = env->GetFieldID(bc, fn[i], fs[i]);
		if (env->ExceptionCheck()) env->ExceptionClear();
	}
	env->DeleteLocalRef(bc);
	bool ok = f[0] && f[1] && f[2] && f[3] && f[4] && f[5];
	double mn[3], mx[3];
	if (ok)
	{
		mn[0] = env->GetDoubleField(box, f[0]);
		mn[1] = env->GetDoubleField(box, f[1]);
		mn[2] = env->GetDoubleField(box, f[2]);
		mx[0] = env->GetDoubleField(box, f[3]);
		mx[1] = env->GetDoubleField(box, f[4]);
		mx[2] = env->GetDoubleField(box, f[5]);
		if (env->ExceptionCheck()) { env->ExceptionClear(); ok = false; }
	}
	env->DeleteLocalRef(box);
	if (!ok) return false;
	double o[3] = { ox, oy, oz };
	double d2 = 0.0;
	for (int i = 0; i < 3; i++)
	{
		double c = o[i] < mn[i] ? mn[i] : (o[i] > mx[i] ? mx[i] : o[i]);
		double dd = c - o[i];
		d2 += dd * dd;
	}
	*out = std::sqrt(d2);
	return true;
}

static void tb_clear_miss_time(JNIEnv* env)
{
	if (!sdk::instance) return;
	jclass cls = sdk::instance->klass();
	jobject mc = sdk::instance->get_minecraft();
	if (!cls || !mc)
	{
		if (cls) env->DeleteLocalRef(cls);
		if (mc) env->DeleteLocalRef(mc);
		return;
	}
	jfieldID fid = env->GetFieldID(cls, sdk::mappings::attack_cooldown_name,
		sdk::mappings::attack_cooldown_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();
	if (fid)
	{
		env->SetIntField(mc, fid, 0);
		if (env->ExceptionCheck()) env->ExceptionClear();
	}
	env->DeleteLocalRef(cls);
	env->DeleteLocalRef(mc);
}

// Delta-style crit window: airborne AND moving down. Returns true only for
// frames where a hit has a real chance of being a crit server-side.
static bool tb_crit_allowed(JNIEnv* env)
{
	jobject lp = sdk::instance->get_player();
	if (!lp) return false;

	sdk::entity_client lpec(lp);
	bool on_ground = lpec.is_on_ground();
	double fall = lpec.get_fall_distance();
	double vy = lpec.get_velocity_y();
	bool water = tb_touching_water(env, lp);
	bool flying = tb_is_flying(env, lp);
	env->DeleteLocalRef(lp);

	bool env_ok = !water && !flying;
	bool falling = fall > 0.05 || vy < -0.08;
	bool allow = env_ok && !on_ground && falling;

	static uint64_t s_last_log = 0;
	uint64_t t = now_ms();
	if (t - s_last_log >= 50ULL)
	{
		s_last_log = t;
		rlog::logf("tb crit og=%d fall=%.2f vy=%.2f water=%d fly=%d allow=%d",
			(int)on_ground, fall, vy, (int)water, (int)flying, (int)allow);
	}
	return allow;
}

void flaway::modules::triggerbot::run()
{
	static bool seeded = false;
	if (!seeded)
	{
		std::random_device rd;
		rng_engine.seed(rd() ^ (unsigned int)(now_ms() & 0xFFFFFFFF));
		seeded = true;
	}

	if (!globals::triggerbot_enabled)
	{
		last_attack_ms = 0;
		// Re-press W NOW while s_sprint_active is still set. Clearing the flag
		// first (as this used to do) made tb_tick_sprint_reset() early-return,
		// so the injected key-up was never undone and the player stayed unable
		// to walk forward.
		if (s_sprint_active) { tb_send_key('W', true); s_sprint_active = false; }
		return;
	}

	tb_tick_sprint_reset();

	rlog::logf("tb frame enabled=1 jump_only=%d", (int)globals::triggerbot_jump_only);

	auto env = flaway::instance->get_env();
	if (!env) return;

	// Airborne telemetry, independent of the crosshair: every 50ms while the
	// player is off the ground, record fallDistance/velocity. This is what
	// proves the crit window is reachable during a real jump (the old log only
	// sampled airborne frames that happened to have a target under the crosshair).
	if (globals::triggerbot_jump_only)
	{
		static uint64_t s_last_air_log = 0;
		uint64_t tnow = now_ms();
		if (tnow - s_last_air_log >= 50ULL)
		{
			jobject lp = sdk::instance->get_player();
			if (lp)
			{
				sdk::entity_client e(lp);
				if (!e.is_on_ground())
				{
					s_last_air_log = tnow;
					rlog::logf("tb air fall=%.2f vy=%.2f",
						e.get_fall_distance(), e.get_velocity_y());
				}
				env->DeleteLocalRef(lp);
			}
		}
	}

	// Do NOT clear s_sprint_active here: tb_tick_sprint_reset() (called above
	// every frame) re-presses W once the 60 ms window elapses, and it refuses to
	// act while the flag is false.
	jobject target = sdk::instance->get_crosshair_target();
	if (!target) { last_attack_ms = 0; return; }

	jclass entity_hit_cls = sdk::classloader::find_class(env,
		sdk::mappings::entity_hit_result_class_sig);
	if (!entity_hit_cls) { env->DeleteLocalRef(target); return; }

	jboolean is_entity_hit = env->IsInstanceOf(target, entity_hit_cls);
	env->DeleteLocalRef(entity_hit_cls);
	if (!is_entity_hit) { env->DeleteLocalRef(target); last_attack_ms = 0; return; }

	jclass hit_cls = env->GetObjectClass(target);
	if (!hit_cls) { env->DeleteLocalRef(target); return; }

	jmethodID get_entity_mid = env->GetMethodID(hit_cls,
		sdk::mappings::entity_hit_result_get_entity_name,
		sdk::mappings::entity_hit_result_get_entity_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();
	env->DeleteLocalRef(hit_cls);

	jobject hit_entity = nullptr;
	if (get_entity_mid)
	{
		hit_entity = env->CallObjectMethod(target, get_entity_mid);
		if (env->ExceptionCheck()) { env->ExceptionClear(); hit_entity = nullptr; }
	}
	env->DeleteLocalRef(target);

	if (!hit_entity) { last_attack_ms = 0; return; }

	// Self-target check
	jobject player = sdk::instance->get_player();
	if (player)
	{
		sdk::entity_client ec(hit_entity);
		if (ec.is_same_object(player))
		{
			env->DeleteLocalRef(player);
			env->DeleteLocalRef(hit_entity);
			last_attack_ms = 0;
			return;
		}
		env->DeleteLocalRef(player);
	}

	// Friend check
	if (flaway::modules::friend_manager::is_entity_friend(env, hit_entity))
	{
		env->DeleteLocalRef(hit_entity);
		last_attack_ms = 0;
		return;
	}

	// Distance check: eye -> closest point on the target hitbox (vanilla reach
	// semantics). The old point test (feet + 1.0) rejected attacks that vanilla
	// would land at the far edge of reach.
	if (globals::triggerbot_distance > 0.0f)
	{
		sdk::camera_data cam = sdk::instance->get_camera();
		double ox, oy, oz;
		if (cam.valid) { ox = cam.x; oy = cam.y; oz = cam.z; }
		else
		{
			jobject lp = sdk::instance->get_player();
			if (!lp) { env->DeleteLocalRef(hit_entity); last_attack_ms = 0; return; }
			sdk::entity_client pc(lp);
			ox = pc.get_x(); oy = pc.get_y() + 1.62; oz = pc.get_z();
			env->DeleteLocalRef(lp);
		}
		double dist = 0.0;
		if (tb_eye_box_dist(env, hit_entity, ox, oy, oz, &dist) &&
			dist > globals::triggerbot_distance)
		{
			env->DeleteLocalRef(hit_entity);
			last_attack_ms = 0;
			return;
		}
	}

	// Weapon-only check (simplified: just check item translation key)
	if (globals::triggerbot_weapon_only)
	{
		jobject lp = sdk::instance->get_player();
		if (lp)
		{
			bool weapon = false;
			jclass pc = env->GetObjectClass(lp);
			if (pc)
			{
				jfieldID inv_fid = env->GetFieldID(pc,
					sdk::mappings::player_inventory_name, sdk::mappings::player_inventory_sig);
				if (env->ExceptionCheck()) env->ExceptionClear();
				if (inv_fid)
				{
					jobject inv = env->GetObjectField(lp, inv_fid);
					if (inv)
					{
						jclass ic = env->GetObjectClass(inv);
						if (ic)
						{
							jfieldID sel_fid = env->GetFieldID(ic,
								sdk::mappings::inventory_selected_slot_name,
								sdk::mappings::inventory_selected_slot_sig);
							if (env->ExceptionCheck()) env->ExceptionClear();
							if (sel_fid)
							{
								int slot = env->GetIntField(inv, sel_fid);
								jmethodID get_stack_mid = env->GetMethodID(ic,
									sdk::mappings::inventory_get_stack_name,
									sdk::mappings::inventory_get_stack_sig);
								if (env->ExceptionCheck()) env->ExceptionClear();
								if (get_stack_mid)
								{
									jobject stack = env->CallObjectMethod(inv, get_stack_mid, slot);
									if (env->ExceptionCheck()) env->ExceptionClear();
									if (stack)
									{
										jclass sc = env->GetObjectClass(stack);
										if (sc)
										{
											jmethodID get_item_mid = env->GetMethodID(sc,
												sdk::mappings::itemstack_get_item_name,
												sdk::mappings::itemstack_get_item_sig);
											if (env->ExceptionCheck()) env->ExceptionClear();
											if (get_item_mid)
											{
												jobject item = env->CallObjectMethod(stack, get_item_mid);
												if (env->ExceptionCheck()) env->ExceptionClear();
												if (item)
												{
													jclass item_cls = env->GetObjectClass(item);
													if (item_cls)
													{
														jmethodID get_key_mid = env->GetMethodID(item_cls,
															sdk::mappings::item_get_translation_key_name,
															sdk::mappings::item_get_translation_key_sig);
														if (env->ExceptionCheck()) env->ExceptionClear();
														if (get_key_mid)
														{
															jstring key = (jstring)env->CallObjectMethod(item, get_key_mid);
															if (env->ExceptionCheck()) env->ExceptionClear();
															if (key)
															{
																const char* ckey = env->GetStringUTFChars(key, nullptr);
																if (ckey)
																{
																	if (strstr(ckey, "sword") || strstr(ckey, "_axe") ||
																		strstr(ckey, "mace") || strstr(ckey, "trident") ||
																		strstr(ckey, "bow") || strstr(ckey, "crossbow"))
																		weapon = true;
																	env->ReleaseStringUTFChars(key, ckey);
																}
																env->DeleteLocalRef(key);
															}
														}
														env->DeleteLocalRef(item_cls);
													}
													env->DeleteLocalRef(item);
												}
											}
										}
										env->DeleteLocalRef(sc);
									}
									env->DeleteLocalRef(stack);
								}
							}
							env->DeleteLocalRef(ic);
						}
						env->DeleteLocalRef(inv);
					}
				}
				env->DeleteLocalRef(pc);
			}
			env->DeleteLocalRef(lp);
			if (!weapon)
			{
				env->DeleteLocalRef(hit_entity);
				last_attack_ms = 0;
				return;
			}
		}
	}

	// Attack while eating: skip swing if player is using item (eating/drinking)
	{
		jobject lp = sdk::instance->get_player();
		if (lp)
		{
			bool using_item = tb_is_using_item(env, lp);
			env->DeleteLocalRef(lp);
			if (using_item)
			{
				env->DeleteLocalRef(hit_entity);
				last_attack_ms = 0;
				return;
			}
		}
	}

	// Crit gate: swing ONLY while falling after a jump. Never on the ground.
	//
	// Condition (Delta AuraUtil.c(), hardened against JNI/mapping misses):
	//   !isOnGround() && (fallDistance > 0.05 || velocity.y < -0.08)
	//
	// - Delta uses fallDistance > 0 && !isOnGround(). The velocity half covers
	//   the first frames after the apex, where fallDistance has not accumulated
	//   yet (that is why the log showed fall=0.00 on every airborne frame) and
	//   it still requires real downward motion, so a stale isOnGround() alone
	//   can never open the gate while standing.
	// - On the ground fallDistance is 0 and velocity.y is 0, so BOTH halves
	//   fail there: this gate is structurally unable to click on the ground.
	// - Rising half of the jump is intentionally excluded: the server decides
	//   crits from its own fallDistance, and that is only > 0 while falling.
	// - Do NOT reset last_attack_ms here: zeroing it every blocked frame made
	//   timed_out() true on the first allowed frame, so the swing went out at
	//   ~0 charge (weak, no crit) and the option looked dead.
	if (globals::triggerbot_jump_only)
	{
		bool crit = tb_crit_allowed(env);
		if (!crit)
		{
			env->DeleteLocalRef(hit_entity);
			return;
		}
	}

	// Attack strength gate: only swing once the weapon is (almost) fully
	// recharged, so every hit lands at full damage instead of ~80%.
	uint64_t now = now_ms();
	float progress = -1.0f;
	{
		jclass player_entity_cls = sdk::classloader::find_class(env,
			sdk::mappings::player_entity_class_sig);
		if (player_entity_cls)
		{
			jmethodID cooldown_mid = env->GetMethodID(player_entity_cls,
				sdk::mappings::get_attack_cooldown_progress_name,
				sdk::mappings::get_attack_cooldown_progress_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			if (cooldown_mid)
			{
				jobject lp = sdk::instance->get_player();
				if (lp)
				{
					progress = env->CallFloatMethod(lp, cooldown_mid, 0.5f);
					if (env->ExceptionCheck()) { env->ExceptionClear(); progress = -1.0f; }
					env->DeleteLocalRef(lp);
				}
			}
			env->DeleteLocalRef(player_entity_cls);
		}
	}
	// Hard deadline: never stay silent longer than a full weapon recharge,
	// even if the JNI cooldown probe misbehaves on some version.
	bool timed_out = last_attack_ms == 0 || now - last_attack_ms >= 1000ULL;
	bool charged = progress >= 0.9f;
	if (!charged && !timed_out)
	{
		if (globals::debug_logging_enabled)
		{
			static uint64_t s_last_log = 0;
			if (now - s_last_log >= 1000ULL)
			{
				s_last_log = now;
				logger::log("[triggerbot] waiting: progress=" + std::to_string(progress) +
					" elapsed=" + std::to_string((unsigned long long)(now - last_attack_ms)));
			}
		}
		env->DeleteLocalRef(hit_entity);
		return;
	}

	// Anti-spam floor: 80ms minimum between attacks (~12.5 attacks/sec max).
	// This prevents double-clicking but is fast enough to never miss a window.
	uint64_t elapsed = now - last_attack_ms;
	if (elapsed < 80ULL)
	{
		env->DeleteLocalRef(hit_entity);
		return;
	}

	// Sprint handling before swing
	jobject lp = sdk::instance->get_player();
	if (lp)
	{
		tb_handle_sprint_before(env, lp);
		env->DeleteLocalRef(lp);
	}

	// Vanilla startAttack() bails out while missTime > 0 (10 tick lockout after
	// any whiff), which left the trigger dead for half a second after a miss.
	tb_clear_miss_time(env);

	// ATTACK
	rlog::logf("tb ATTACK jump_only=%d", (int)globals::triggerbot_jump_only);
	if (!sdk::instance->do_attack() && globals::debug_logging_enabled)
		logger::log("[triggerbot] doAttack() returned false");
	last_attack_ms = now;

	flaway::modules::stap::on_hit();
	flaway::modules::wtap::on_hit();
	flaway::modules::autojumpreset::on_hit();

	// Legit sprint reset (W-tap) on hit
	if (globals::triggerbot_sprint_mode == 2 && !globals::wtap_enabled)
	{
		tb_start_sprint_reset(env);
	}

	env->DeleteLocalRef(hit_entity);
}

void flaway::modules::triggerbot::cleanup()
{
	last_attack_ms = 0;
	if (s_sprint_active) { tb_send_key('W', true); s_sprint_active = false; }
}
