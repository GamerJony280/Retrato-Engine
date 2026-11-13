#pragma once
#include "../../platform/common/vulkan_functions.hpp"
#include "ember/render.hpp"
#include <functional>
#include <utility>
namespace ember::detail {
struct VulkanWindow {
    std::vector<const char *> extensions;
    std::function<VkSurfaceKHR(VkInstance, PFN_vkGetInstanceProcAddr)> create;
    std::function<std::pair<int,int>()> size;
    bool vsync = true, nearest = true;
};
std::unique_ptr<Backend> make_vulkan_window(VulkanWindow);
// One graphics submission in flight. Present-finished semaphores belong to
// swapchain images, not to frame indices: acquisition proves safe reuse.
class VulkanPresentation {
    VulkanFunctions &vk_;
    VulkanWindow window_;
    VkInstance instance_{};
    VkPhysicalDevice physical_{};
    VkDevice device_{};
    VkSurfaceKHR surface_{};
    VkSwapchainKHR swapchain_{};
    VkQueue present_queue_{};
    VkExtent2D extent_{};
    VkSemaphore acquired_{};
    std::vector<VkImage> images_;
    std::vector<VkSemaphore> finished_;
    uint32_t graphics_family_=0, present_family_=0;
    bool dirty_=true;
#define RETRATO_WSI_FUNCTIONS(X) \
    X(vkDestroySurfaceKHR) X(vkGetPhysicalDeviceSurfaceSupportKHR) \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR) X(vkEnumerateDeviceExtensionProperties) \
    X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR) \
    X(vkAcquireNextImageKHR) X(vkQueuePresentKHR) X(vkCreateSemaphore) X(vkDestroySemaphore) \
    X(vkCmdBlitImage) X(vkCmdClearColorImage)
#define RETRATO_WSI_DECLARE(fn) PFN_##fn fn=nullptr;
    RETRATO_WSI_FUNCTIONS(RETRATO_WSI_DECLARE)
#undef RETRATO_WSI_DECLARE
    void clear_swapchain() noexcept;
    bool resize();
public:
    VulkanPresentation(VulkanFunctions &, VkInstance, VulkanWindow);
    ~VulkanPresentation();
    VulkanPresentation(const VulkanPresentation &)=delete;
    VulkanPresentation &operator=(const VulkanPresentation &)=delete;
    bool supports(VkPhysicalDevice, uint32_t graphics, uint32_t &present);
    void attach(VkPhysicalDevice, VkDevice, uint32_t graphics, uint32_t present);
    void present(VkImage, int width, int height, VkQueue, VkCommandBuffer, VkFence);
};
}
