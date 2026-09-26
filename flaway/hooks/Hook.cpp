#include "Hook.h"
#include "../flaway.h"
#include "../gui/GUI.h"
#include "../globals/globals.h"
#include "../utils/logger.h"
#include "../utils/crash_dump.h"
#include <dlfcn.h>
#include <atomic>
#include <mutex>
#include <cstring>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <ctime>
#include "../../platform/linux/inline_hook.h"
#include <exception>
#include "../../platform/linux/x11_helper.h"
#include "../modules/modules.h"
#include "../gui/glass_blur.h"
#include "flaway/utils/no_log.h"
#include <link.h>

static int s_hook_diag_fd = -1;
static void diag(const char* msg) {
    if (s_hook_diag_fd < 0) {
        const char* home = getenv("HOME");
        if (!home) return;
        char path[512];
        snprintf(path, sizeof(path), "%s/.minecraft/flaway_diag.txt", home);
        s_hook_diag_fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    }
    if (s_hook_diag_fd >= 0) {
        write(s_hook_diag_fd, msg, strlen(msg));
        write(s_hook_diag_fd, "\n", 1);
    }
}

namespace x11_helper {
    void init();
    bool check_toggle();
    unsigned long get_window_handle();
    void get_window_dimensions(int* width, int* height);
    Display* get_display();
}

namespace linux_hook {
    void set_unhooked(bool value);
    bool get_unhooked();
}

// Set by the injector (gdb) on a RE-inject: gdb sends SIGCONT when it detaches
// and stops the game threads; the SIGCONT handler below records that here when
// the cheat was previously unhooked. The render thread consumes it in
// h_swap_buffers and re-arms the cheat (flaway::reinit). Defined in
// flaway.cpp; extern "C" so a future gdb `set variable` could also drive it.
// Definition is inline in flaway.h

namespace Hook {
    bool init() { return linux_hook::init(); }
    void shutdown() {
        // Gate-only teardown: keep the swap/X11 hooks INSTALLED. The .so stays
        // loaded (mono.sh only dlopens it), and a re-inject re-arms the cheat
        // via SIGCONT -> flaway_reinject_requested -> reinit() inside
        // h_swap_buffers. Removing the hooks here would break that: the render
        // thread would call the real swap directly and nobody would ever
        // consume flaway_reinject_requested, so the cheat could not come back
        // until a full game restart. Hook removal only happens in
        // remove_all_hooks(), which is used when the .so is truly unloaded.
        linux_hook::set_unhooked(true);
    }
    void set_unhooked(bool value) { linux_hook::set_unhooked(value); }
    bool get_unhooked() { return linux_hook::get_unhooked(); }
    bool get_is_init() { return GUI::get_is_init(); }
    float get_game_fps() { return linux_hook::get_game_fps(); }
    unsigned long get_window() { return x11_helper::get_window_handle(); }
    bool wait_render_idle() { return linux_hook::wait_render_idle(); }
    void wait_gui_shutdown() { linux_hook::wait_gui_shutdown(); }
    void release_gui_resources() { linux_hook::release_gui_resources(); }
}

namespace linux_hook {
    typedef void (*swap_buffers_t)(void* dpy, unsigned long drawable);
    static swap_buffers_t o_swap_buffers = nullptr;
    static void* s_opened_lib = nullptr;
    static std::atomic<bool> imgui_init{false};
    static std::atomic<bool> g_init_needed{false};

    void reset_fbo_cache() {}

    // Set once when the cheat is fully unloaded. From that point the swap hook
    // must NOT call MainHook (which runs JNI modules and the GUI) because the
    // classloader globals have been released. It simply forwards to the real
    // swap function so the game keeps presenting frames.
    static std::atomic<bool> g_unhooked{false};
    void set_unhooked(bool value) { g_unhooked.store(value, std::memory_order_release); }
    bool get_unhooked() { return g_unhooked.load(std::memory_order_acquire); }

