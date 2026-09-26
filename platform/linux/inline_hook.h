#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

// Minimal inline hook (detour) for Linux x86_64
// Replaces MinHook on Windows

namespace linux_hook
{
    // Memory protection
    bool make_writable(void* address, size_t size);
    bool make_executable(void* address, size_t size);
    bool make_rwx(void* address, size_t size);

    // Install a hook: overwrites `target` with jmp to `detour`
    // Returns trampoline address for calling original, or nullptr on failure
    void* install_hook(void* target, void* detour);

    // Remove a previously installed hook
    bool remove_hook(void* target);

    // Call original function through trampoline
    void* get_original(void* target);
}
