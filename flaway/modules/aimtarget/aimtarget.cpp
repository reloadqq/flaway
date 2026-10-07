#include "aimtarget.h"

#include "../../flaway.h"
#include "../../globals/globals.h"
#include "../../utils/logger.h"
#include "../friend_manager/friend_manager.h"
#include <sdk/minecraft/minecraft.h>
#include <sdk/minecraft/entity/entity.h>
#include <sdk/minecraft/world/world.h>
#include <sdk/classloader.h>
#include <sdk/mappings/mappings.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <time.h>
#include <vector>

namespace
{
	struct v3
	{
		double x = 0.0;
		double y = 0.0;
		double z = 0.0;
	};

	constexpr double k_aim_reach = 3.0;
	constexpr double k_select_reach = 4.0;
	constexpr float k_lerp = 0.4f;
	constexpr float k_speed_mult = 4.0f;
	constexpr double k_lead_time = 0.075;
	constexpr float k_hold_pitch = 13.0f;
	constexpr float k_hold_yaw = 8.0f;
	constexpr uint64_t k_scan_period_ms = 50;

	struct jni_cache
	{
		bool ready = false;
		jclass c_entity = nullptr;
		jclass c_living = nullptr;
		jclass c_player = nullptr;
		jclass c_mob = nullptr;
		jclass c_animal = nullptr;
		jclass c_box = nullptr;
		jclass c_vec = nullptr;
		jclass c_ctx = nullptr;
		jclass c_shape = nullptr;
		jclass c_fluid = nullptr;
		jclass c_hit_type = nullptr;
		jmethodID m_alive = nullptr;
		jmethodID m_using = nullptr;
		jmethodID m_eye_height = nullptr;
		jmethodID m_entity_id = nullptr;
		jmethodID m_set_yaw = nullptr;
		jmethodID m_set_pitch = nullptr;
		jmethodID m_vec_ctor = nullptr;
		jmethodID m_ctx_ctor = nullptr;
		jmethodID m_hit_type = nullptr;
		jmethodID m_world_raycast = nullptr;
		jfieldID f_bounding_box = nullptr;
		jfieldID f_velocity = nullptr;
		jfieldID f_vx = nullptr;
		jfieldID f_vy = nullptr;
		jfieldID f_vz = nullptr;
		jfieldID f_box[6] = {};
		jfieldID f_outline = nullptr;
		jfieldID f_fluid_none = nullptr;
		jfieldID f_miss = nullptr;
		jobject o_miss = nullptr;
		bool raycast_available = false;
		bool raycast_failed = false;
	};

	jni_cache g;
	jobject g_target = nullptr;
	jobject g_world = nullptr;
	int g_target_id = 0;
	bool g_aim_valid = false;
	v3 g_aim;
	uint64_t g_last_scan = 0;
	uint64_t g_last_frame = 0;

	uint64_t now_ms()
	{
		timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
	}

	float frame_delta(uint64_t now)
	{
		float dt = 0.0f;
		if (g_last_frame)
		{
			dt = (float)(now - g_last_frame) / 50.0f;
			dt = std::max(0.005f, std::min(dt, 1.0f));
		}
		g_last_frame = now;
		return dt;
	}

	float clampf(float v, float lo, float hi)
	{
		return v < lo ? lo : (v > hi ? hi : v);
	}

	double clampd(double v, double lo, double hi)
	{
		return v < lo ? lo : (v > hi ? hi : v);
	}

	float wrap_degrees(float d)
	{
		d = std::fmod(d + 180.0f, 360.0f);
		if (d < 0.0f) d += 360.0f;
		return d - 180.0f;
	}

	v3 look_dir(float yaw, float pitch)
	{
		float yr = yaw * 0.0174532925f;
		float pr = pitch * 0.0174532925f;
		float cp = std::cos(pr);
		return { (double)(std::sin(yr) * cp), (double)(-std::sin(pr)), (double)(std::cos(yr) * cp) };
	}

	jclass keep_class(JNIEnv* env, const char* sig)
	{
		jclass local = sdk::classloader::find_class(env, sig);
		if (!local) return nullptr;
		jclass global = (jclass)env->NewGlobalRef(local);
		env->DeleteLocalRef(local);
		return global;
	}

	jmethodID method(JNIEnv* env, jclass cls, const char* name, const char* sig)
	{
		if (!cls) return nullptr;
		jmethodID mid = env->GetMethodID(cls, name, sig);
		if (env->ExceptionCheck()) env->ExceptionClear();
		return mid;
	}

