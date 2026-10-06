#pragma once

// All client-side logging removed: every fprintf/printf/fflush in the .so
// becomes a no-op. This header is force-included for every TU via
// `-include flaway/utils/no_log.h` in the Makefile, so nothing is written to
// the game's stderr / latest.log and no log files are created.
//
// IMPORTANT: the real declarations must be pulled in BEFORE the macros are
// defined. Force-including a header that only #defines would rewrite the
// prototypes inside <stdio.h>/<cstdio> (`int fprintf(...)` ->
// `int ((void)0)(...)`) and fail to compile on every TU.
#include <cstdio>
#include <cstdio>
#include <cstdio>

#define fprintf(...) ((void)0)
#define printf(...) ((void)0)
#define fflush(...) ((void)0)
