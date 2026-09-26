#pragma once
#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan.h>
#include <X11/Xlib.h>
#include <cstdint>

struct ImGui_ImplVulkanH_Window;

struct BackgroundTexture {
    static const int MAX_FRAMES = 8;

    VkSampler sampler = VK_NULL_HANDLE;
    int width = 0, height = 0;

    struct PerFrame {
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        void* texID = nullptr;
        VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    };
    PerFrame frames[MAX_FRAMES];
    int imageCount = 0;

    bool init(VkDevice device, VkPhysicalDevice physDev, int w, int h, int imgCount);
    void registerTexID(VkDevice device);
    void recordCopy(VkCommandBuffer cmdBuf, VkDevice device, int imageIndex, const uint8_t* pixels);
    bool valid(int imageIndex) const {
        return imageIndex >= 0 && imageIndex < imageCount && frames[imageIndex].view != VK_NULL_HANDLE;
    }
    void shutdown(VkDevice device);
};

struct VkContext {
    VkInstance              instance = VK_NULL_HANDLE;
    VkPhysicalDevice        physicalDevice = VK_NULL_HANDLE;
    VkDevice                device = VK_NULL_HANDLE;
    VkQueue                 queue = VK_NULL_HANDLE;
    uint32_t                queueFamily = 0;
    VkDescriptorPool        descriptorPool = VK_NULL_HANDLE;
    VkSurfaceKHR            surface = VK_NULL_HANDLE;
    ImGui_ImplVulkanH_Window* wd = nullptr;
    BackgroundTexture       backgroundTexture;
    bool                    bgEverUploaded = false;

    bool init(Display* dpy, Window window, int width, int height);
    void shutdown();
    bool recreateSwapchain(int width, int height);
};

extern VkContext g_vk;
