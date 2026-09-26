#include "vk_context.h"
#include "../../utils/imgui/imgui_impl_vulkan.h"
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include "flaway/utils/no_log.h"

VkContext g_vk;

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*)
{
    fprintf(stderr, "[VK] %s\n", data->pMessage);
    return VK_FALSE;
}

static bool has_extension(const char* name) {
    uint32_t count;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    if (count > 64) count = 64;
    VkExtensionProperties exts[64];
    vkEnumerateInstanceExtensionProperties(nullptr, &count, exts);
    for (uint32_t i = 0; i < count; i++)
        if (strcmp(exts[i].extensionName, name) == 0)
            return true;
    return false;
}

static bool create_instance(VkInstance& instance)
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "flaway";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "flaway";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    bool has_debug = has_extension("VK_EXT_debug_utils");

    VkDebugUtilsMessengerCreateInfoEXT debugInfo{};
    debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
    debugInfo.pfnUserCallback = debug_callback;

    const char* exts[8];
    uint32_t extCount = 0;

    // Always need surface + platform extension
    exts[extCount++] = "VK_KHR_surface";
    exts[extCount++] = "VK_KHR_xlib_surface";

    if (has_debug)
        exts[extCount++] = "VK_EXT_debug_utils";

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = extCount;
    createInfo.ppEnabledExtensionNames = exts;
    createInfo.pNext = has_debug ? &debugInfo : nullptr;

    VkResult err = vkCreateInstance(&createInfo, nullptr, &instance);
    if (err != VK_SUCCESS) {
        fprintf(stderr, "[VK] Failed to create instance: %d\n", err);
        return false;
    }
    return true;
}

static bool select_physical_device(VkInstance instance, VkPhysicalDevice& physDev,
                                    uint32_t& queueFamily, VkSurfaceKHR surface)
{
    uint32_t count;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (!count) return false;

    VkPhysicalDevice devices[8];
    vkEnumeratePhysicalDevices(instance, &count, devices);

    for (uint32_t i = 0; i < count && i < 8; i++) {
        uint32_t qCount;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &qCount, nullptr);
        VkQueueFamilyProperties qProps[16];
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &qCount, qProps);
        for (uint32_t j = 0; j < qCount && j < 16; j++) {
            if (qProps[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                VkBool32 presentSupport = VK_TRUE;
                if (surface)
                    vkGetPhysicalDeviceSurfaceSupportKHR(devices[i], j, surface, &presentSupport);
                if (presentSupport) {
                    physDev = devices[i];
                    queueFamily = j;
                    VkPhysicalDeviceProperties props;
                    vkGetPhysicalDeviceProperties(physDev, &props);
                    fprintf(stderr, "[VK] Selected device: %s (queue family %u)\n", props.deviceName, j);
                    return true;
                }
            }
        }
    }
    return false;
}

static bool create_device(VkPhysicalDevice physDev, VkDevice& device, uint32_t queueFamily)
{
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo qInfo{};
    qInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qInfo.queueFamilyIndex = queueFamily;
    qInfo.queueCount = 1;
    qInfo.pQueuePriorities = &queuePriority;

    const char* devExts[] = { "VK_KHR_swapchain" };
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &qInfo;
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = devExts;

    VkResult err = vkCreateDevice(physDev, &createInfo, nullptr, &device);
    if (err != VK_SUCCESS) {
        fprintf(stderr, "[VK] Failed to create device: %d\n", err);
        return false;
    }
    return true;
}

static bool create_descriptor_pool(VkDevice device, VkDescriptorPool& pool)
{
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32 }
    };
    VkDescriptorPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    info.maxSets = 32;
    info.poolSizeCount = 1;
    info.pPoolSizes = poolSizes;
    return vkCreateDescriptorPool(device, &info, nullptr, &pool) == VK_SUCCESS;
}

