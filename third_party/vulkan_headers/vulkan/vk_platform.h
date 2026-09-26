#ifndef VULKAN_PLATFORM_H_
#define VULKAN_PLATFORM_H_

/*
 * Minimal vk_platform.h for the syntax-check harness. The vendored
 * /tmp/vu/include/vulkan tree is missing its platform header; this stub
 * provides the macros vulkan_core.h relies on.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VKAPI_ATTR
#define VKAPI_CALL __stdcall
#else
#define VKAPI_ATTR
#define VKAPI_CALL
#endif

#define VKAPI_PTR

#ifdef __cplusplus
}
#endif

#endif /* VULKAN_PLATFORM_H_ */