	jfieldID field(JNIEnv* env, jclass cls, const char* name, const char* sig)
	{
		if (!cls) return nullptr;
		jfieldID fid = env->GetFieldID(cls, name, sig);
		if (env->ExceptionCheck()) env->ExceptionClear();
		return fid;
	}

	bool resolve_world_raycast(JNIEnv* env)
	{
		static const char* candidates[] = {
			"net/minecraft/class_638",
			"net/minecraft/class_1937",
			"net/minecraft/class_1922",
		};
		for (const char* name : candidates)
		{
			jclass local = sdk::classloader::find_class(env, name);
			if (!local) continue;
			jmethodID mid = env->GetMethodID(local, "method_17742",
				"(Lnet/minecraft/class_3959;)Lnet/minecraft/class_3965;");
			if (env->ExceptionCheck()) { env->ExceptionClear(); mid = nullptr; }
			env->DeleteLocalRef(local);
			if (mid) { g.m_world_raycast = mid; return true; }
		}
		return false;
	}

	bool init(JNIEnv* env)
	{
		if (g.ready) return true;

		g.c_entity = keep_class(env, sdk::mappings::entity_class_sig);
		g.c_living = keep_class(env, sdk::mappings::living_entity_class_sig);
		g.c_player = keep_class(env, sdk::mappings::player_entity_class_sig);
		g.c_mob = keep_class(env, "net/minecraft/class_1308");
		g.c_animal = keep_class(env, "net/minecraft/class_1429");
		g.c_box = keep_class(env, sdk::mappings::box_class_sig);
		g.c_vec = keep_class(env, sdk::mappings::vec3d_class_sig);
		g.c_ctx = keep_class(env, "net/minecraft/class_3959");
		g.c_shape = keep_class(env, "net/minecraft/class_3959$class_3960");
		g.c_fluid = keep_class(env, "net/minecraft/class_3959$class_242");
		g.c_hit_type = keep_class(env, "net/minecraft/class_239$class_240");

		if (!g.c_entity || !g.c_living || !g.c_player || !g.c_box || !g.c_vec)
			return false;

		g.m_alive = method(env, g.c_entity, "method_5805", "()Z");
		g.m_using = method(env, g.c_living, "method_6115", "()Z");
		g.m_eye_height = method(env, g.c_entity, "method_5751", "()F");
		g.m_entity_id = method(env, g.c_entity, sdk::mappings::entity_get_id_name, sdk::mappings::entity_get_id_sig);
		g.m_set_yaw = method(env, g.c_entity, sdk::mappings::entity_set_yaw_name, sdk::mappings::entity_set_yaw_sig);
		g.m_set_pitch = method(env, g.c_entity, sdk::mappings::entity_set_pitch_name, sdk::mappings::entity_set_pitch_sig);
		g.m_vec_ctor = method(env, g.c_vec, "<init>", "(DDD)V");
		g.m_hit_type = method(env, g.c_hit_type, "method_17783", "()Lnet/minecraft/class_239$class_240;");

		g.f_bounding_box = field(env, g.c_entity, sdk::mappings::get_bounding_box_name, sdk::mappings::get_bounding_box_sig);
		g.f_velocity = field(env, g.c_entity, sdk::mappings::entity_velocity_name, sdk::mappings::entity_velocity_sig);
		g.f_vx = field(env, g.c_vec, sdk::mappings::vec3d_x_name, sdk::mappings::vec3d_x_sig);
		g.f_vy = field(env, g.c_vec, sdk::mappings::vec3d_y_name, sdk::mappings::vec3d_y_sig);
		g.f_vz = field(env, g.c_vec, sdk::mappings::vec3d_z_name, sdk::mappings::vec3d_z_sig);

		g.f_box[0] = field(env, g.c_box, sdk::mappings::box_min_x_name, sdk::mappings::box_min_x_sig);
		g.f_box[1] = field(env, g.c_box, sdk::mappings::box_min_y_name, sdk::mappings::box_min_y_sig);
		g.f_box[2] = field(env, g.c_box, sdk::mappings::box_min_z_name, sdk::mappings::box_min_z_sig);
		g.f_box[3] = field(env, g.c_box, sdk::mappings::box_max_x_name, sdk::mappings::box_max_x_sig);
		g.f_box[4] = field(env, g.c_box, sdk::mappings::box_max_y_name, sdk::mappings::box_max_y_sig);
		g.f_box[5] = field(env, g.c_box, sdk::mappings::box_max_z_name, sdk::mappings::box_max_z_sig);

		if (g.c_ctx)
		{
			g.m_ctx_ctor = method(env, g.c_ctx, "<init>",
				"(Lnet/minecraft/class_243;Lnet/minecraft/class_243;Lnet/minecraft/class_3959$class_3960;"
				"Lnet/minecraft/class_3959$class_242;Lnet/minecraft/class_1297;)V");
		}
		if (g.c_shape)
			g.f_outline = field(env, g.c_shape, "field_17559", "Lnet/minecraft/class_3959$class_3960;");
		if (g.c_fluid)
			g.f_fluid_none = field(env, g.c_fluid, "field_1348", "Lnet/minecraft/class_3959$class_242;");
		if (g.c_hit_type)
		{
			g.f_miss = env->GetStaticFieldID(g.c_hit_type, "field_1333", "Lnet/minecraft/class_239$class_240;");
			if (env->ExceptionCheck()) env->ExceptionClear();
			if (g.f_miss)
			{
				jobject local = env->GetStaticObjectField(g.c_hit_type, g.f_miss);
				if (env->ExceptionCheck()) env->ExceptionClear();
				if (local)
				{
					g.o_miss = env->NewGlobalRef(local);
					env->DeleteLocalRef(local);
				}
			}
		}
		g.raycast_available = resolve_world_raycast(env) && g.m_ctx_ctor && g.m_vec_ctor &&
			g.f_outline && g.f_fluid_none && g.o_miss && g.m_hit_type;
		if (!g.raycast_available)
			logger::log("[aimtarget] world raycast not resolved, line of sight disabled");

		g.ready = g.m_alive && g.m_entity_id && g.m_set_yaw && g.m_set_pitch &&
			g.f_bounding_box && g.f_box[0] && g.f_box[1] && g.f_box[2] &&
			g.f_box[3] && g.f_box[4] && g.f_box[5];
		return g.ready;
	}

