#pragma once

// On-disk trace removal for unhook.
//
//   hide()    - called when the cheat unloads: the log files
//               (~/.minecraft/flaway_crash.txt, flaway_diag.txt,
//               flaway_render.txt) are wiped from disk, and the data folder
//               (~/.minecraft/flaway) is moved to /tmp under a random name so
//               no flaway-named path is left behind in the game directory.
//   restore() - called on the next inject: puts the stashed folder back to
//               its original path (config + friends intact) and drops the
//               pointer. No-op when nothing was stashed.
namespace flaway
{
	namespace artifacts
	{
		void hide();
		void restore();
	}
}
