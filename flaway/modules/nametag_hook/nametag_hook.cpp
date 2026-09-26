#include "nametag_hook.h"
#include "../../flaway.h"
#include <sdk/mappings/mappings.hpp>
#include <sdk/classloader.h>
#include <cstdio>

static jmethodID ORIG_render_label = nullptr;
static jmethodID g_method_id = nullptr;
static jclass g_entity_renderer_class = nullptr;
static bool g_enabled = false;

// JNIHook state is shared with reach_hook / backtrack_hook. NEVER call
// JNIHook_Shutdown() here; only track our own init so the central teardown
// in flaway.cpp can shut the whole thing down once.
static int jnihook_refcount = 0;

// EntityRenderer.renderLabelIfPresent (method_3571) draws the vanilla
// nametag above every entity. Suppressing it while our own ESP plates are
// shown prevents duplicate labels. Verified against the 1.21.10 runtime
// bytecode: renderName (method_3569) simply casts its first arg and calls
// this method, so hooking method_3571 alone covers all labels.
void hkRenderLabel(JNIEnv* env, jobject thiz, jobject text, jobject matrices,
	jobject vertex_consumers, jobject light)
{
	if (g_enabled)
	{
		return;
	}

	if (ORIG_render_label && thiz && g_entity_renderer_class)
	{
		env->CallNonvirtualVoidMethod(thiz, g_entity_renderer_class,
			ORIG_render_label, text, matrices, vertex_consumers, light);
		if (env->ExceptionCheck()) env->ExceptionClear();
	}
}

bool flaway::modules::nametag_hook::init()
{
	if (g_entity_renderer_class != nullptr)
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

	ORIG_render_label = nullptr;

	if (jnihook_refcount == 0)
	{
		jnihook_result_t result = JNIHook_Init(jvm);
		if (result == JNIHOOK_OK)
		{
			jnihook_refcount = 1;
		}
		else
		{
			return false;
		}
	}
	else
	{
		jnihook_refcount++;
	}

	jclass renderer_class = sdk::classloader::find_class(env, sdk::mappings::entity_renderer_class_sig);
	if (!renderer_class)
	{
		return false;
	}

	jmethodID method_id = env->GetMethodID(renderer_class,
		sdk::mappings::render_label_name,
		sdk::mappings::render_label_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();

	if (!method_id)
	{
		env->DeleteLocalRef(renderer_class);
		return false;
	}

	jnihook_result_t result = JNIHook_Attach(method_id, reinterpret_cast<void*>(hkRenderLabel), &ORIG_render_label);
	if (result != JNIHOOK_OK)
	{
		env->DeleteLocalRef(renderer_class);
		return false;
	}

	// Cache the jmethodID for the bound native (kept for the session; the
	// gate-only teardown in shutdown() does NOT unregister it).
	g_method_id = method_id;

	g_entity_renderer_class = reinterpret_cast<jclass>(env->NewGlobalRef(renderer_class));
	env->DeleteLocalRef(renderer_class);

	return g_entity_renderer_class != nullptr;
}

void flaway::modules::nametag_hook::shutdown()
{
	g_enabled = false;

	// GATE-ONLY teardown: the entity renderer class is deliberately NOT
	// restored via JNIHook_Detach — class redefinition at unhook triggers the
	// delta-JRE perfdata guard-page SIGSEGV storm. Keep the native method bound
	// with g_enabled off: hkRenderLabel then pass-throughs to
	// ORIG_render_label, so nametags render normally again. ORIG_render_label
	// and g_entity_renderer_class must stay valid — do NOT null them or delete
	// the ref here.
}

void flaway::modules::nametag_hook::set_enabled(bool enabled)
{
	g_enabled = enabled;
}

bool flaway::modules::nametag_hook::is_initialized()
{
	return g_entity_renderer_class != nullptr;
}