    // Re-inject detection. gdb (mono.sh) stops every thread ("set
    // scheduler-locking on"), dlopens flaway.so, then detaches — which sends
    // SIGCONT to resume them. The FIRST inject ignores it (g_unhooked false);
    // a re-inject after unhook flips the flag so the render thread re-arms the
    // cheat on its next swap. The handler only does a volatile store — safe
    // from any thread and async-signal-safe.
    static void reinject_sigcont_handler(int)
    {
        if (g_unhooked.load(std::memory_order_acquire))
            flaway_reinject_requested.store(1, std::memory_order_release);
    }
    void install_reinject_signal()
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = reinject_sigcont_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        if (sigaction(SIGCONT, &sa, nullptr) != 0)
        {
            logger::log_error("[HOOK] failed to install SIGCONT re-inject handler");
        }
    }

    // True if the swap hook was successfully installed (guards init() against
    // running the install twice).
    static bool s_hooks_installed = false;
    // The function we inline-hooked (glfwSwapBuffers / glXSwapBuffers /
    // eglSwapBuffers). Stored so remove_all_hooks() can restore the original
    // bytes before the .so is unloaded.
    static void* s_swap_target = nullptr;

    // True game render-loop FPS (see h_swap_buffers).
    static float s_game_fps = 0.0f;
    float get_game_fps() { return s_game_fps; }

    // Serializes the render thread's MainHook with the teardown in
    // unhook_all(): teardown sets g_unhooked, then takes this mutex, which
    // waits for any in-flight MainHook to finish before resources are freed.
    static pthread_mutex_t g_render_mutex = PTHREAD_MUTEX_INITIALIZER;
    // Bounded wait: if the render thread is wedged inside MainHook (e.g. a
    // module call blocked), a plain lock would hang unhook_all() forever and
    // the game would appear frozen. Trylock in a loop and give up after a few
    // seconds with a warning rather than deadlock the teardown.
    static std::atomic<bool> g_gui_shutdown_done{false};
    bool wait_render_idle()
    {
        for (int i = 0; i < 50; i++)
        {
            if (pthread_mutex_trylock(&g_render_mutex) == 0)
            {
                pthread_mutex_unlock(&g_render_mutex);
                return true;
            }
            usleep(100 * 1000);
        }
        fprintf(stderr, "[HOOK] wait_render_idle: render thread stuck, proceeding anyway\n");
        fflush(stderr);
        return false;
    }
    void wait_gui_shutdown()
    {
        for (int i = 0; i < 15; i++)
        {
            if (g_gui_shutdown_done) return;
            usleep(100 * 1000);
        }
        fprintf(stderr, "[HOOK] wait_gui_shutdown: timed out, proceeding anyway\n");
        fflush(stderr);
    }
    void release_gui_resources(); // defined below

    // Hook type — we need to know if we hooked glfw or raw GLX/EGL
    enum HookType { HOOK_NONE, HOOK_GLFW, HOOK_GLX, HOOK_EGL };
    static HookType s_hook_type = HOOK_NONE;

    // GLFW context
    static void* g_glfw_window = nullptr;
    // GLFW library handle (for resolving X11 helpers)
    static void* s_glfw_lib = nullptr;
    // Function pointers for X11 extraction from GLFW
    static Display* (*g_glfwGetX11Display)(void*) = nullptr;
    static unsigned long (*g_glfwGetX11Window)(void*) = nullptr;
    // Cached Minecraft X11 connection (from GLFW)
    static Display* g_minecraft_dpy = nullptr;
    static Window g_minecraft_window = 0;
    static bool g_have_minecraft_x11 = false;

    // ImGui/GL teardown must run on the RENDER thread: it calls
    // ImGui_ImplOpenGL3_Shutdown() which issues glDelete* calls, and those
    // require a current GL context (present only on the render thread during
    // swap). release_gui_resources() (teardown thread) sets this flag; the next
    // h_swap_buffers on the render thread performs the actual shutdown.
    static std::atomic<bool> g_gui_shutdown_pending{false};
    // Last drawable for glXSwapBuffers (must pass it through to the original)
    static unsigned long g_last_drawable = 0;

    // glfwSetInputMode (resolved from libglfw) so we can release/recapture the
    // cursor when the menu opens/closes. Without this the game keeps the cursor
    // warped to the window center and menu buttons cannot be clicked.
    typedef void (*glfwSetInputMode_t)(void*, int, int);
    static glfwSetInputMode_t g_glfwSetInputMode = nullptr;

    static void set_cursor_capture(bool capture) {
        if (!g_glfw_window || !g_glfwSetInputMode) return;
        // GLFW_CURSOR mode (0x00033001) with GLFW_CURSOR_NORMAL (0x00034001) /
        // GLFW_CURSOR_DISABLED (0x00034003) values.
        g_glfwSetInputMode(g_glfw_window, 0x00033001,
            capture ? 0x00034003 : 0x00034001);
    }

    static void ensure_minecraft_x11();

     	bool MainHook() {

		// Lazy JVM + cheat initialization on the FIRST frame.
		//
		// The .so constructor (dlopen via gdb) intentionally does NOT touch
		// the JVM: during injection every game thread is SIGSTOPped
		// ("set scheduler-locking on"), so AttachCurrentThread/FindClass from
		// the constructor can deadlock against the stopped JVM (observed: the
		// launcher then killed the game process). By the time this first swap
		// frame runs, gdb is detached and all threads are live, so JNI is safe.
		// Gated by !get_unhooked(): after unhook the modules were torn down and
		// must NOT be re-attached behind the teardown's back (that re-hooking
		// caused SIGSEGVs on the Netty thread right after unhook_all).
		if (flaway::instance && !flaway::instance->initialized.load(std::memory_order_acquire)
		    && !linux_hook::get_unhooked())
		{
			JavaVM* jvm_buf[1];
			jsize jvm_count = 0;
			if (JNI_GetCreatedJavaVMs(jvm_buf, 1, &jvm_count) == JNI_OK && jvm_count > 0)
			{
				JNIEnv* env = nullptr;
				if (jvm_buf[0]->GetEnv((void**)&env, JNI_VERSION_1_8) == JNI_EDETACHED)
					jvm_buf[0]->AttachCurrentThread((void**)&env, nullptr);
				if (env)
				{
					fprintf(stderr, "[DEBUG] First frame: calling flaway::init\n"); fflush(stderr);
					if (flaway::instance->init(env))
					{
						fprintf(stderr, "[DEBUG] flaway::init OK (lazy)\n"); fflush(stderr);
						diag("[5] flaway::init OK");
					}
					else
					{
						fprintf(stderr, "[DEBUG] flaway::init failed (lazy)\n"); fflush(stderr);
						diag("[5] flaway::init FAILED");
					}
				}
				else
				{
					fprintf(stderr, "[DEBUG] No JNIEnv on first frame, will retry\n"); fflush(stderr);
				}
			}
			else
			{
				fprintf(stderr, "[DEBUG] No JavaVM yet on first frame, will retry\n"); fflush(stderr);
			}
		}


		// After unhook the .so must not touch JNI/ImGui anymore. Just fall
		// through to the original swap so the game keeps rendering.
		if (get_unhooked()) return false;

		bool toggle = x11_helper::check_toggle();

        // Poll the server keymap every frame so module keybinds fire even when
        // the game never delivers key events to its event loop.
        x11_helper::scan_key_events();

		if (toggle) {
			if (imgui_init) {
				globals::show_gui = !globals::show_gui;
				if (globals::show_gui) {
					// Release the cursor so menu buttons can be clicked.
					set_cursor_capture(false);
				} else {
					// Recapture the cursor and cancel any in-progress keybind capture.
					set_cursor_capture(true);
					GUI::cancel_keybind_capture();
				}
				fprintf(stderr, "[DEBUG] GUI toggle: %d\n", (int)globals::show_gui);
			} else {
				// Lazy init is not finished (or failed earlier and needs a retry).
				// Re-assert init_needed unconditionally so a stuck "pending init"
				// state can never deadlock the menu: every press either retries
				// the init or, once ready, toggles the GUI.
				g_init_needed = true;
				fprintf(stderr, "[DEBUG] Toggle queued: init retry (imgui_init=%d)\n", (int)imgui_init);
			}
			logger::log("[hook] toggle pressed, imgui_init=" + std::to_string((int)imgui_init) + " show_gui=" + std::to_string((int)globals::show_gui));
			fflush(stderr);
		}

		// The game re-captures the cursor on every frame while in-game, so the
		// one-shot release at toggle time gets overwritten and the cursor keeps
		// disappearing right after the menu opens. Re-assert the free cursor
		// each frame while the GUI is shown; the game's own capture re-engages
		// normally once the menu closes.
		if (GUI::get_is_init() && globals::show_gui) {
			set_cursor_capture(false);
		}

		// Handle module keybinds (toggle modules on key press).
		// Skipped while the GUI is open (the menu owns the keyboard) and while
		// a keybind capture is in progress.
		if (!globals::show_gui && !GUI::is_keybind_waiting()) {
			flaway::modules::handle_keybinds();
		}

		// Check for unhook-all request (full unload)
		flaway::modules::check_unhook_all();

		// Run all cheat modules every frame (they self-gate on their enabled flags).
		// Skip if init failed — sdk::instance is null and modules would crash.
		if (flaway::instance && flaway::instance->initialized.load(std::memory_order_acquire)) {
			try {
				flaway::modules::run_all();
			} catch (std::exception& e) {
				fprintf(stderr, "[HOOK] Exception in modules::run_all: %s\n", e.what()); fflush(stderr);
			} catch (...) {
				fprintf(stderr, "[HOOK] Unknown exception in modules::run_all\n"); fflush(stderr);
			}
		}

        if (g_init_needed && !imgui_init) {
            diag("[6] GUI init starting");
            ensure_minecraft_x11();
            diag("[6b] x11 ensured");
            logger::log("[hook] starting GUI init");
            logger::log_rss("gui-init-before");
            fprintf(stderr, "[DEBUG] Starting GUI init (OpenGL backend)...\n"); fflush(stderr);
            // The GL context is current here (we are inside the game's swap
            // call on its render thread), so the ImGui GL3 backend can
            // resolve its functions and create the font atlas right now.
            if (!GUI::init()) {
                logger::log("[hook] GUI::init FAILED");
                diag("[6c] GUI::init FAILED");
                fprintf(stderr, "[DEBUG] GUI::init FAILED\n"); fflush(stderr);
                g_init_needed = false;
            } else {
                logger::log("[hook] GUI::init OK");
                diag("[6c] GUI::init OK");
                imgui_init = true;
                g_init_needed = false;
                globals::show_gui = true;
                fprintf(stderr, "[DEBUG] GUI init SUCCESS\n"); fflush(stderr);
            }
            logger::log_rss("gui-init-after");
        }

        if (GUI::get_is_init()) {
            int w = 0, h = 0;
            if (g_have_minecraft_x11) {
                XWindowAttributes attr;
                if (XGetWindowAttributes(g_minecraft_dpy, g_minecraft_window, &attr)) {
                    w = attr.width; h = attr.height;
                }
            }
            if (w <= 0 || h <= 0) {
                x11_helper::get_window_dimensions(&w, &h);
            }
            if (w > 0 && h > 0) {
                static int s_render_diag = 0;
                if (s_render_diag < 5) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "[7] render w=%d h=%d x11=%d", w, h, (int)g_have_minecraft_x11);
                    diag(buf);
                    s_render_diag++;
                }
                bool rendered = GUI::render(w, h);
                if (s_render_diag <= 5) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "[8] GUI::render returned %d", (int)rendered);
                    diag(buf);
                }
                return rendered;
            }
            static bool s_warned_dims = false;
            if (!s_warned_dims) {
                diag("[7] render: w<=0 or h<=0, skipping");
                s_warned_dims = true;
            }
        }
        return false;
    }

    void ensure_minecraft_x11() {
        if (g_have_minecraft_x11) return;
        fprintf(stderr, "[HOOK] ensure_minecraft_x11: s_glfw_lib=%p g_glfw_window=%p\n", s_glfw_lib, g_glfw_window);
        fflush(stderr);
        if (g_glfw_window) {
            // Resolve from the GLFW library handle (not RTLD_DEFAULT, since GLFW
            // might have been loaded by us without RTLD_GLOBAL)
            if (!g_glfwGetX11Display && s_glfw_lib) {
                g_glfwGetX11Display = (Display* (*)(void*))dlsym(s_glfw_lib, "glfwGetX11Display");
                g_glfwGetX11Window = (unsigned long (*)(void*))dlsym(s_glfw_lib, "glfwGetX11Window");
                fprintf(stderr, "[HOOK] ensure_minecraft_x11: dlsym from s_glfw_lib: dpy=%p win=%p\n",
                    (void*)(intptr_t)(bool)g_glfwGetX11Display, (void*)(intptr_t)(bool)g_glfwGetX11Window);
                fflush(stderr);
            }
            if (g_glfwGetX11Display && g_glfwGetX11Window) {
                g_minecraft_dpy = g_glfwGetX11Display(g_glfw_window);
                g_minecraft_window = (Window)g_glfwGetX11Window(g_glfw_window);
                if (g_minecraft_dpy && g_minecraft_window) {
                    g_have_minecraft_x11 = true;
                    fprintf(stderr, "[DEBUG] Got X11 dpy=%p window=%lu from GLFW\n",
                        (void*)g_minecraft_dpy, (unsigned long)g_minecraft_window);
                }
            }
        }
        // Fallback: try dlsym(RTLD_DEFAULT, ...) just in case
        if (!g_have_minecraft_x11) {
            auto dpy = (Display* (*)(void*))dlsym(RTLD_DEFAULT, "glfwGetX11Display");
            auto win = (unsigned long (*)(void*))dlsym(RTLD_DEFAULT, "glfwGetX11Window");
            if (dpy && win && g_glfw_window) {
                g_minecraft_dpy = dpy(g_glfw_window);
                g_minecraft_window = (Window)win(g_glfw_window);
                if (g_minecraft_dpy && g_minecraft_window) {
                    g_have_minecraft_x11 = true;
                    fprintf(stderr, "[DEBUG] Got X11 dpy=%p window=%lu from GLFW (RTLD_DEFAULT)\n",
                        (void*)g_minecraft_dpy, (unsigned long)g_minecraft_window);
                }
            }
        }
    }

    static void h_swap_buffers(void* win, unsigned long drawable) {
        // Save the drawable for proper passthrough to glXSwapBuffers
        g_last_drawable = drawable;

        // Diagnostic: write once on first hook call
        {
            static bool s_first = true;
            if (s_first) {
                s_first = false;
                diag("[4] h_swap_buffers FIRST CALL");
            }
        }

        // Deferred ImGui/GL teardown: runs here on the render thread, where the
        // GL context is current (required by ImGui_ImplOpenGL3_Shutdown's
        // glDelete* calls). release_gui_resources() (teardown thread) only sets
        // the flag, never touches GL.
        if (g_gui_shutdown_pending) {
            g_gui_shutdown_pending = false;
            GUI::shutdown();
            g_gui_shutdown_done = true;
            logger::log("[flaway] deferred GUI shutdown done on render thread");
        }

        // No pacing and no capture: the overlay is drawn directly into the
        // game's OpenGL framebuffer inside MainHook(), and then the REAL swap
        // happens below. The game's own swap (vsync) paces the loop exactly
        // like a normal game — one overlay draw per presented frame, zero added
        // latency, no readback stalls, no cadence mismatch.

        // Measure the REAL game loop rate (swap calls per second).
        {
            static std::chrono::steady_clock::time_point s_fps_t = std::chrono::steady_clock::now();
            auto n = std::chrono::steady_clock::now();
            double dt = std::chrono::duration<double>(n - s_fps_t).count();
            s_fps_t = n;
            if (dt > 1e-6 && dt < 0.5) {
                float f = (float)(1.0 / dt);
                s_game_fps = s_game_fps <= 0.0f ? f : s_game_fps * 0.9f + f * 0.1f;
            }
        }

        // Fully unloaded: never run overlay/JNI code again, just present.
        // EXCEPT: a re-inject was requested (SIGCONT seen after an unhook) AND
        // the teardown has fully completed — then re-arm the cheat so MainHook
        // + the lazy init run again on the next frame. This runs on the render
        // thread, like the first-frame init. teardown_done keeps a SIGCONT that
        // arrived mid-teardown from racing the in-progress cleanup: the flag
        // stays set and is consumed once teardown finishes.
        if (get_unhooked()) {
            if (flaway_reinject_requested.load(std::memory_order_acquire) && flaway::instance &&
                flaway::instance->teardown_done) {
                flaway_reinject_requested.store(0, std::memory_order_release);
                flaway::instance->reinit();
                logger::log("[flaway] re-inject requested: re-arming cheat");
            }
            if (o_swap_buffers) {
                if (s_hook_type == HOOK_GLX)
                    o_swap_buffers(win, g_last_drawable);
                else
                    o_swap_buffers(win, 0);
            }
            return;
        }

        // Capture GLFWwindow pointer (only when hooked to glfwSwapBuffers)
        if (s_hook_type == HOOK_GLFW) {
            if (!g_glfw_window) {
                g_glfw_window = win;
                ensure_minecraft_x11();
            }
        } else if (s_hook_type == HOOK_GLX && !g_have_minecraft_x11) {
            // For glXSwapBuffers, first arg is Display*, second is GLXDrawable
            g_minecraft_dpy = (Display*)win;
            g_minecraft_window = (Window)drawable;
            if (g_minecraft_dpy && g_minecraft_window)
                g_have_minecraft_x11 = true;
        } else if (s_hook_type == HOOK_EGL && !g_have_minecraft_x11) {
            // For eglSwapBuffers, first arg is EGLDisplay, second is EGLSurface
            // We can't extract X11 window/surface from EGL args directly.
            // Fall back to X11 query below.
        }

        // Draw the overlay (HUD / menu / ESP) directly into the game's back
        // buffer.  ImGui's OpenGL3 backend manages its own GL state.
        bool overlay_rendered = false;
        if (!get_unhooked())
        {
            bool locked = false;
            try
            {
                if (pthread_mutex_lock(&g_render_mutex) == 0)
                {
                    locked = true;
                    if (!get_unhooked()) overlay_rendered = MainHook();
                }
            }
            catch (...)
            {
                fprintf(stderr, "[HOOK] exception in MainHook\n"); fflush(stderr);
            }
            if (locked)
                pthread_mutex_unlock(&g_render_mutex);
        }

        (void)overlay_rendered;

        if (o_swap_buffers) {
            if (s_hook_type == HOOK_GLX)
                o_swap_buffers(win, g_last_drawable);
            else
                o_swap_buffers(win, 0);
        }
    }

    // dl_iterate_phdr callback: look for a loaded library that contains the
    // given symbol name. Saves the library handle on match.
    struct SymSearch {
        const char* name;
        void* found;
        void* lib_handle;
    };

    static int find_sym_callback(struct dl_phdr_info* info, size_t, void* data) {
        auto* s = (SymSearch*)data;
        if (!info->dlpi_name || !info->dlpi_name[0]) return 0;
        void* h = dlopen(info->dlpi_name, RTLD_LAZY | RTLD_NOLOAD);
        if (!h) return 0;
        void* sym = dlsym(h, s->name);
        if (sym) {
            s->found = sym;
            s->lib_handle = h;
            return 1;  // stop iteration
        }
        dlclose(h);
        return 0;
    }

    // Find a symbol by enumerating all loaded shared objects.
    // RTLD_DEFAULT fails when the library was loaded with RTLD_LOCAL
    // (common for LWJGL-loaded GLFW). This scans every loaded .so instead.
    static void* find_loaded_symbol(const char* name) {
        // Try RTLD_DEFAULT first (fast path)
        void* sym = dlsym(RTLD_DEFAULT, name);
        if (sym) return sym;

        // Scan all loaded libraries
        SymSearch s{ name, nullptr, nullptr };
        dl_iterate_phdr(find_sym_callback, &s);
        if (s.found) {
            // Save the handle so we don't dlclose it yet
            s_opened_lib = s.lib_handle;
            return s.found;
        }
        return nullptr;
    }

    bool init() {
        if (s_hooks_installed) { diag("init: already installed, skip"); return true; }
        diag("init: starting...");

        // Make libGL symbols globally visible so dlsym(RTLD_DEFAULT, "glXxx")
        // works.  LWJGL/GLFW load libGL with RTLD_LOCAL which hides all GL
        // symbols.  Without this, glass_blur and GUI GL state save/restore
        // get NULL function pointers and the game's render breaks.
        {
            void* gl = dlopen("libGL.so.1", RTLD_LAZY | RTLD_GLOBAL);
            if (!gl) gl = dlopen("libGL.so", RTLD_LAZY | RTLD_GLOBAL);
            if (gl) {
                diag("init: libGL opened with RTLD_GLOBAL");
                // Keep the handle alive — dlclose would un-export the symbols.
                // We intentionally leak it; the process lifetime is the game.
            } else {
                diag("init: WARNING libGL not found");
            }
        }

        x11_helper::init();
        diag("init: x11_helper::init done");

        // Resolve glfwSetInputMode early so the menu can release/recapture the
        // cursor without waiting for the swap hook.
        g_glfwSetInputMode = (glfwSetInputMode_t)find_loaded_symbol("glfwSetInputMode");
        if (!g_glfwSetInputMode) {
            void* glfw_lib = dlopen("libglfw.so.3", RTLD_LAZY | RTLD_NOLOAD);
            if (glfw_lib) {
                g_glfwSetInputMode = (glfwSetInputMode_t)dlsym(glfw_lib, "glfwSetInputMode");
                dlclose(glfw_lib);
            }
        }

        void* target = nullptr;

        // Try each swap function — use find_loaded_symbol which enumerates all
        // loaded .so files when RTLD_DEFAULT fails (LWJGL loads GLFW locally)
        target = find_loaded_symbol("glfwSwapBuffers");
        diag(target ? "init: found glfwSwapBuffers" : "init: glfwSwapBuffers not found");

        if (target) {
            s_hook_type = HOOK_GLFW;
            // Save the GLFW library handle so ensure_minecraft_x11() can
            // resolve glfwGetX11Display/Window later.  s_opened_lib was set
            // by find_loaded_symbol and points to the same loaded .so.
            if (s_opened_lib && !s_glfw_lib) {
                s_glfw_lib = s_opened_lib;
                diag("init: saved GLFW lib handle for X11 resolution");
            }
            // Also eagerly resolve glfwSetInputMode from the same handle
            if (!g_glfwSetInputMode) {
                g_glfwSetInputMode = (glfwSetInputMode_t)dlsym(s_glfw_lib, "glfwSetInputMode");
            }
        }
        if (!target) {
            target = find_loaded_symbol("glXSwapBuffers");
            diag(target ? "init: found glXSwapBuffers" : "init: glXSwapBuffers not found");
            if (target) s_hook_type = HOOK_GLX;
        }
        if (!target) {
            target = find_loaded_symbol("eglSwapBuffers");
            diag(target ? "init: found eglSwapBuffers" : "init: eglSwapBuffers not found");
            if (target) s_hook_type = HOOK_EGL;
        }

        if (!target) {
            diag("init: FAILED - no swap function found in any loaded library");
            return false;
        }

        {
            char buf[128];
            snprintf(buf, sizeof(buf), "init: target=%p hook_type=%d", target, (int)s_hook_type);
            diag(buf);
        }

        diag("init: calling install_hook...");
        void* tramp = install_hook(target, (void*)h_swap_buffers);
        if (tramp) {
            o_swap_buffers = (swap_buffers_t)tramp;
            s_swap_target = target;
            s_hooks_installed = true;
            diag("init: install_hook OK");
            return true;
        }
        diag("init: install_hook FAILED (returned NULL)");
        return false;
    }

    void cleanup() {
        if (s_opened_lib) {
            // Don't close if s_glfw_lib points to the same handle —
            // it's still needed for ensure_minecraft_x11().
            if (s_opened_lib != s_glfw_lib) {
                dlclose(s_opened_lib);
            }
            s_opened_lib = nullptr;
        }
    }

    void remove_all_hooks() {
        // Ensure the render thread is out of MainHook and h_swap_buffers
        // before removing hooks. Otherwise the trampoline (which lives in
        // this .so) would be freed while the render thread is still executing
        // through it → SIGSEGV on the next swap.
        wait_render_idle();
        wait_gui_shutdown();

        if (s_swap_target) {
            linux_hook::remove_hook(s_swap_target);
            s_swap_target = nullptr;
            o_swap_buffers = nullptr;
            s_hooks_installed = false;
            fprintf(stderr, "[HOOK] swap hook removed\n"); fflush(stderr);
        }
        x11_helper::shutdown();
        crash_dump::shutdown();
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGCONT, &sa, nullptr);
    }

    void release_gui_resources() {
        g_gui_shutdown_done = false;
        g_gui_shutdown_pending = true;
        imgui_init = false;
        g_init_needed = true;
        reset_fbo_cache();
        fprintf(stderr, "[HOOK] GUI resources released (GL shutdown deferred)\n"); fflush(stderr);
    }
}
