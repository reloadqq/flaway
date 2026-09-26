#include "flaway.h"
#include "globals/globals.h"
#include "hooks/Hook.h"
#include "gui/GUI.h"
#include "utils/logger.h"
#include "utils/crash_dump.h"
#include "config/config.h"
#include <sdk/includes.h>
#include <sdk/classloader.h>
#include <sdk/minecraft/minecraft.h>
#include <sdk/minecraft/entity/entity.h>
#include "modules/backtrack/backtrack_hook.h"
#include "modules/reach/reach_hook.h"
#include "modules/nametag_hook/nametag_hook.h"
#include "modules/storage_esp/storage_esp.h"
#include "modules/base_finder/base_finder.h"
#include "modules/autosprint/autosprint.h"
#include "modules/esp/esp.h"
#include "modules/aimassist/aimassist.h"
#include "modules/friend_manager/friend_manager.h"
#include "modules/chat_command/chat_command.h"
#include <jnihook.h>
#include "modules/modules.h"
#include <fcntl.h>
#include <unistd.h>
#include <atomic>
#include "flaway/utils/no_log.h"

flaway::instance_t* flaway::instance = nullptr;
flaway::instance_t flaway::g_instance; // zero-initialized .bss
thread_local bool flaway::instance_t::attached = false;

// Definition is inline in flaway.h

// Runs at constructor priority 100 — BEFORE any default-priority
// __attribute__((constructor)) (e.g. entry.cpp flaway_init). At that point
// the .bss object g_instance is guaranteed zeroed but its C++ constructor
// never runs, so we set up the recursive mutex explicitly.
__attribute__((constructor(100)))
static void early_flaway_instance_init()
{
    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr) != 0) return;
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&flaway::g_instance.mtx, &attr);
    pthread_mutexattr_destroy(&attr);
    flaway::instance = &flaway::g_instance;
    crash_dump::init();
    linux_hook::install_reinject_signal();
}

JNIEnv* flaway::instance_t::get_env()
{
    pthread_mutex_lock(&mtx);

    // Lazily locate the JVM in case the library was loaded before the JVM
    // existed (e.g. LD_PRELOAD / early injection). Self-heals once it's up.
    if (!jvm)
    {
        JavaVM* jvm_buf[1];
        jsize count = 0;
        if (JNI_GetCreatedJavaVMs(jvm_buf, 1, &count) == JNI_OK && count > 0)
        {
            jvm = jvm_buf[0];
        }
    }
    if (!jvm) { pthread_mutex_unlock(&mtx); return nullptr; }

    JNIEnv* thread_env = nullptr;
    jint status = jvm->GetEnv((void**)&thread_env, JNI_VERSION_1_8);
    if (status == JNI_EDETACHED)
    {
        status = jvm->AttachCurrentThread((void**)&thread_env, nullptr);
        if (status != JNI_OK) { pthread_mutex_unlock(&mtx); return nullptr; }
        attached = true;
    }
    else if (status == JNI_EVERSION)
    {
        pthread_mutex_unlock(&mtx);
        return nullptr;
    }
    pthread_mutex_unlock(&mtx);
    return thread_env;
}