	bool read_box(JNIEnv* env, jobject box, double out[6])
	{
		if (!box) return false;
		out[0] = env->GetDoubleField(box, g.f_box[0]);
		out[1] = env->GetDoubleField(box, g.f_box[1]);
		out[2] = env->GetDoubleField(box, g.f_box[2]);
		out[3] = env->GetDoubleField(box, g.f_box[3]);
		out[4] = env->GetDoubleField(box, g.f_box[4]);
		out[5] = env->GetDoubleField(box, g.f_box[5]);
		if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
		return out[3] >= out[0] && out[4] >= out[1] && out[5] >= out[2];
	}

	jobject get_box(JNIEnv* env, jobject entity)
	{
		if (!entity || !g.f_bounding_box) return nullptr;
		jobject box = env->GetObjectField(entity, g.f_bounding_box);
		if (env->ExceptionCheck()) { env->ExceptionClear(); box = nullptr; }
		return box;
	}

	bool eye_box_dist2(JNIEnv* env, jobject entity, const v3& eye, double* out)
	{
		jobject box = get_box(env, entity);
		if (!box) return false;
		double b[6];
		bool ok = read_box(env, box, b);
		env->DeleteLocalRef(box);
		if (!ok) return false;
		double cx = clampd(eye.x, b[0], b[3]);
		double cy = clampd(eye.y, b[1], b[4]);
		double cz = clampd(eye.z, b[2], b[5]);
		double dx = cx - eye.x, dy = cy - eye.y, dz = cz - eye.z;
		*out = dx * dx + dy * dy + dz * dz;
		return true;
	}

	double ray_box_entry(const v3& o, const v3& d, double len, const double b[6])
	{
		double tmin = -1e300;
		double tmax = 1e300;
		const double mn[3] = { b[0], b[1], b[2] };
		const double mx[3] = { b[3], b[4], b[5] };
		const double oo[3] = { o.x, o.y, o.z };
		const double dd[3] = { d.x, d.y, d.z };
		for (int i = 0; i < 3; i++)
		{
			double lo = mn[i] - 1e-7;
			double hi = mx[i] + 1e-7;
			if (std::fabs(dd[i]) < 1e-12)
			{
				if (oo[i] < lo || oo[i] > hi) return -1.0;
				continue;
			}
			double t1 = (lo - oo[i]) / dd[i];
			double t2 = (hi - oo[i]) / dd[i];
			if (t1 > t2) std::swap(t1, t2);
			if (t1 > tmin) tmin = t1;
			if (t2 < tmax) tmax = t2;
			if (tmin > tmax) return -1.0;
		}
		if (tmax < 0.0 || tmin > len) return -1.0;
		return tmin < 0.0 ? 0.0 : tmin;
	}

