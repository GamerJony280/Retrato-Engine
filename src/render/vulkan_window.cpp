#include "vulkan_window.hpp"
#include <algorithm>
#include <cstring>
namespace ember::detail {
namespace {
void check(VkResult result, const char *operation) {
    if(result!=VK_SUCCESS) throw std::runtime_error(std::string(operation)+" (VkResult "+std::to_string(result)+")");
}
}
VulkanPresentation::VulkanPresentation(VulkanFunctions &vk, VkInstance instance, VulkanWindow window)
    :vk_(vk),window_(std::move(window)),instance_(instance) {
#define RETRATO_WSI_LOAD(fn) fn=reinterpret_cast<PFN_##fn>(vk_.get_proc(instance_,#fn)); \
    if(!fn) throw std::runtime_error("Vulkan presentation function unavailable: " #fn);
    RETRATO_WSI_FUNCTIONS(RETRATO_WSI_LOAD)
#undef RETRATO_WSI_LOAD
    surface_=window_.create(instance_,vk_.get_proc);
    if(!surface_)throw std::runtime_error("Cannot create Vulkan surface");
}
void VulkanPresentation::clear_swapchain() noexcept {
    for(auto semaphore:finished_)if(semaphore)vkDestroySemaphore(device_,semaphore,nullptr);
    finished_.clear();images_.clear();
    if(swapchain_)vkDestroySwapchainKHR(device_,swapchain_,nullptr);
    swapchain_=VK_NULL_HANDLE;extent_={};
}
VulkanPresentation::~VulkanPresentation() {
    if(device_) {vk_.vkDeviceWaitIdle(device_);clear_swapchain();if(acquired_)vkDestroySemaphore(device_,acquired_,nullptr);}
    if(surface_)vkDestroySurfaceKHR(instance_,surface_,nullptr);
}
bool VulkanPresentation::supports(VkPhysicalDevice physical, uint32_t graphics, uint32_t &present) {
    uint32_t count=0;check(vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,nullptr),"enumerate device extensions");
    std::vector<VkExtensionProperties> extensions(count);
    check(vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,extensions.data()),"device extensions");
    if(std::none_of(extensions.begin(),extensions.end(),[](auto &p){return std::strcmp(p.extensionName,VK_KHR_SWAPCHAIN_EXTENSION_NAME)==0;}))return false;
    VkBool32 supported=VK_FALSE;
    check(vkGetPhysicalDeviceSurfaceSupportKHR(physical,graphics,surface_,&supported),"surface support");
    if(supported){present=graphics;return true;}
    vk_.vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);
    for(uint32_t i=0;i<count;++i){check(vkGetPhysicalDeviceSurfaceSupportKHR(physical,i,surface_,&supported),"surface support");if(supported){present=i;return true;}}
    return false;
}
void VulkanPresentation::attach(VkPhysicalDevice physical, VkDevice device, uint32_t graphics, uint32_t present) {
    physical_=physical;device_=device;graphics_family_=graphics;present_family_=present;
    vk_.vkGetDeviceQueue(device_,present,0,&present_queue_);
    VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    check(vkCreateSemaphore(device_,&info,nullptr,&acquired_),"create acquire semaphore");
    resize();
}
bool VulkanPresentation::resize() {
    auto size=window_.size();if(size.first<=0||size.second<=0)return false;
    VkSurfaceCapabilitiesKHR caps{};check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_,surface_,&caps),"surface capabilities");
    VkExtent2D extent=caps.currentExtent;
    if(extent.width==UINT32_MAX)extent={std::clamp(uint32_t(size.first),caps.minImageExtent.width,caps.maxImageExtent.width),std::clamp(uint32_t(size.second),caps.minImageExtent.height,caps.maxImageExtent.height)};
    if(!extent.width||!extent.height)return false;
    if(!dirty_&&extent.width==extent_.width&&extent.height==extent_.height)return true;
    uint32_t count=0;check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_,surface_,&count,nullptr),"surface formats");
    std::vector<VkSurfaceFormatKHR> formats(count);check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_,surface_,&count,formats.data()),"surface formats");
    auto format=std::find_if(formats.begin(),formats.end(),[](auto f){return (f.format==VK_FORMAT_B8G8R8A8_UNORM||f.format==VK_FORMAT_R8G8B8A8_UNORM||f.format==VK_FORMAT_UNDEFINED)&&f.colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;});
    if(format==formats.end())throw std::runtime_error("Vulkan presentation requires an RGBA/BGRA UNORM surface");
    VkSurfaceFormatKHR chosen=*format;if(chosen.format==VK_FORMAT_UNDEFINED)chosen.format=VK_FORMAT_B8G8R8A8_UNORM;
    VkFormatProperties features{};vk_.vkGetPhysicalDeviceFormatProperties(physical_,chosen.format,&features);
    if(!(caps.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_DST_BIT)||!(features.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_DST_BIT))throw std::runtime_error("Vulkan surface cannot accept image blits");
    check(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_,surface_,&count,nullptr),"present modes");
    std::vector<VkPresentModeKHR> modes(count);check(vkGetPhysicalDeviceSurfacePresentModesKHR(physical_,surface_,&count,modes.data()),"present modes");
    VkPresentModeKHR mode=VK_PRESENT_MODE_FIFO_KHR;
    if(!window_.vsync)for(auto candidate:{VK_PRESENT_MODE_MAILBOX_KHR,VK_PRESENT_MODE_IMMEDIATE_KHR})if(std::find(modes.begin(),modes.end(),candidate)!=modes.end())mode=candidate;
    check(vk_.vkDeviceWaitIdle(device_),"wait swapchain resize");clear_swapchain();
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};info.surface=surface_;
    info.minImageCount=caps.minImageCount+1;if(caps.maxImageCount)info.minImageCount=std::min(info.minImageCount,caps.maxImageCount);
    info.imageFormat=chosen.format;info.imageColorSpace=chosen.colorSpace;info.imageExtent=extent;info.imageArrayLayers=1;info.imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    uint32_t families[]={graphics_family_,present_family_};info.imageSharingMode=graphics_family_==present_family_?VK_SHARING_MODE_EXCLUSIVE:VK_SHARING_MODE_CONCURRENT;
    if(info.imageSharingMode==VK_SHARING_MODE_CONCURRENT){info.queueFamilyIndexCount=2;info.pQueueFamilyIndices=families;}
    info.preTransform=caps.currentTransform;
    for(auto alpha:{VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})if(caps.supportedCompositeAlpha&alpha){info.compositeAlpha=alpha;break;}
    info.presentMode=mode;info.clipped=VK_TRUE;
    check(vkCreateSwapchainKHR(device_,&info,nullptr,&swapchain_),"create swapchain");
    check(vkGetSwapchainImagesKHR(device_,swapchain_,&count,nullptr),"swapchain images");images_.resize(count);
    check(vkGetSwapchainImagesKHR(device_,swapchain_,&count,images_.data()),"swapchain images");
    finished_.resize(count,VK_NULL_HANDLE);VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for(auto &s:finished_)check(vkCreateSemaphore(device_,&sem,nullptr,&s),"create present semaphore");
    extent_=extent;dirty_=false;return true;
}
void VulkanPresentation::present(VkImage source,int width,int height,VkQueue graphics,VkCommandBuffer command,VkFence fence) {
    check(vk_.vkWaitForFences(device_,1,&fence,VK_TRUE,UINT64_MAX),"wait presentation");
    if(!resize())return;
    uint32_t index=0;auto result=vkAcquireNextImageKHR(device_,swapchain_,UINT64_MAX,acquired_,VK_NULL_HANDLE,&index);
    if(result==VK_ERROR_OUT_OF_DATE_KHR){dirty_=true;return;}
    if(result==VK_SUBOPTIMAL_KHR)dirty_=true;else check(result,"acquire image");
    check(vk_.vkResetCommandBuffer(command,0),"reset presentation command");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vk_.vkBeginCommandBuffer(command,&begin),"begin presentation");
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.image=images_[index];barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    vk_.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    VkClearColorValue black{{0,0,0,1}};vkCmdClearColorImage(command,images_[index],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&black,1,&barrier.subresourceRange);
    // The clear and the subsequent blit both write the target image.
    barrier.oldLayout=barrier.newLayout;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    vk_.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    double scale=std::min(double(extent_.width)/width,double(extent_.height)/height);
    int w=std::max(1,int(width*scale)),h=std::max(1,int(height*scale));int x=(int(extent_.width)-w)/2,y=(int(extent_.height)-h)/2;
    VkImageBlit blit{};blit.srcSubresource=blit.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};blit.srcOffsets[1]={width,height,1};blit.dstOffsets[0]={x,y,0};blit.dstOffsets[1]={x+w,y+h,1};
    vkCmdBlitImage(command,source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,images_[index],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,window_.nearest?VK_FILTER_NEAREST:VK_FILTER_LINEAR);
    barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=0;
    vk_.vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    check(vk_.vkEndCommandBuffer(command),"end presentation");check(vk_.vkResetFences(device_,1,&fence),"reset presentation fence");
    VkPipelineStageFlags wait=VK_PIPELINE_STAGE_TRANSFER_BIT;VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&acquired_;submit.pWaitDstStageMask=&wait;
    submit.commandBufferCount=1;submit.pCommandBuffers=&command;submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&finished_[index];
    check(vk_.vkQueueSubmit(graphics,1,&submit,fence),"submit presentation");
    VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};info.waitSemaphoreCount=1;info.pWaitSemaphores=&finished_[index];info.swapchainCount=1;info.pSwapchains=&swapchain_;info.pImageIndices=&index;
    result=vkQueuePresentKHR(present_queue_,&info);
    if(result==VK_ERROR_OUT_OF_DATE_KHR||result==VK_SUBOPTIMAL_KHR)dirty_=true;else check(result,"present image");
}
}
