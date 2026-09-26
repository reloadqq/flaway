#pragma once

#include <atomic>
#include <pthread.h>
#include <jni.h>

namespace flaway
{
    struct instance_t
    {
        std::atomic<bool> initialized{false};
        JavaVM* jvm = nullptr;
        thread_local static bool attached;

        // Set true only when unhook_all() has FULLY completed. Guards the
        // re-inject path: a SIGCONT can arrive while teardown is still running
        // (phase 1-3, mtx released); the render thread must not re-arm until
        // teardown_done so it never races the in-progress cleanup.
        std::atomic<bool> teardown_done{false};

        // Plain pthread_mutex_t WITHOUT initializer: the object lives in
        // zero-initialized .bss (no C++ constructor), and the mutex is set up
        // explicitly by flaway::early_instance_init() (constructor priority
        // 100, runs BEFORE entry.cpp's default-priority constructor).
        pthread_mutex_t mtx;

        JNIEnv* get_env();
        JavaVM* get_java_vm() {
            pthread_mutex_lock(&mtx);
            JavaVM* p = jvm;
            pthread_mutex_unlock(&mtx);
            return p;
        }
        bool init(JNIEnv* env);
        void shutdown();
        void unhook_all();
        // Re-arms the cheat after a re-inject (dlopen of an already-loaded .so
        // does NOT rerun the constructor). Only un-gates MainHook and marks the
        // instance uninitialized so the normal lazy init re-runs on the next
        // frame (classloader, minecraft_client, config, GUI). No JVMTI is
        // touched — the native hooks are still bound and gated to pass through,
        // so reach/nametag/backtrack come back by flipping their gates.
        void reinit();
    };

    // Raw pointer (NOT std::unique_ptr): smart-pointer dynamic initializers
    // may not have run yet when entry.cpp's __attribute__((constructor))
    // calls into us during dlopen. The pointer is set eagerly at priority
    // 100, before any default-priority constructor runs.
    extern instance_t* instance;
    extern instance_t g_instance; // zero-initialized .bss storage
}

// Consumed by the render thread in h_swap_buffers to detect a re-inject.
inline std::atomic<int> flaway_reinject_requested{0};