	bool los_clear(JNIEnv* env, jobject world, jobject player, const v3& from, const v3& to)
	{
		if (!g.raycast_available || g.raycast_failed) return true;
		if (env->PushLocalFrame(8) != 0) return true;

		bool clear = true;
		jobject start = env->NewObject(g.c_vec, g.m_vec_ctor, from.x, from.y, from.z);
		jobject end = env->NewObject(g.c_vec, g.m_vec_ctor, to.x, to.y, to.z);
		jobject outline = g.f_outline ? env->GetStaticObjectField(g.c_shape, g.f_outline) : nullptr;
		jobject fluid = g.f_fluid_none ? env->GetStaticObjectField(g.c_fluid, g.f_fluid_none) : nullptr;
		jobject ctx = nullptr;
		if (start && end && outline && fluid)
			ctx = env->NewObject(g.c_ctx, g.m_ctx_ctor, start, end, outline, fluid, player);
		if (env->ExceptionCheck()) env->ExceptionClear();

		if (ctx)
		{
			jobject hit = env->CallObjectMethod(world, g.m_world_raycast, ctx);
			if (env->ExceptionCheck())
			{
				env->ExceptionClear();
				g.raycast_failed = true;
				logger::log("[aimtarget] world raycast call failed, line of sight disabled");
			}
			else if (hit)
			{
				jobject type = env->CallObjectMethod(hit, g.m_hit_type);
				if (env->ExceptionCheck()) { env->ExceptionClear(); type = nullptr; }
				if (type)
				{
					clear = env->IsSameObject(type, g.o_miss) ? true : false;
					env->DeleteLocalRef(type);
				}
				env->DeleteLocalRef(hit);
			}
		}
		env->PopLocalFrame(nullptr);
		return clear;
	}

	bool is_alive(JNIEnv* env, jobject entity)
	{
		jboolean ok = env->CallBooleanMethod(entity, g.m_alive);
		if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
		return ok == JNI_TRUE;
	}

	bool is_using_item(JNIEnv* env, jobject player)
	{
		if (!g.m_using) return false;
		jboolean ok = env->CallBooleanMethod(player, g.m_using);
		if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
		return ok == JNI_TRUE;
	}

	double eye_height(JNIEnv* env, jobject entity)
	{
		if (!g.m_eye_height || !entity) return 1.62;
		jfloat h = env->CallFloatMethod(entity, g.m_eye_height);
		if (env->ExceptionCheck()) { env->ExceptionClear(); return 1.62; }
		return (double)h;
	}

	int entity_id(JNIEnv* env, jobject entity)
	{
		jint id = env->CallIntMethod(entity, g.m_entity_id);
		if (env->ExceptionCheck()) { env->ExceptionClear(); return -1; }
		return (int)id;
	}

	bool player_velocity(JNIEnv* env, jobject player, v3* out)
	{
		*out = { 0.0, 0.0, 0.0 };
		if (!g.f_velocity || !g.f_vx) return false;
		jobject vel = env->GetObjectField(player, g.f_velocity);
		if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
		if (!vel) return false;
		out->x = env->GetDoubleField(vel, g.f_vx);
		out->y = env->GetDoubleField(vel, g.f_vy);
		out->z = env->GetDoubleField(vel, g.f_vz);
		if (env->ExceptionCheck()) env->ExceptionClear();
		env->DeleteLocalRef(vel);
		return true;
	}

	double player_speed(JNIEnv* env, jobject player)
	{
		v3 v;
		if (!player_velocity(env, player, &v)) return 0.0;
		return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
	}

	bool is_target_allowed(JNIEnv* env, jobject entity)
	{
		if (g.c_player && env->IsInstanceOf(entity, g.c_player))
		{
			if (!globals::aimtarget_players) return false;
			return globals::aimtarget_friends ||
				!flaway::modules::friend_manager::is_entity_friend(env, entity);
		}
		if (g.c_mob && env->IsInstanceOf(entity, g.c_mob))
			return globals::aimtarget_mobs;
		if (g.c_animal && env->IsInstanceOf(entity, g.c_animal))
			return globals::aimtarget_animals;
		return false;
	}

	bool is_valid_target(JNIEnv* env, jobject entity, jobject player, const v3& eye)
	{
		if (!entity || !player) return false;
		if (env->IsSameObject(entity, player)) return false;
		if (!is_alive(env, entity)) return false;

		double reach = k_select_reach + player_speed(env, player) * 3.0;
		double d2 = 0.0;
		if (!eye_box_dist2(env, entity, eye, &d2)) return false;
		if (d2 > reach * reach) return false;
		return is_target_allowed(env, entity);
	}

