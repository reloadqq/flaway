#include "logger.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <atomic>
#include <fcntl.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// POSIX-fd logger.
//
// CRITICAL: this TU must contain NO C++ objects with non-constexpr
// constructors at static scope (no std::ofstream, no std::string globals).
// The .so is loaded via dlopen and `flaway_init` (an __attribute__((constructor))
// in entry.cpp) runs BEFORE _GLOBAL__sub_I_logger.cpp in .init_array — a
// std::ofstream would still be a zeroed .bss blob when logger::init() calls
// ofstream::open(), crashing inside basic_ios::clear.
//
// std::atomic<int> with a constexpr constructor is constant-initialized
// (no runtime ctor), so it is safe here.
// ---------------------------------------------------------------------------
bool logger::init()
{
	// Client-side logging removed: never create or open flaway_log.txt.
	return true;
}

void logger::shutdown()
{
}

bool logger::is_ready()
{
	return false;
}

void logger::log(const std::string&)
{
}

void logger::log_error(const std::string&)
{
}

void logger::log_debug(const std::string&)
{
}

void logger::log_rss(const char*)
{
}
