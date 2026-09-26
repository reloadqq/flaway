#pragma once
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <atomic>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace logger {
    bool init();
#ifdef _WIN32
    bool init(HMODULE h_module);
#endif
    void shutdown();
    void log(const std::string& message);
    void log_error(const std::string& message);
    void log_debug(const std::string& message);
    bool is_ready();  // safe to call before init()
    void log_rss(const char* tag);  // logs current RSS in MB
}
