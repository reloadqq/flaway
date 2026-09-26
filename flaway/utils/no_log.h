#pragma once

// All client-side logging removed: every fprintf/printf/fflush in the .so
// becomes a no-op. This header is force-included for every TU via
// `-include flaway/utils/no_log.h` in the Makefile, so nothing is written to
// the game's stderr / latest.log and no log files are created.

#define fprintf(...) ((void)0)
#define printf(...) ((void)0)
#define fflush(...) ((void)0)
