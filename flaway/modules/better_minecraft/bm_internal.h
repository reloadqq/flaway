#pragma once

// Shared between better_minecraft.cpp and bm_hooks.cpp (the JNI hooks).
// Everything here runs on the render thread (swapBuffers hook -> run_all /
// game render), no locking is needed.

namespace flaway
{
	namespace modules
	{
		namespace better_minecraft
		{
			namespace detail
			{
				// Current zoom factor (1.0 = vanilla FOV). Animated by
				// zoom_tick(), read by the GameRenderer.getFov hook.
				extern float zoom_state;

				// Attach getFov / Camera.update + the vanilla-animation ports
				// (ChatHud / Tiny Item Animations / SmoothGUI). Idempotent;
				// retried each frame from run() until everything is attached.
				bool init_hooks();

				// Zoom key + wheel + animation + mouse-sensitivity scaling.
				void zoom_tick();

				// Drop the animation state and restore the saved sensitivity
				// (used by shutdown()).
				void zoom_reset();
			}
		}
	}
}