	std::string held_item_key(JNIEnv* env, jobject player)
	{
		std::string out;
		jclass pc = env->GetObjectClass(player);
		if (!pc) return out;
		jfieldID inv_fid = field(env, pc, sdk::mappings::player_inventory_name,
			sdk::mappings::player_inventory_sig);
		env->DeleteLocalRef(pc);
		if (!inv_fid) return out;

		jobject inv = env->GetObjectField(player, inv_fid);
		if (env->ExceptionCheck()) { env->ExceptionClear(); inv = nullptr; }
		if (!inv) return out;

		jclass ic = env->GetObjectClass(inv);
		jfieldID sel_fid = ic ? field(env, ic, sdk::mappings::inventory_selected_slot_name,
			sdk::mappings::inventory_selected_slot_sig) : nullptr;
		jmethodID get_stack = ic ? method(env, ic, sdk::mappings::inventory_get_stack_name,
			sdk::mappings::inventory_get_stack_sig) : nullptr;
		if (ic) env->DeleteLocalRef(ic);
		if (sel_fid && get_stack)
		{
			int slot = env->GetIntField(inv, sel_fid);
			if (env->ExceptionCheck()) { env->ExceptionClear(); slot = 0; }
			jobject stack = env->CallObjectMethod(inv, get_stack, slot);
			if (env->ExceptionCheck()) { env->ExceptionClear(); stack = nullptr; }
			if (stack)
			{
				jclass sc = env->GetObjectClass(stack);
				jmethodID get_item = sc ? method(env, sc, sdk::mappings::itemstack_get_item_name,
					sdk::mappings::itemstack_get_item_sig) : nullptr;
				if (sc) env->DeleteLocalRef(sc);
				if (get_item)
				{
					jobject item = env->CallObjectMethod(stack, get_item);
					if (env->ExceptionCheck()) { env->ExceptionClear(); item = nullptr; }
					if (item)
					{
						jclass item_cls = env->GetObjectClass(item);
						jmethodID get_key = item_cls ? method(env, item_cls,
							sdk::mappings::item_get_translation_key_name,
							sdk::mappings::item_get_translation_key_sig) : nullptr;
						if (item_cls) env->DeleteLocalRef(item_cls);
						if (get_key)
						{
							jstring key = (jstring)env->CallObjectMethod(item, get_key);
							if (env->ExceptionCheck()) { env->ExceptionClear(); key = nullptr; }
							if (key)
							{
								const char* ckey = env->GetStringUTFChars(key, nullptr);
								if (ckey)
								{
									out = ckey;
									env->ReleaseStringUTFChars(key, ckey);
								}
								env->DeleteLocalRef(key);
							}
						}
						env->DeleteLocalRef(item);
					}
				}
				env->DeleteLocalRef(stack);
			}
		}
		env->DeleteLocalRef(inv);
		return out;
	}

	bool has_weapon(JNIEnv* env, jobject player)
	{
		std::string key = held_item_key(env, player);
		return key.find("sword") != std::string::npos ||
			key.find("_axe") != std::string::npos ||
			key.find("mace") != std::string::npos;
	}

	bool holding_mace(JNIEnv* env, jobject player)
	{
		return held_item_key(env, player).find("mace") != std::string::npos;
	}

	bool look_hits(JNIEnv* env, jobject world, jobject player, const v3& eye,
		float yaw, float pitch, const double b[6], bool through_walls)
	{
		v3 d = look_dir(yaw, pitch);
		double t = ray_box_entry(eye, d, k_aim_reach, b);
		if (t < 0.0) return false;
		if (through_walls) return true;
		v3 entry = { eye.x + d.x * t, eye.y + d.y * t, eye.z + d.z * t };
		return los_clear(env, world, player, eye, entry);
	}