bool flaway::instance_t::init(JNIEnv* jenv)
{
    fprintf(stderr, "[FLAWAY] init called, jenv=%p\n", (void*)jenv); fflush(stderr);
    if (!jenv) { fprintf(stderr, "[FLAWAY] null jenv!\n"); fflush(stderr); return false; }

    {
        pthread_mutex_lock(&mtx);
        if (initialized) { pthread_mutex_unlock(&mtx); return true; }

        try {
            jenv->GetJavaVM(&jvm);
            fprintf(stderr, "[FLAWAY] jvm=%p\n", (void*)jvm); fflush(stderr);

            if (!logger::init())
            {
                logger::log("[flaway] logger init failed");
            }

            fprintf(stderr, "[FLAWAY] initializing classloader...\n"); fflush(stderr);
            sdk::classloader::init(jenv);
            fprintf(stderr, "[FLAWAY] classloader init OK\n"); fflush(stderr);

            fprintf(stderr, "[FLAWAY] creating minecraft_client instance...\n"); fflush(stderr);
            sdk::instance = std::make_unique<sdk::minecraft_client>();
            fprintf(stderr, "[FLAWAY] minecraft_client instance created\n"); fflush(stderr);

            logger::log("[flaway] instance initialized");

            flaway::config::load_auto();
            flaway::modules::friend_manager::load();
            flaway::modules::chat_command::init();

            initialized = true;
        } catch (std::exception& e) {
            fprintf(stderr, "[FLAWAY] init exception: %s\n", e.what()); fflush(stderr);
            sdk::instance.reset();
            initialized = false;
        } catch (...) {
            fprintf(stderr, "[FLAWAY] init unknown exception\n"); fflush(stderr);
            sdk::instance.reset();
            initialized = false;
        }
        pthread_mutex_unlock(&mtx);
    }
    return initialized.load(std::memory_order_acquire);
}

void flaway::instance_t::shutdown()
{
    pthread_mutex_lock(&mtx);
    if (!initialized) { pthread_mutex_unlock(&mtx); return; }

    try {

    logger::log("[flaway] shutting down");
    flaway::config::save_auto();

    // Gate the render thread before tearing down modules. Without this,
    // MainHook could be mid-run_all() when we destroy module state below.
    Hook::set_unhooked(true);
    pthread_mutex_unlock(&mtx);

    // Wait for the render thread to finish its current frame and exit MainHook
    // before we destroy the state it may be using.
    Hook::wait_render_idle();

    pthread_mutex_lock(&mtx);

    flaway::modules::reach_hook::shutdown();
    flaway::modules::backtrack_hook::shutdown();
    flaway::modules::nametag_hook::shutdown();
    flaway::modules::chat_command::shutdown();
    flaway::modules::storage_esp::shutdown();
    flaway::modules::base_finder::shutdown();
    flaway::modules::autosprint::cleanup();
    flaway::modules::esp::cleanup();
    flaway::modules::aimassist::cleanup();

    try { JNIHook_Shutdown(); } catch (...) {}

    Hook::shutdown();
    Hook::release_gui_resources();
    Hook::wait_gui_shutdown();

    try
    {
        JNIEnv* env = nullptr;
        if (jvm)
        {
            jint status = jvm->GetEnv((void**)&env, JNI_VERSION_1_8);
            if (status == JNI_EDETACHED)
                env = nullptr;
        }
        if (env)
        {
            sdk::classloader::cleanup(env);
            if (env->ExceptionCheck()) env->ExceptionClear();
            if (attached)
            {
                jvm->DetachCurrentThread();
                attached = false;
            }
        }
    }
    catch (std::exception& e)
    {
        fprintf(stderr, "[flaway] shutdown classloader cleanup exception: %s\n", e.what()); fflush(stderr);
    }
    catch (...)
    {
        fprintf(stderr, "[flaway] shutdown classloader cleanup unknown exception\n"); fflush(stderr);
    }

    } catch (std::exception& e) {
        fprintf(stderr, "[flaway] shutdown exception: %s\n", e.what()); fflush(stderr);
    } catch (...) {
        fprintf(stderr, "[flaway] shutdown unknown exception\n"); fflush(stderr);
    }

    try { logger::shutdown(); } catch (...) {}

    jvm = nullptr;
    initialized = false;
    teardown_done = true;
    pthread_mutex_unlock(&mtx);
}

