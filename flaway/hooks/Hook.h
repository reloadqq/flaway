#ifndef HOOK_H_
#define HOOK_H_

#ifdef _WIN32
#include <Windows.h>
#endif

namespace Hook
{
	bool init();
	void shutdown();

	bool get_is_init();

	// Actual game render-loop FPS (swap-call rate), EMA-smoothed. This is the
	// true game frame rate even when the Vulkan overlay is presenting at a
	// slower vsync-limited rate, so the HUD shows the real FPS instead of the
	// lowered overlay rate.
	float get_game_fps();

	// Marks the cheat as fully unhooked (value=true) or re-armed for a
	// re-inject (value=false). While unhooked, the swap hook bypasses all
	// overlay work (run_all, JNI, ImGui) and just calls the original swap, so
	// no code touches the classloader/Java objects once teardown has started.
	void set_unhooked(bool value);
	bool get_unhooked();

	// Blocks until the render thread has left MainHook (used by unhook_all so
	// the overlay/JNI teardown never races a frame that is mid-render).
	// Returns false (after a bounded wait) if the render thread is stuck inside
	// MainHook; the caller must then skip the destructive teardown.
	bool wait_render_idle();

	// Blocks (bounded) until the render thread has consumed the deferred
	// ImGui/GL shutdown. Used by unhook_all before removing the swap hook.
	void wait_gui_shutdown();

	// Releases Vulkan/ImGui resources and resets overlay state after unhook.
	void release_gui_resources();

	// Reset stale FBO/GL cache handles on unhook so re-inject gets fresh objects.
	void reset_fbo_cache();

#ifdef _WIN32
	HWND get_window();
#else
	unsigned long get_window();
#endif
}
namespace linux_hook {
    bool init();
    void set_unhooked(bool value);
    bool get_unhooked();
    bool wait_render_idle();
    void wait_gui_shutdown();
    void release_gui_resources();
    void remove_all_hooks();
    void reset_fbo_cache();
    float get_game_fps();
    // Installs the SIGCONT handler used to detect a re-inject (gdb sends
    // SIGCONT when it detaches). Must be called once at library load.
    void install_reinject_signal();
}


#endif