bool VkContext::init(Display* dpy, Window window, int width, int height)
{
    if (!create_instance(instance)) return false;

    // Create Xlib surface
    VkXlibSurfaceCreateInfoKHR surfaceInfo{};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.dpy = dpy;
    surfaceInfo.window = window;
    VkResult res = vkCreateXlibSurfaceKHR(instance, &surfaceInfo, nullptr, &surface);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "[VK] Failed to create surface: %d\n", res);
        return false;
    }

    // Select physical device (with present check using the surface)
    if (!select_physical_device(instance, physicalDevice, queueFamily, surface)) {
        fprintf(stderr, "[VK] No suitable physical device with present support\n");
        return false;
    }

    if (!create_device(physicalDevice, device, queueFamily)) return false;
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    if (!create_descriptor_pool(device, descriptorPool)) return false;

    // Pre-check: surface format query must succeed. ImGui's SelectSurfaceFormat
    // doesn't check vkGetPhysicalDeviceSurfaceFormatsKHR return value, and
    // will crash (SIGSEGV) on failure because avail_count stays uninitialized.
    {
        uint32_t fc;
        VkResult vr = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &fc, nullptr);
        if (vr != VK_SUCCESS || fc == 0) {
            fprintf(stderr, "[VK] Surface format query failed: %d (count=%u)\n", vr, fc);
            return false;
        }
    }

    // Set up ImGui window
    wd = new ImGui_ImplVulkanH_Window();
    wd->Surface = surface;
    wd->Width = width;
    wd->Height = height;

    VkFormat requestFormats[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
                                  VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM };
    wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
        physicalDevice, surface, requestFormats, 4, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);

    // MAILBOX first: presents ~vsync-synced (smooth, no tearing). IMMEDIATE
    // would show visible tear lines on the last window -> the "flickering/
    // cracked render" symptom. IMMEDIATE only as last resort.
    VkPresentModeKHR modes[] = { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR };
    wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(physicalDevice, surface, modes, 3);

    int minImageCount = ImGui_ImplVulkanH_GetMinImageCountFromPresentMode(wd->PresentMode);
    if (minImageCount < 2) minImageCount = 2;
    ImGui_ImplVulkanH_CreateOrResizeWindow(instance, physicalDevice, device, wd,
                                            queueFamily, nullptr, width, height, minImageCount);

    return true;
}

void VkContext::shutdown()
{
    if (device)
        vkDeviceWaitIdle(device);

    backgroundTexture.shutdown(device);

    if (wd) {
        // DestroyWindow destroys the surface handle — nullify g_vk.surface to avoid double-free
        ImGui_ImplVulkanH_DestroyWindow(instance, device, wd, nullptr);
        delete wd;
        wd = nullptr;
        surface = VK_NULL_HANDLE;
    }

    if (descriptorPool) { vkDestroyDescriptorPool(device, descriptorPool, nullptr); descriptorPool = VK_NULL_HANDLE; }
    if (surface) { vkDestroySurfaceKHR(instance, surface, nullptr); surface = VK_NULL_HANDLE; }
    if (device) { vkDestroyDevice(device, nullptr); device = VK_NULL_HANDLE; }
    if (instance) { vkDestroyInstance(instance, nullptr); instance = VK_NULL_HANDLE; }
}

bool VkContext::recreateSwapchain(int width, int height)
{
    if (!wd) return false;
    if (device)
        vkDeviceWaitIdle(device);

    wd->Width = width;
    wd->Height = height;

    {
        uint32_t fc;
        if (vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &fc, nullptr) != VK_SUCCESS || fc == 0)
            return false;
    }

    VkFormat formats[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM };
    wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
        physicalDevice, surface, formats, 2, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);

    // MAILBOX first: vsync-synced smooth present (no tearing/flicker).
    VkPresentModeKHR modes[] = { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR };
    wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(physicalDevice, surface, modes, 3);

    int minImageCount = ImGui_ImplVulkanH_GetMinImageCountFromPresentMode(wd->PresentMode);
    if (minImageCount < 2) minImageCount = 2;
    if (minImageCount < 2) minImageCount = 2;
    ImGui_ImplVulkanH_CreateOrResizeWindow(instance, physicalDevice, device, wd,
                                            queueFamily, nullptr, width, height, minImageCount);

    wd->SemaphoreIndex = 0;
    wd->FrameIndex = 0;

    bgEverUploaded = false;

    // Recreate background texture to match new swapchain dimensions
    if (!backgroundTexture.init(device, physicalDevice, wd->Width, wd->Height, wd->ImageCount))
        return true; // non-fatal — next recreate will retry

    backgroundTexture.registerTexID(device);

    return true;
}

// ---- BackgroundTexture ----

