#include <flaway/flaway.h>
#include "entity.h"
#include <sdk/classloader.h>
#include <sdk/mappings/mappings.hpp>
#include <flaway/utils/rlog.h>

// Cached JNI ids for the Entity class. Method/field ids stay valid for the
// lifetime of the class, so we resolve them once and reuse them every frame
// instead of hammering GetMethodID/GetFieldID for every entity.
namespace
{
	struct entity_jni
	{
		jclass cls = nullptr;
		jmethodID get_x = nullptr;
		jmethodID get_y = nullptr;
		jmethodID get_z = nullptr;
		jmethodID get_yaw = nullptr;
		jmethodID get_pitch = nullptr;
		jmethodID set_yaw = nullptr;
		jmethodID set_pitch = nullptr;
		jmethodID get_id = nullptr;
		jmethodID is_on_ground = nullptr;
		jmethodID set_bounding_box = nullptr;
		jfieldID bounding_box = nullptr;
		jfieldID fall_distance = nullptr;
		jfieldID velocity = nullptr;

		bool init(JNIEnv* env)
		{
			if (cls) return true;

			jclass local = sdk::classloader::find_class(env, sdk::mappings::entity_class_sig);
			if (!local) return false;
			cls = (jclass)env->NewGlobalRef(local);
			env->DeleteLocalRef(local);
			if (!cls) return false;

			get_x = env->GetMethodID(cls, sdk::mappings::entity_get_x_name, sdk::mappings::entity_get_x_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			get_y = env->GetMethodID(cls, sdk::mappings::entity_get_y_name, sdk::mappings::entity_get_y_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			get_z = env->GetMethodID(cls, sdk::mappings::entity_get_z_name, sdk::mappings::entity_get_z_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			get_yaw = env->GetMethodID(cls, sdk::mappings::entity_get_yaw_name, sdk::mappings::entity_get_yaw_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			get_pitch = env->GetMethodID(cls, sdk::mappings::entity_get_pitch_name, sdk::mappings::entity_get_pitch_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			set_yaw = env->GetMethodID(cls, sdk::mappings::entity_set_yaw_name, sdk::mappings::entity_set_yaw_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			set_pitch = env->GetMethodID(cls, sdk::mappings::entity_set_pitch_name, sdk::mappings::entity_set_pitch_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			get_id = env->GetMethodID(cls, sdk::mappings::entity_get_id_name, sdk::mappings::entity_get_id_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			is_on_ground = env->GetMethodID(cls, sdk::mappings::is_on_ground_name, sdk::mappings::is_on_ground_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			set_bounding_box = env->GetMethodID(cls, sdk::mappings::set_bounding_box_name, sdk::mappings::set_bounding_box_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			bounding_box = env->GetFieldID(cls, sdk::mappings::get_bounding_box_name, sdk::mappings::get_bounding_box_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			fall_distance = env->GetFieldID(cls, sdk::mappings::entity_fall_distance_name, sdk::mappings::entity_fall_distance_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();
			velocity = env->GetFieldID(cls, sdk::mappings::entity_velocity_name, sdk::mappings::entity_velocity_sig);
			if (env->ExceptionCheck()) env->ExceptionClear();

			return true;
		}
	};

	entity_jni g_jni;
}

// Cached JNI ids for LivingEntity.hasStatusEffect / StatusEffects.POISON
namespace
{
	struct status_jni
	{
		jclass living_cls = nullptr;
		jclass effects_cls = nullptr;
		jmethodID has_status_effect = nullptr;
		jfieldID poison = nullptr;

		bool init(JNIEnv* env)
		{
			if (living_cls && effects_cls && has_status_effect) return true;

			if (!living_cls) {
				jclass le = sdk::classloader::find_class(env, sdk::mappings::living_entity_class_sig);
				if (!le) return false;
				living_cls = (jclass)env->NewGlobalRef(le);
				env->DeleteLocalRef(le);
				if (!living_cls) return false;
			}
			if (!has_status_effect) {
				has_status_effect = env->GetMethodID(living_cls,
					sdk::mappings::living_entity_has_status_effect_name,
					sdk::mappings::living_entity_has_status_effect_sig);
				if (env->ExceptionCheck()) env->ExceptionClear();
			}

			// resolved lazily (StatusEffects may load after the entity class)
			jclass fx = sdk::classloader::find_class(env, sdk::mappings::status_effects_class_sig);
			if (fx) {
				effects_cls = (jclass)env->NewGlobalRef(fx);
				env->DeleteLocalRef(fx);
				if (effects_cls) {
					poison = env->GetStaticFieldID(effects_cls,
						sdk::mappings::status_effects_poison_field,
						"Lnet/minecraft/class_6880;");
					if (env->ExceptionCheck()) env->ExceptionClear();
				}
			}
			return living_cls && has_status_effect != nullptr;
		}
	};

	status_jni g_status;

	bool ensure_status(JNIEnv* env)
	{
		if (!g_status.init(env)) return false;
		if (!g_status.effects_cls || !g_status.poison) {
			// one retry per class-load; find again (registry may load later)
			jclass fx = sdk::classloader::find_class(env, sdk::mappings::status_effects_class_sig);
			if (!fx) return false;
			// Delete old ref before overwriting to prevent JNI global ref leak
			if (g_status.effects_cls) {
				env->DeleteGlobalRef(g_status.effects_cls);
				g_status.effects_cls = nullptr;
			}
			g_status.effects_cls = (jclass)env->NewGlobalRef(fx);
			env->DeleteLocalRef(fx);
			if (!g_status.effects_cls) return false;
			g_status.poison = env->GetStaticFieldID(g_status.effects_cls,
				sdk::mappings::status_effects_poison_field, "Lnet/minecraft/class_6880;");
			if (env->ExceptionCheck()) env->ExceptionClear();
		}
		return g_status.poison != nullptr;
	}

	void cleanup_entity_jni(JNIEnv* env) {
		if (g_jni.cls) { if (env) env->DeleteGlobalRef(g_jni.cls); g_jni.cls = nullptr; }
		// Reset all method/field IDs to prevent stale references after re-init
		g_jni.get_x = nullptr; g_jni.get_y = nullptr; g_jni.get_z = nullptr;
		g_jni.get_yaw = nullptr; g_jni.get_pitch = nullptr;
		g_jni.set_yaw = nullptr; g_jni.set_pitch = nullptr;
		g_jni.get_id = nullptr; g_jni.is_on_ground = nullptr;
		g_jni.set_bounding_box = nullptr; g_jni.bounding_box = nullptr;
		g_jni.fall_distance = nullptr; g_jni.velocity = nullptr;
		if (g_status.living_cls) { if (env) env->DeleteGlobalRef(g_status.living_cls); g_status.living_cls = nullptr; }
		if (g_status.effects_cls) { if (env) env->DeleteGlobalRef(g_status.effects_cls); g_status.effects_cls = nullptr; }
		g_status.has_status_effect = nullptr; g_status.poison = nullptr;
	}
}

void sdk::entity_client::cleanup_cache(JNIEnv* env) {
	cleanup_entity_jni(env);
}

sdk::entity_client::entity_client(jobject entity)
{
	auto env = flaway::instance->get_env();
	if (env && entity)
		this->entity = env->NewGlobalRef(entity);
	else
		this->entity = nullptr;
}

sdk::entity_client::~entity_client()
{
	if (entity)
	{
		auto env = flaway::instance->get_env();
		if (env) env->DeleteGlobalRef(entity);
	}
}

jobject sdk::entity_client::get_entity()
{
	return entity;
}

jobject sdk::entity_client::get_bounding_box()
{
	auto env = flaway::instance->get_env();
	if (!env || !entity) return nullptr;
	if (!g_jni.init(env) || !g_jni.bounding_box) return nullptr;

	jobject ret = env->GetObjectField(entity, g_jni.bounding_box);
	if (env->ExceptionCheck()) { env->ExceptionClear(); ret = nullptr; }
	return ret;
}

void sdk::entity_client::set_bounding_box(jobject box)
{
	auto env = flaway::instance->get_env();
	if (!env || !entity || !box) return;
	if (!g_jni.init(env) || !g_jni.set_bounding_box) return;

	env->CallVoidMethod(entity, g_jni.set_bounding_box, box);
	if (env->ExceptionCheck()) env->ExceptionClear();
}

bool sdk::entity_client::is_same_object(jobject other)
{
	if (!entity || !other) return false;
	auto env = flaway::instance->get_env();
	if (!env) return false;
	return env->IsSameObject(entity, other);
}

double sdk::entity_client::get_x()
{
	auto env = flaway::instance->get_env();
	if (!env || !entity) return 0.0;
	if (!g_jni.init(env) || !g_jni.get_x) return 0.0;

	jdouble ret = env->CallDoubleMethod(entity, g_jni.get_x);
	if (env->ExceptionCheck()) env->ExceptionClear();
	return ret;
}

double sdk::entity_client::get_y()
{
	auto env = flaway::instance->get_env();
	if (!env || !entity) return 0.0;
	if (!g_jni.init(env) || !g_jni.get_y) return 0.0;

	jdouble ret = env->CallDoubleMethod(entity, g_jni.get_y);
	if (env->ExceptionCheck()) env->ExceptionClear();
	return ret;
}

double sdk::entity_client::get_z()
{
	auto env = flaway::instance->get_env();
	if (!env || !entity) return 0.0;
	if (!g_jni.init(env) || !g_jni.get_z) return 0.0;

	jdouble ret = env->CallDoubleMethod(entity, g_jni.get_z);
	if (env->ExceptionCheck()) env->ExceptionClear();
	return ret;
}

float sdk::entity_client::get_yaw()
{
	auto env = flaway::instance->get_env();
	if (!env || !entity) return 0.0f;
	if (!g_jni.init(env) || !g_jni.get_yaw) return 0.0f;

	jfloat ret = env->CallFloatMethod(entity, g_jni.get_yaw);
	if (env->ExceptionCheck()) env->ExceptionClear();
	return ret;
}

float sdk::entity_client::get_pitch()
{
	auto env = flaway::instance->get_env();
	if (!env || !entity) return 0.0f;
	if (!g_jni.init(env) || !g_jni.get_pitch) return 0.0f;

	jfloat ret = env->CallFloatMethod(entity, g_jni.get_pitch);
	if (env->ExceptionCheck()) env->ExceptionClear();
	return ret;
}

void sdk::entity_client::set_yaw(float yaw)
{
	auto env = flaway::instance->get_env();
	if (!env || !entity) return;
	if (!g_jni.init(env) || !g_jni.set_yaw) return;

	env->CallVoidMethod(entity, g_jni.set_yaw, yaw);
	if (env->ExceptionCheck()) env->ExceptionClear();
}

void sdk::entity_client::set_pitch(float pitch)
{
	auto env = flaway::instance->get_env();
	if (!env || !entity) return;
	if (!g_jni.init(env) || !g_jni.set_pitch) return;

	env->CallVoidMethod(entity, g_jni.set_pitch, pitch);
	if (env->ExceptionCheck()) env->ExceptionClear();
}

bool sdk::entity_client::is_on_ground()
{
	auto env = flaway::instance->get_env();
	if (!env || !entity) return false;
	if (!g_jni.init(env) || !g_jni.is_on_ground) return false;

	jboolean ret = env->CallBooleanMethod(entity, g_jni.is_on_ground);
	if (env->ExceptionCheck()) env->ExceptionClear();
	return ret == JNI_TRUE;
}

double sdk::entity_client::get_fall_distance()
{
	auto env = flaway::instance->get_env();
	if (!env || !entity) return 0.0;
	if (!g_jni.init(env) || !g_jni.fall_distance) return 0.0;

	jdouble ret = env->GetDoubleField(entity, g_jni.fall_distance);
	if (env->ExceptionCheck()) env->ExceptionClear();
	return ret;
}

jobject sdk::entity_client::get_velocity()
{
	auto env = flaway::instance->get_env();
	if (!env || !entity) return nullptr;
	if (!g_jni.init(env) || !g_jni.velocity) return nullptr;

	jobject velocity = env->GetObjectField(entity, g_jni.velocity);
	if (env->ExceptionCheck()) env->ExceptionClear();
	return velocity;
}

double sdk::entity_client::get_velocity_y()
{
	auto env = flaway::instance->get_env();
	if (!env || !entity) return 0.0;
	if (!g_jni.init(env) || !g_jni.velocity) return 0.0;

	jobject velocity = env->GetObjectField(entity, g_jni.velocity);
	if (env->ExceptionCheck()) { env->ExceptionClear(); if (velocity) env->DeleteLocalRef(velocity); return 0.0; }
	if (!velocity) return 0.0;

	jclass vc = sdk::classloader::find_class(env, sdk::mappings::vec3d_class_sig);
	if (!vc) { env->DeleteLocalRef(velocity); return 0.0; }
	jfieldID y_fid = env->GetFieldID(vc, sdk::mappings::vec3d_y_name, sdk::mappings::vec3d_y_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();
	double ret = 0.0;
	if (y_fid) ret = env->GetDoubleField(velocity, y_fid);
	if (env->ExceptionCheck()) env->ExceptionClear();
	env->DeleteLocalRef(vc);
	env->DeleteLocalRef(velocity);
	return ret;
}

int sdk::entity_client::get_entity_id()
{
	auto env = flaway::instance->get_env();
	if (!env || !entity) return -1;
	if (!g_jni.init(env) || !g_jni.get_id) return -1;

	jint id = env->CallIntMethod(entity, g_jni.get_id);
	if (env->ExceptionCheck()) env->ExceptionClear();
	return id;
}

bool sdk::entity_client::has_poison()
{
	auto env = flaway::instance->get_env();
	if (!env || !entity) return false;
	if (!ensure_status(env) || !g_status.has_status_effect) {
		static bool s_logged = false;
		if (!s_logged) {
			s_logged = true;
			rlog::logf("hud: status effect JNI unavailable (method=%d effects=%d poison=%d)",
			           g_status.has_status_effect != nullptr,
			           g_status.effects_cls != nullptr, g_status.poison != nullptr);
		}
		return false;
	}

	jobject registry_entry = env->GetStaticObjectField(g_status.effects_cls, g_status.poison);
	if (env->ExceptionCheck()) env->ExceptionClear();
	if (!registry_entry) return false;

	jboolean ret = env->CallBooleanMethod(entity, g_status.has_status_effect, registry_entry);
	env->DeleteLocalRef(registry_entry);
	if (env->ExceptionCheck()) env->ExceptionClear();
	return ret == JNI_TRUE;
}