void flaway::instance_t::unhook_all()
{
    // Idempotency guard: unhook can be triggered from two places at once (the
    // GUI button AND a keybind, or a repeated press). Without this, two
    // detached threads would run the destructive teardown concurrently on the
    // same state (double JNIHook_Shutdown / double GUI::shutdown / null refs)
    // -> crash. The atomic exchange makes only the first caller proceed.
    static std::atomic<bool> s_unhook_running{false};
    if (s_unhook_running.exchange(true))
        return;

    crash_dump::set_phase(0);

    pthread_mutex_lock(&mtx);
    if (!initialized)
    {
        s_unhook_running = false;
        pthread_mutex_unlock(&mtx);
        return;
    }

    logger::log("[flaway] unhook_all: completely unloading all hooks and unloading .so");

    // Persist the current config before unloading.
    crash_dump::set_phase(1);
    flaway::config::save_auto();

    // First thing: stop the swap hook from running any module/JNI/ImGui code.
    // The render thread (still inside the installed swap hook) may race with
    // this teardown; from this point MainHook is bypassed and only calls the
    // original swap. This prevents it from touching the classloader globals
    // (find_class / re-init) while we are destroying them.
    logger::log("[flaway] unhook: set_unhooked");
    Hook::set_unhooked(true);

    // CRITICAL: release mtx BEFORE waiting for the render thread. The render
    // thread is mid-MainHook->run_all right now and calls get_env() (which
    // locks this SAME mutex) dozens of times per frame. If we keep holding it,
    // the render thread freezes on its next get_env(), wait_render_idle() spins
    // for 5 s, times out, and the teardown then proceeds while the render
    // thread is frozen mid-frame inside run_all -> use-after-free / death.
    // With the mutex released, the render thread finishes the current frame
    // and exits MainHook (gated by g_unhooked), so wait_render_idle() actually
    // observes an idle render thread.
    pthread_mutex_unlock(&mtx);

    try {

    // Stop the crash-storm source BEFORE anything else: the backtrack hook
    // redefines the JIT-hot ChannelInboundHandlerAdapter.channelRead0, and the
    // packet thread keeps releasing packets through it. That JVM activity
    // triggers the delta-JRE perfdata guard-page SIGSEGVs (observed: a
    // recurring fault storm during combat that raced with teardown and killed
    // the game right after set_unhooked). Backtrack shutdown joins the packet
    // thread and gates channelRead0 to pass through, which silences the storm
    // so the rest of the teardown runs in a quiet JVM. g_hook_mutex serializes
    // against the render thread, which is still inside run_all on this frame.
    crash_dump::set_phase(2);
    logger::log("[flaway] unhook: detaching backtrack hook (storm source)");
    flaway::modules::backtrack_hook::shutdown();

    // Wait until the render thread has left MainHook (in-flight frame done),
    // so the overlay resources below cannot be freed under a running render.
    crash_dump::set_phase(3);
    logger::log("[flaway] unhook: waiting for render idle");
    if (!Hook::wait_render_idle())
    {
        // Render thread is STILL inside MainHook after the bounded wait (e.g.
        // a heavy module scan wedged it for seconds). Proceeding with the
        // destructive teardown would free ImGui/JNI/Java refs under that
        // running frame -> use-after-free SIGSEGV on the render thread (seen:
        // SEGV_ACCERR in libc from the LWJGL swap path right after unhook).
        // Instead bail out gate-only: the cheat is off, the game keeps running
        // normally, and a re-inject re-arms it via SIGCONT + reinit().
        fprintf(stderr, "[flaway] render thread stuck — skipping destructive teardown (gate-only)\n");
        fflush(stderr);
        // Mark initialized=false so a repeated unhook_all() returns early
        // instead of re-entering the destructive phase on partially-torn state.
        // Do NOT reset sdk::instance here — the render thread may still be
        // inside run_all() using it. The gated MainHook will see g_unhooked
        // on its next frame and bail without touching sdk::instance.
        initialized = false;
        teardown_done = true;
        s_unhook_running = false;
        crash_dump::set_phase(14);
        return;
    }

    // Let the JVM settle after the packet/NIO churn stopped before the JVMTI
    // teardown below (RedefineClasses/UnregisterNative/Shutdown are the most
    // crash-prone steps; running them while a fault storm is still winding
    // down was killing the process).
    usleep(300 * 1000);

    } catch (...) {
        s_unhook_running = false;
        crash_dump::set_phase(14);
        return;
    }

    // Re-lock for the destructive phase. The render thread is confirmed out of
    // MainHook and gated by g_unhooked, so it will not contend for the mutex
    // through get_env() anymore.
    pthread_mutex_lock(&mtx);

    // Wrap the destructive phase in try/catch so an exception (or a leftover
    // JNI error) can never leave the recursive mutex locked and the thread
    // attached: a subsequent re-inject would then deadlock on mtx forever and
    // freeze the game. On error we bail to the tail that always unlocks.
    try
    {

    // Detach all module JNIHook hooks while JVM is still alive
    crash_dump::set_phase(4);
    logger::log("[flaway] unhook: detaching reach hook");
    flaway::modules::reach_hook::shutdown();
    crash_dump::set_phase(5);
    logger::log("[flaway] unhook: detaching nametag hook");
    flaway::modules::nametag_hook::shutdown();
    logger::log("[flaway] unhook: detaching chat command hook");
    flaway::modules::chat_command::shutdown();

    // Release cached storage-ESP global refs (classloader cleanup may null
    // the env, so do this while the JVM is still alive).
    crash_dump::set_phase(6);
    logger::log("[flaway] unhook: storage_esp shutdown");
    flaway::modules::storage_esp::shutdown();
    flaway::modules::base_finder::shutdown();

    // Release the injected sprint key if AutoSprint still holds it down.
    crash_dump::set_phase(7);
    logger::log("[flaway] unhook: autosprint cleanup");
    flaway::modules::autosprint::cleanup();

    // Release ESP caches / pinned world global ref.
    crash_dump::set_phase(8);
    logger::log("[flaway] unhook: esp cleanup");
    flaway::modules::esp::cleanup();

    logger::log("[flaway] unhook: aimassist cleanup");
    flaway::modules::aimassist::cleanup();

    // Release entity/status JNI global ref caches.
    {
        JNIEnv* env_cleanup = nullptr;
        if (jvm) {
            jint st = jvm->GetEnv((void**)&env_cleanup, JNI_VERSION_1_8);
            if (st == JNI_EDETACHED) env_cleanup = nullptr;
        }
        if (env_cleanup) sdk::entity_client::cleanup_cache(env_cleanup);
    }

    // Fully tear down JNIHook bookkeeping and remove the ClassFileLoadHook
    // callback so the JVM won't invoke our stale callback during its own
    // shutdown sequence. GATE-ONLY: the module shutdowns above gated their
    // native hooks to pass through (classes are deliberately NOT redefined
    // back — redefinition at unhook triggers the delta-JRE perfdata
    // guard-page SIGSEGV storm). JNIHook_Shutdown no longer reapplies classes.
    crash_dump::set_phase(9);
    logger::log("[flaway] unhook: JNIHook_Shutdown");
    try {
        int jnh_err = JNIHook_Shutdown();
        if (jnh_err != 0) {
            fprintf(stderr, "[flaway] JNIHook_Shutdown returned error %d (thread may not be attached)\n", jnh_err);
            fflush(stderr);
        }
    } catch (...) {
        fprintf(stderr, "[flaway] JNIHook_Shutdown threw exception\n"); fflush(stderr);
    }

    // Gate-only shutdown: set the flag that stops MainHook/JNI/ImGui. The swap
    // and X11 hooks stay installed so a re-inject can re-arm the cheat via
    // SIGCONT + reinit() in h_swap_buffers (removing them here would break the
    // re-inject flow; real hook removal belongs to remove_all_hooks() and only
    // runs when the .so is truly unloaded).
    crash_dump::set_phase(10);
    logger::log("[flaway] unhook: Hook::shutdown");
    Hook::shutdown();

    // Request the deferred ImGui/GL teardown. The render thread still runs
    // through h_swap_buffers while the swap hook is installed, so it picks up
    // the flag on its next swap and runs GUI::shutdown with a current GL
    // context.
    crash_dump::set_phase(11);
    logger::log("[flaway] unhook: release_gui_resources");
    Hook::release_gui_resources();
    // Wait (bounded) for the render thread to consume the deferred shutdown so
    // GUI resources are gone before the classloader/env cleanup below.
    crash_dump::set_phase(12);
    logger::log("[flaway] unhook: waiting for GUI shutdown");
    Hook::wait_gui_shutdown();

    } // end try
    catch (std::exception& e)
    {
        fprintf(stderr, "[flaway] unhook exception: %s\n", e.what()); fflush(stderr);
    }
    catch (...)
    {
        fprintf(stderr, "[flaway] unhook unknown exception\n"); fflush(stderr);
    }

    // Cleanup classloader globals (env may be null if JVM is already dying).
    // We already hold the recursive mutex — get env directly from the JVM
    // without going through get_env() which would re-lock and potentially
    // re-attach a thread that is about to be detached.
    crash_dump::set_phase(13);
    try
    {
        JNIEnv* env = nullptr;
        if (jvm)
        {
            jint status = jvm->GetEnv((void**)&env, JNI_VERSION_1_8);
            if (status == JNI_EDETACHED)
            {
                // Attach this thread to the JVM for classloader cleanup.
                // Safe here: backtrack_hook shutdown already silenced the
                // packet thread storm, so the JVM is quiet.
                jint rc = jvm->AttachCurrentThread((void**)&env, nullptr);
                if (rc != JNI_OK || !env) {
                    fprintf(stderr, "[flaway] unhook: AttachCurrentThread failed (%d), skipping classloader cleanup\n", (int)rc);
                    fflush(stderr);
                    env = nullptr;
                }
            }
        }
        if (env)
        {
            logger::log("[flaway] unhook: classloader cleanup");
            sdk::classloader::cleanup(env);
            if (env->ExceptionCheck()) env->ExceptionClear();
            // Detach only if we attached above (attached is thread_local,
            // reflects THIS thread's state, not the calling thread's).
            if (jvm) jvm->DetachCurrentThread();
        }
    }
    catch (std::exception& e)
    {
        fprintf(stderr, "[flaway] unhook classloader cleanup exception: %s\n", e.what()); fflush(stderr);
    }
    catch (...)
    {
        fprintf(stderr, "[flaway] unhook classloader cleanup unknown exception\n"); fflush(stderr);
    }

    // Signal complete unloading - will unload the .so file
    // NOTE: This function must be called from a single thread when the
    //       .so is being unloaded (by unloadsig or other mechanism)
    // The actual .so unloading will be handled by the unloading mechanism
    logger::log("[flaway] unhook_all completed");

    // Mark teardown complete so a re-inject (SIGCONT seen during the unhook
    // window) only re-arms AFTER the cleanup above has fully finished.
    teardown_done = true;

    // The instance is fully torn down. Mark it uninitialized so a repeated
    // unhook request returns immediately instead of tearing down dead state,
    // and so a re-inject re-runs the normal lazy init from scratch.
    initialized = false;
    s_unhook_running = false;

    crash_dump::set_phase(14);

    // Cleanup logger (must be LAST: all logging above has to be visible)
    try { logger::shutdown(); } catch (...) {}

    pthread_mutex_unlock(&mtx);
}

void flaway::instance_t::reinit()
{
    // Runs on the RENDER thread (from h_swap_buffers) after a re-inject. Only
    // un-gates MainHook and resets the instance so the normal lazy init
    // re-runs on the next frame: classloader::init, a fresh minecraft_client,
    // config reload, and the GUI re-arm (release_gui_resources already set
    // imgui_init=false / g_init_needed=true, so the menu comes back).
    //
    // Deliberately NO JVMTI here: after gate-only unhook the natives
    // (channelRead0 / getEntityInteractionRange / renderLabel) are still bound
    // and passing through, and the module class refs are still valid, so
    // reach/nametag/backtrack re-enable by flipping their gates instead of
    // redefining classes (which would re-trigger the delta-JRE crash storm).
    //
    // Only the render thread calls this (via h_swap_buffers) and only after
    // teardown_done, so it can never race the in-progress unhook cleanup.
    pthread_mutex_lock(&mtx);
    jvm = nullptr;
    initialized = false;
    teardown_done = false;
    Hook::set_unhooked(false);
    pthread_mutex_unlock(&mtx);
}


