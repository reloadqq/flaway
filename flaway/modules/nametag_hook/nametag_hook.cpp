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

// EntityRenderer.renderLabelIfPresent draws the vanilla nametag above every
// entity. Suppressing it while our own ESP plates are shown prevents duplicate
// labels. The native must mirror the Java descriptor exactly, and the arity
// differs between versions, so both are probed at init.
// 1.21.4: renderLabelIfPresent(EntityRenderState, Text, MatrixStack,
//         VertexConsumerProvider, int light)
void hkRenderLabel(JNIEnv* env, jobject thiz, jobject render_state, jobject text,
	jobject matrices, jobject vertex_consumers, jint light)
{
	if (g_enabled)
	{
		return;
	}

	if (ORIG_render_label && thiz && g_entity_renderer_class)
	{
		env->CallNonvirtualVoidMethod(thiz, g_entity_renderer_class,
			ORIG_render_label, render_state, text, matrices, vertex_consumers, light);
		if (env->ExceptionCheck()) env->ExceptionClear();
	}
}

// 1.21.10: renderLabelIfPresent(EntityRenderState, MatrixStack,
//         OrderedRenderCommandQueue, CameraRenderState)
void hkRenderLabelLegacy(JNIEnv* env, jobject thiz, jobject render_state,
	jobject matrices, jobject queue, jobject camera_state)
{
	if (g_enabled)
	{
		return;
	}

	if (ORIG_render_label && thiz && g_entity_renderer_class)
	{
		env->CallNonvirtualVoidMethod(thiz, g_entity_renderer_class,
			ORIG_render_label, render_state, matrices, queue, camera_state);
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

	// JNIHook_Init is idempotent, so a retry after a later failure is safe.
	// The refcount is incremented ONLY on the success path below - incrementing
	// it up front made it grow every frame while init() kept failing.
	if (jnihook_refcount == 0)
	{
		jnihook_result_t result = JNIHook_Init(jvm);
		if (result != JNIHOOK_OK)
		{
			return false;
		}
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

	void* hook = reinterpret_cast<void*>(hkRenderLabel);

	if (!method_id)
	{
		method_id = env->GetMethodID(renderer_class,
			sdk::mappings::render_label_name_legacy,
			sdk::mappings::render_label_sig_legacy);
		if (env->ExceptionCheck()) env->ExceptionClear();
		hook = reinterpret_cast<void*>(hkRenderLabelLegacy);
	}

	if (!method_id)
	{
		env->DeleteLocalRef(renderer_class);
		return false;
	}

	jnihook_result_t result = JNIHook_Attach(method_id, hook, &ORIG_render_label);
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

	if (!g_entity_renderer_class)
	{
		return false;
	}

	jnihook_refcount++;
	return true;
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