bool BackgroundTexture::init(VkDevice dev, VkPhysicalDevice physDev, int w, int h, int imgCount)
{
    shutdown(dev);
    width = w; height = h;
    imageCount = (imgCount > 0 && imgCount <= MAX_FRAMES) ? imgCount : 2;

    VkPhysicalDeviceMemoryProperties physMem;
    vkGetPhysicalDeviceMemoryProperties(physDev, &physMem);

    auto findMem = [&](uint32_t bits, VkMemoryPropertyFlags flags) -> uint32_t {
        for (uint32_t i = 0; i < physMem.memoryTypeCount; i++)
            if ((bits & (1u << i)) && (physMem.memoryTypes[i].propertyFlags & flags) == flags)
                return i;
        return ~0u;
    };

    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = (VkDeviceSize)w * h * 4;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;

    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_B8G8R8A8_UNORM;
    ii.extent = { (uint32_t)w, (uint32_t)h, 1 };
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_B8G8R8A8_UNORM;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;

    for (int i = 0; i < imageCount; i++) {
        PerFrame& pf = frames[i];

        if (vkCreateBuffer(dev, &bi, nullptr, &pf.staging) != VK_SUCCESS) { shutdown(dev); return false; }
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(dev, pf.staging, &mr);
        ai.allocationSize = mr.size;
        uint32_t mt = findMem(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (mt == ~0u) mt = findMem(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        if (mt == ~0u) { shutdown(dev); return false; }
        ai.memoryTypeIndex = mt;
        if (vkAllocateMemory(dev, &ai, nullptr, &pf.stagingMemory) != VK_SUCCESS) { shutdown(dev); return false; }
        vkBindBufferMemory(dev, pf.staging, pf.stagingMemory, 0);
        vkMapMemory(dev, pf.stagingMemory, 0, VK_WHOLE_SIZE, 0, &pf.mapped);

        if (vkCreateImage(dev, &ii, nullptr, &pf.image) != VK_SUCCESS) { shutdown(dev); return false; }
        vkGetImageMemoryRequirements(dev, pf.image, &mr);
        ai.allocationSize = mr.size;
        mt = findMem(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (mt == ~0u) mt = findMem(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        if (mt == ~0u) { shutdown(dev); return false; }
        ai.memoryTypeIndex = mt;
        if (vkAllocateMemory(dev, &ai, nullptr, &pf.memory) != VK_SUCCESS) { shutdown(dev); return false; }
        vkBindImageMemory(dev, pf.image, pf.memory, 0);

        vi.image = pf.image;
        if (vkCreateImageView(dev, &vi, nullptr, &pf.view) != VK_SUCCESS) { shutdown(dev); return false; }

        pf.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        pf.texID = nullptr;
    }

    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.anisotropyEnable = VK_FALSE;
    si.maxLod = 1.0f;
    if (vkCreateSampler(dev, &si, nullptr, &sampler) != VK_SUCCESS) { shutdown(dev); return false; }

    return true;
}

void BackgroundTexture::registerTexID(VkDevice device)
{
    if (!sampler) return;
    for (int i = 0; i < imageCount; i++) {
        PerFrame& pf = frames[i];
        if (pf.texID || !pf.view) continue;
        pf.texID = (void*)ImGui_ImplVulkan_AddTexture(sampler, pf.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
}

void BackgroundTexture::recordCopy(VkCommandBuffer cmdBuf, VkDevice device,
                                    int imageIndex, const uint8_t* pixels)
{
    if (imageIndex < 0 || imageIndex >= imageCount) return;
    PerFrame& pf = frames[imageIndex];
    if (!pf.image || !pf.view || !pf.texID) return;
    if (!pixels || !pf.mapped) return;

    memcpy(pf.mapped, pixels, (size_t)width * height * 4);

    VkMappedMemoryRange flushRange{};
    flushRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    flushRange.memory = pf.stagingMemory;
    flushRange.offset = 0;
    flushRange.size = VK_WHOLE_SIZE;
    vkFlushMappedMemoryRanges(device, 1, &flushRange);

    VkImageLayout oldLayout = pf.currentLayout;

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = pf.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) ? 0 : VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    VkPipelineStageFlags srcStage = (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED)
        ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
        : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    vkCmdPipelineBarrier(cmdBuf, srcStage,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { (uint32_t)width, (uint32_t)height, 1 };
    vkCmdCopyBufferToImage(cmdBuf, pf.staging, pf.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    pf.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void BackgroundTexture::shutdown(VkDevice device)
{
    for (int i = 0; i < MAX_FRAMES; i++) {
        PerFrame& pf = frames[i];
        if (pf.texID) {
            ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)pf.texID);
            pf.texID = nullptr;
        }
        if (pf.view) { vkDestroyImageView(device, pf.view, nullptr); pf.view = VK_NULL_HANDLE; }
        if (pf.image) { vkDestroyImage(device, pf.image, nullptr); pf.image = VK_NULL_HANDLE; }
        if (pf.memory) { vkFreeMemory(device, pf.memory, nullptr); pf.memory = VK_NULL_HANDLE; }
        if (pf.mapped) vkUnmapMemory(device, pf.stagingMemory);
        if (pf.stagingMemory) { vkFreeMemory(device, pf.stagingMemory, nullptr); pf.stagingMemory = VK_NULL_HANDLE; }
        if (pf.staging) { vkDestroyBuffer(device, pf.staging, nullptr); pf.staging = VK_NULL_HANDLE; }
        pf.mapped = nullptr;
    }
    if (sampler) { vkDestroySampler(device, sampler, nullptr); sampler = VK_NULL_HANDLE; }
}
