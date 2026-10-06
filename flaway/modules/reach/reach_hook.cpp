#include "reach_hook.h"
#include "../../flaway.h"
#include <sdk/mappings/mappings.hpp>
#include <sdk/classloader.h>
#include <cstdio>
#include <atomic>
#include "../../utils/logger.h"

static jmethodID ORIG_getEntityInteractionRange = nullptr;
static jmethodID g_method_id = nullptr;
static jclass g_player_entity_class = nullptr;
// Written by the render thread (reach::run/reset), read by whatever game thread
// performs a reach check - must be atomic, a plain double is a data race.
static std::atomic<double> g_reach_override{-1.0};

jdouble hkGetEntityInteractionRange(JNIEnv *env, jobject thiz)
{
	const double override_dist = g_reach_override.load(std::memory_order_acquire);
	if (override_dist > 0.0)
	{
		return static_cast<jdouble>(override_dist);
	}
	
	if (ORIG_getEntityInteractionRange && thiz && g_player_entity_class)
	{
		jdouble ret = env->CallNonvirtualDoubleMethod(thiz, g_player_entity_class, ORIG_getEntityInteractionRange);
		if (env->ExceptionCheck()) { env->ExceptionClear(); return 3.0; }
		return ret;
	}
	
	return 3.0;
}

// JNIHook state is shared with backtrack_hook (and possibly other modules).
// NEVER call JNIHook_Shutdown() unless ALL modules that use it are done.
// Track our own refcount instead.
static int jnihook_refcount = 0;

bool flaway::modules::reach_hook::init()
{
	if (g_player_entity_class != nullptr)
	{
		return true;
	}

	if (!flaway::instance) return false;
	auto env = flaway::instance->get_env();
	auto jvm = flaway::instance->get_java_vm();
	if (!env || !jvm) 
	{
		return false;
	}

	g_reach_override.store(-1.0, std::memory_order_release);
	ORIG_getEntityInteractionRange = nullptr;

	logger::log("[reach] init start");
	logger::log_rss("reach-before-jnihook");
	// JNIHook_Init is idempotent ("already initialized" -> JNIHOOK_OK), so it is
	// safe to retry it after a later failure. The refcount below is incremented
	// ONLY on the success path - incrementing it up front (as this used to) made
	// it grow by ~60/second whenever init() failed, making the counter useless.
	if (jnihook_refcount == 0)
	{
		jnihook_result_t result = JNIHook_Init(jvm);
		if (result != JNIHOOK_OK)
		{
			logger::log("[reach] JNIHook_Init failed result=" + std::to_string((int)result));
			return false;
		}
	}

	logger::log_rss("reach-before-find-class");
	jclass player_entity_class = sdk::classloader::find_class(env, sdk::mappings::player_entity_class_sig);
	logger::log_rss("reach-after-find-class");
	if (!player_entity_class)
	{

		return false;
	}

	jmethodID method_id = env->GetMethodID(player_entity_class, 
		sdk::mappings::get_entity_interaction_range_name, 
		sdk::mappings::get_entity_interaction_range_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();
	
	if (!method_id)
	{
		env->DeleteLocalRef(player_entity_class);
		return false;
	}

	jnihook_result_t result = JNIHook_Attach(method_id, reinterpret_cast<void*>(hkGetEntityInteractionRange), &ORIG_getEntityInteractionRange);
	if (result != JNIHOOK_OK)
	{
		env->DeleteLocalRef(player_entity_class);
		return false;
	}

	// Cache the jmethodID for the bound native (kept for the session; the
	// gate-only teardown in shutdown() does NOT unregister it).
	g_method_id = method_id;

	g_player_entity_class = reinterpret_cast<jclass>(env->NewGlobalRef(player_entity_class));
	env->DeleteLocalRef(player_entity_class);
	
	if (!g_player_entity_class)
	{
		return false;
	}

	jnihook_refcount++;
	return true;
}

void flaway::modules::reach_hook::shutdown()
{
	g_reach_override.store(-1.0, std::memory_order_release);

	// GATE-ONLY teardown: the PlayerEntity class is deliberately NOT restored
	// via JNIHook_Detach — class redefinition at unhook triggers the delta-JRE
	// perfdata guard-page SIGSEGV storm. We keep the native method bound and
	// flip the override off: hkGetEntityInteractionRange then pass-throughs to
	// ORIG_getEntityInteractionRange, so the game keeps working. ORIG and
	// g_player_entity_class must stay valid (the game calls us on every reach
	// check) — do NOT null them or delete the ref here.
}

void flaway::modules::reach_hook::set_reach(double distance)
{
	g_reach_override.store(distance, std::memory_order_release);
}