	bool aim_point(JNIEnv* env, jobject world, jobject player, jobject target,
		const v3& eye, const double b[6], bool through_walls, v3* out)
	{
		bool mace = holding_mace(env, player);
		v3 vel = { 0.0, 0.0, 0.0 };
		if (mace) player_velocity(env, player, &vel);
		v3 aim_eye = { eye.x + vel.x, eye.y + vel.y, eye.z + vel.z };

		double mx = (b[0] + b[3]) * 0.5;
		double mz = (b[2] + b[5]) * 0.5;
		double target_eye_y = b[1] + eye_height(env, target);
		double dist_x = aim_eye.x - mx;
		double dist_y = aim_eye.y - target_eye_y;
		double dist_z = aim_eye.z - mz;
		double dist_to_target_eye = std::sqrt(dist_x * dist_x + dist_y * dist_y + dist_z * dist_z);

		v3 aim_origin = aim_eye;
		double aim_height = aim_eye.y;
		if (mace && dist_to_target_eye > k_aim_reach)
		{
			aim_origin.y = target_eye_y;
			aim_height = target_eye_y;
		}
		double blend_dist = mace ? std::min(dist_to_target_eye, k_aim_reach) : dist_to_target_eye;
		double blend = clampd(blend_dist / k_aim_reach, 0.0, 1.0);
		double aim_y = clampd(aim_height, b[1], b[4]);
		v3 ideal = { mx, b[1] + (aim_y - b[1]) * blend, mz };

		v3 pts[400];
		int n = 0;
		pts[n++] = ideal;
		static const double t[9] = { 0.0, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875, 1.0 };
		for (int a = 0; a < 9 && n < 400; a++)
			for (int c = 0; c < 9 && n < 400; c++)
				for (int d = 0; d < 9 && n < 400; d++)
					if (a == 0 || a == 8 || c == 0 || c == 8 || d == 0 || d == 8)
						pts[n++] = {
							b[0] + (b[3] - b[0]) * t[a],
							b[1] + (b[4] - b[1]) * t[c],
							b[2] + (b[5] - b[2]) * t[d]
						};

		v3 visible[400];
		static const double pads[2] = { 0.0, 0.20000001551382535 };
		for (int pad_i = 0; pad_i < 2; pad_i++)
		{
			double pad = pads[pad_i];
			double limit = k_aim_reach + pad;
			for (int pass = 0; pass < 2; pass++)
			{
				bool world_check = (pass == 0);
				if (pass == 1 && !through_walls) break;

				int k = 0;
				for (int i = 0; i < n && k < 400; i++)
				{
					v3 delta = { pts[i].x - aim_origin.x, pts[i].y - aim_origin.y, pts[i].z - aim_origin.z };
					double len = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
					if (len < 1e-9 || (!mace && len > limit)) continue;
					v3 dir = { delta.x / len, delta.y / len, delta.z / len };
					double trace = mace ? len + pad + 0.01 : limit;
					double hit_t = ray_box_entry(aim_origin, dir, trace, b);
					if (hit_t < 0.0) continue;
					if (world_check)
					{
						v3 entry = { aim_origin.x + dir.x * hit_t, aim_origin.y + dir.y * hit_t, aim_origin.z + dir.z * hit_t };
						if (!los_clear(env, world, player, aim_origin, entry)) continue;
					}
					visible[k++] = pts[i];
				}
				if (k == 0) continue;

				v3 centroid = { 0.0, 0.0, 0.0 };
				for (int i = 0; i < k; i++)
				{
					centroid.x += visible[i].x;
					centroid.y += visible[i].y;
					centroid.z += visible[i].z;
				}
				centroid.x /= k;
				centroid.y /= k;
				centroid.z /= k;

				int best = 0;
				double best_d = 1e30;
				for (int i = 0; i < k; i++)
				{
					double dx = visible[i].x - centroid.x;
					double dy = visible[i].y - centroid.y;
					double dz = visible[i].z - centroid.z;
					double d2 = dx * dx + dy * dy + dz * dz;
					if (d2 < best_d) { best_d = d2; best = i; }
				}
				out->x = visible[best].x - aim_origin.x;
				out->y = visible[best].y - aim_origin.y;
				out->z = visible[best].z - aim_origin.z;
				return true;
			}
		}
		return false;
	}

