#pragma once

namespace flaway
{
	namespace modules
	{
		namespace better_minecraft
		{
			// Per-frame: attach the JNI hooks (once) and animate the zoom.
			void run();

			// Legacy ImGui overlay entry — the vanilla-animation ports draw
			// through the hooked vanilla render methods, so nothing runs here.
			void draw();

			// Force everything off and restore any tweaked game option
			// (mouse sensitivity). Called from the flaway teardown paths.
			void shutdown();
		}
	}
}