	jobject crosshair_entity(JNIEnv* env)
	{
		jobject hit = sdk::instance->get_crosshair_target();
		if (!hit) return nullptr;
		jobject out = nullptr;
		jclass cls = sdk::classloader::find_class(env, sdk::mappings::entity_hit_result_class_sig);
		if (cls && env->IsInstanceOf(hit, cls))
		{
			jmethodID mid = env->GetMethodID(cls, sdk::mappings::entity_hit_result_get_entity_name,
				sdk::mappings::entity_hit_result_get_entity_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			if (mid)
			{
				out = env->CallObjectMethod(hit, mid);
				if (env->ExceptionCheck()) { env->ExceptionClear(); out = nullptr; }
			}
		}
		if (cls) env->DeleteLocalRef(cls);
		env->DeleteLocalRef(hit);
		return out;
	}

	jobject find_target(JNIEnv* env, jobject world, jobject player, const v3& eye,
		float yaw, float pitch, bool through_walls)
	{
		v3 look = look_dir(yaw, pitch);
		double best_angle = 1e30;
		jobject best = nullptr;

		sdk::world_client wc(world);
		std::vector<jobject> entities = wc.get_entities();
		for (jobject e : entities)
		{
			if (!e) continue;
			bool keep = env->IsInstanceOf(e, g.c_living) &&
				is_valid_target(env, e, player, eye);
			if (keep)
			{
				double d2 = 0.0;
				keep = eye_box_dist2(env, e, eye, &d2) &&
					(through_walls || d2 <= k_select_reach * k_select_reach);
			}
			if (!keep) { env->DeleteLocalRef(e); continue; }

			jobject box = get_box(env, e);
			double b[6];
			bool got = box && read_box(env, box, b);
			if (box) env->DeleteLocalRef(box);
			if (!got) { env->DeleteLocalRef(e); continue; }

			v3 center = { (b[0] + b[3]) * 0.5, (b[1] + b[4]) * 0.5, (b[2] + b[5]) * 0.5 };
			double dx = center.x - eye.x, dy = center.y - eye.y, dz = center.z - eye.z;
			double len = std::sqrt(dx * dx + dy * dy + dz * dz);
			if (len < 1e-9) { env->DeleteLocalRef(e); continue; }
			double dot = (look.x * dx + look.y * dy + look.z * dz) / len;
			double angle = std::acos(clampd(dot, -1.0, 1.0));
			if (angle * 57.2957795 > (double)globals::aimtarget_fov * 0.5)
			{
				env->DeleteLocalRef(e);
				continue;
			}
			if (angle < best_angle)
			{
				if (best) env->DeleteLocalRef(best);
				best = e;
				best_angle = angle;
			}
			else
			{
				env->DeleteLocalRef(e);
			}
		}
		return best;
	}

	void reset_target(JNIEnv* env)
	{
		if (env && g_target) env->DeleteGlobalRef(g_target);
		g_target = nullptr;
		g_target_id = 0;
		g_aim_valid = false;
	}

	void acquire_target(JNIEnv* env, jobject world, jobject player, const v3& eye,
		float yaw, float pitch, bool through_walls)
	{
		jobject found = nullptr;
		if (globals::triggerbot_enabled)
		{
			found = crosshair_entity(env);
			if (found && !is_valid_target(env, found, player, eye))
			{
				env->DeleteLocalRef(found);
				found = nullptr;
			}
		}
		if (!found)
			found = find_target(env, world, player, eye, yaw, pitch, through_walls);

		int id = found ? entity_id(env, found) : 0;
		if (id != g_target_id)
		{
			reset_target(env);
			if (found && id > 0)
			{
				g_target = env->NewGlobalRef(found);
				g_target_id = id;
				g_aim_valid = false;
			}
		}
		if (found) env->DeleteLocalRef(found);
	}

	void aim_step(JNIEnv* env, jobject world, jobject player, const v3& eye,
		float cur_yaw, float cur_pitch, float dt)
	{
		if (!g_target) return;
		if (is_using_item(env, player)) return;
		if (globals::aimtarget_weapon_only && !has_weapon(env, player)) return;

		bool through_walls = globals::aimtarget_through_walls;
		jobject box = get_box(env, g_target);
		double b[6];
		bool got = box && read_box(env, box, b);
		if (box) env->DeleteLocalRef(box);
		if (!got) return;

		v3 tv = { 0.0, 0.0, 0.0 };
		if (player_velocity(env, g_target, &tv))
		{
			double lx = clampd(tv.x * k_lead_time, -0.5, 0.5);
			double ly = clampd(tv.y * k_lead_time, -0.5, 0.5);
			double lz = clampd(tv.z * k_lead_time, -0.5, 0.5);
			b[0] += lx; b[3] += lx;
			b[1] += ly; b[4] += ly;
			b[2] += lz; b[5] += lz;
		}

		v3 position;
		if (!aim_point(env, world, player, g_target, eye, b, through_walls, &position))
			return;

		if (!g_aim_valid)
		{
			g_aim = position;
			g_aim_valid = true;
		}
		else
		{
			double t = 1.0 - std::pow(1.0 - (double)k_lerp, (double)dt);
			g_aim.x += (position.x - g_aim.x) * t;
			g_aim.y += (position.y - g_aim.y) * t;
			g_aim.z += (position.z - g_aim.z) * t;
		}

		float yaw = wrap_degrees((float)(std::atan2(g_aim.z, g_aim.x) * 57.2957795) - 90.0f);
		float pitch = (float)(-std::atan2(g_aim.y, std::hypot(g_aim.x, g_aim.z)) * 57.2957795);
		float d_yaw = wrap_degrees(yaw - cur_yaw);
		float d_pitch = pitch - cur_pitch;

		if (std::fabs(d_pitch) <= k_hold_pitch && std::fabs(d_yaw) < k_hold_yaw &&
			look_hits(env, world, player, eye, cur_yaw, cur_pitch, b, through_walls))
			d_pitch = 0.0f;

		float eraw = clampf((float)(std::hypot(d_yaw, d_pitch) / 2.0), 0.0f, 1.0f);
		float ease = eraw * eraw * (3.0f - 2.0f * eraw);
		float speed = globals::aimtarget_threshold * k_speed_mult * dt * ease;
		if (speed <= 0.0f) return;
		float denom = std::max(std::fabs(d_yaw), std::fabs(d_pitch) * 2.0f);
		if (denom <= 0.0f) return;
		float step = std::min(1.0f, speed / denom);

		sdk::entity_client ec(player);
		ec.set_yaw(cur_yaw + d_yaw * step);
		if (d_pitch != 0.0f)
			ec.set_pitch(clampf(cur_pitch + d_pitch * step, -90.0f, 90.0f));
	}
}

void flaway::modules::aimtarget::run()
{
	uint64_t now = now_ms();
	float dt = frame_delta(now);

	JNIEnv* env = flaway::instance ? flaway::instance->get_env() : nullptr;
	if (!globals::aimtarget_enabled)
	{
		reset_target(env);
		return;
	}
	if (!env || !sdk::instance) return;
	if (!init(env)) return;
	if (env->PushLocalFrame(8192) != 0) return;

	jobject player = sdk::instance->get_player();
	jobject world = sdk::instance->get_world();
	if (!player || !world)
	{
		reset_target(env);
		env->PopLocalFrame(nullptr);
		return;
	}

	if (g_world && !env->IsSameObject(g_world, world))
	{
		reset_target(env);
		env->DeleteGlobalRef(g_world);
		g_world = nullptr;
	}
	if (!g_world) g_world = env->NewGlobalRef(world);

	sdk::entity_client pc(player);
	v3 eye = { pc.get_x(), pc.get_y() + eye_height(env, player), pc.get_z() };
	float yaw = pc.get_yaw();
	float pitch = pc.get_pitch();
	bool through_walls = globals::aimtarget_through_walls;

	if (g_target && !is_valid_target(env, g_target, player, eye))
		reset_target(env);

	if (now - g_last_scan >= k_scan_period_ms)
	{
		g_last_scan = now;
		acquire_target(env, world, player, eye, yaw, pitch, through_walls);
	}

	if (g_target)
		aim_step(env, world, player, eye, yaw, pitch, dt);

	env->PopLocalFrame(nullptr);
}

void flaway::modules::aimtarget::cleanup()
{
	JNIEnv* env = flaway::instance ? flaway::instance->get_env() : nullptr;
	if (env)
	{
		if (g_target) env->DeleteGlobalRef(g_target);
		if (g_world) env->DeleteGlobalRef(g_world);
		if (g.o_miss) env->DeleteGlobalRef(g.o_miss);
		if (g.c_entity) env->DeleteGlobalRef(g.c_entity);
		if (g.c_living) env->DeleteGlobalRef(g.c_living);
		if (g.c_player) env->DeleteGlobalRef(g.c_player);
		if (g.c_mob) env->DeleteGlobalRef(g.c_mob);
		if (g.c_animal) env->DeleteGlobalRef(g.c_animal);
		if (g.c_box) env->DeleteGlobalRef(g.c_box);
		if (g.c_vec) env->DeleteGlobalRef(g.c_vec);
		if (g.c_ctx) env->DeleteGlobalRef(g.c_ctx);
		if (g.c_shape) env->DeleteGlobalRef(g.c_shape);
		if (g.c_fluid) env->DeleteGlobalRef(g.c_fluid);
		if (g.c_hit_type) env->DeleteGlobalRef(g.c_hit_type);
	}
	g_target = nullptr;
	g_world = nullptr;
	g_target_id = 0;
	g_aim_valid = false;
	g_last_scan = 0;
	g = jni_cache{};
}
