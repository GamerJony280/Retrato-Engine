#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include "shared_library.hpp"
#include <stdexcept>
namespace ember {
struct VulkanFunctions {
    platform::detail::SharedLibrary library{
#ifdef _WIN32
        "vulkan-1.dll"
#elif defined(__ANDROID__)
        "libvulkan.so"
#else
        "libvulkan.so.1"
#endif
    };
    PFN_vkGetInstanceProcAddr get_proc=nullptr;
    PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers=nullptr;
    PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets=nullptr;
    PFN_vkAllocateMemory vkAllocateMemory=nullptr;
    PFN_vkBeginCommandBuffer vkBeginCommandBuffer=nullptr;
    PFN_vkBindBufferMemory vkBindBufferMemory=nullptr;
    PFN_vkBindImageMemory vkBindImageMemory=nullptr;
    PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass=nullptr;
    PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets=nullptr;
    PFN_vkCmdBindPipeline vkCmdBindPipeline=nullptr;
    PFN_vkCmdBindVertexBuffers vkCmdBindVertexBuffers=nullptr;
    PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer=nullptr;
    PFN_vkCmdDraw vkCmdDraw=nullptr;
    PFN_vkCmdEndRenderPass vkCmdEndRenderPass=nullptr;
    PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier=nullptr;
    PFN_vkCmdPushConstants vkCmdPushConstants=nullptr;
    PFN_vkCmdSetScissor vkCmdSetScissor=nullptr;
    PFN_vkCmdSetViewport vkCmdSetViewport=nullptr;
    PFN_vkCreateBuffer vkCreateBuffer=nullptr;
    PFN_vkCreateBufferView vkCreateBufferView=nullptr;
    PFN_vkCreateCommandPool vkCreateCommandPool=nullptr;
    PFN_vkCreateDescriptorPool vkCreateDescriptorPool=nullptr;
    PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout=nullptr;
    PFN_vkCreateDevice vkCreateDevice=nullptr;
    PFN_vkCreateFence vkCreateFence=nullptr;
    PFN_vkCreateFramebuffer vkCreateFramebuffer=nullptr;
    PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines=nullptr;
    PFN_vkCreateImage vkCreateImage=nullptr;
    PFN_vkCreateImageView vkCreateImageView=nullptr;
    PFN_vkCreateInstance vkCreateInstance=nullptr;
    PFN_vkCreatePipelineLayout vkCreatePipelineLayout=nullptr;
    PFN_vkCreateRenderPass vkCreateRenderPass=nullptr;
    PFN_vkCreateShaderModule vkCreateShaderModule=nullptr;
    PFN_vkDestroyBuffer vkDestroyBuffer=nullptr;
    PFN_vkDestroyBufferView vkDestroyBufferView=nullptr;
    PFN_vkDestroyCommandPool vkDestroyCommandPool=nullptr;
    PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool=nullptr;
    PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout=nullptr;
    PFN_vkDestroyDevice vkDestroyDevice=nullptr;
    PFN_vkDestroyFence vkDestroyFence=nullptr;
    PFN_vkDestroyFramebuffer vkDestroyFramebuffer=nullptr;
    PFN_vkDestroyImage vkDestroyImage=nullptr;
    PFN_vkDestroyImageView vkDestroyImageView=nullptr;
    PFN_vkDestroyInstance vkDestroyInstance=nullptr;
    PFN_vkDestroyPipeline vkDestroyPipeline=nullptr;
    PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout=nullptr;
    PFN_vkDestroyRenderPass vkDestroyRenderPass=nullptr;
    PFN_vkDestroyShaderModule vkDestroyShaderModule=nullptr;
    PFN_vkDeviceWaitIdle vkDeviceWaitIdle=nullptr;
    PFN_vkEndCommandBuffer vkEndCommandBuffer=nullptr;
    PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices=nullptr;
    PFN_vkFreeMemory vkFreeMemory=nullptr;
    PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements=nullptr;
    PFN_vkGetDeviceQueue vkGetDeviceQueue=nullptr;
    PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements=nullptr;
    PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties=nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties=nullptr;
    PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties=nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties=nullptr;
    PFN_vkMapMemory vkMapMemory=nullptr;
    PFN_vkQueueSubmit vkQueueSubmit=nullptr;
    PFN_vkResetCommandBuffer vkResetCommandBuffer=nullptr;
    PFN_vkResetFences vkResetFences=nullptr;
    PFN_vkUnmapMemory vkUnmapMemory=nullptr;
    PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets=nullptr;
    PFN_vkWaitForFences vkWaitForFences=nullptr;
    void load_global(){
        get_proc=reinterpret_cast<PFN_vkGetInstanceProcAddr>(library.symbol("vkGetInstanceProcAddr"));
        if(!get_proc)throw std::runtime_error("Vulkan loader has no vkGetInstanceProcAddr");
        vkCreateInstance=reinterpret_cast<PFN_vkCreateInstance>(get_proc(VK_NULL_HANDLE,"vkCreateInstance"));
        if(!vkCreateInstance)throw std::runtime_error("Vulkan loader has no vkCreateInstance");
    }
    void load_instance(VkInstance instance){
        vkDestroyInstance=reinterpret_cast<PFN_vkDestroyInstance>(get_proc(instance,"vkDestroyInstance")); if(!vkDestroyInstance)throw std::runtime_error("Vulkan function unavailable: vkDestroyInstance");
        vkAllocateCommandBuffers=reinterpret_cast<PFN_vkAllocateCommandBuffers>(get_proc(instance,"vkAllocateCommandBuffers")); if(!vkAllocateCommandBuffers)throw std::runtime_error("Vulkan function unavailable: vkAllocateCommandBuffers");
        vkAllocateDescriptorSets=reinterpret_cast<PFN_vkAllocateDescriptorSets>(get_proc(instance,"vkAllocateDescriptorSets")); if(!vkAllocateDescriptorSets)throw std::runtime_error("Vulkan function unavailable: vkAllocateDescriptorSets");
        vkAllocateMemory=reinterpret_cast<PFN_vkAllocateMemory>(get_proc(instance,"vkAllocateMemory")); if(!vkAllocateMemory)throw std::runtime_error("Vulkan function unavailable: vkAllocateMemory");
        vkBeginCommandBuffer=reinterpret_cast<PFN_vkBeginCommandBuffer>(get_proc(instance,"vkBeginCommandBuffer")); if(!vkBeginCommandBuffer)throw std::runtime_error("Vulkan function unavailable: vkBeginCommandBuffer");
        vkBindBufferMemory=reinterpret_cast<PFN_vkBindBufferMemory>(get_proc(instance,"vkBindBufferMemory")); if(!vkBindBufferMemory)throw std::runtime_error("Vulkan function unavailable: vkBindBufferMemory");
        vkBindImageMemory=reinterpret_cast<PFN_vkBindImageMemory>(get_proc(instance,"vkBindImageMemory")); if(!vkBindImageMemory)throw std::runtime_error("Vulkan function unavailable: vkBindImageMemory");
        vkCmdBeginRenderPass=reinterpret_cast<PFN_vkCmdBeginRenderPass>(get_proc(instance,"vkCmdBeginRenderPass")); if(!vkCmdBeginRenderPass)throw std::runtime_error("Vulkan function unavailable: vkCmdBeginRenderPass");
        vkCmdBindDescriptorSets=reinterpret_cast<PFN_vkCmdBindDescriptorSets>(get_proc(instance,"vkCmdBindDescriptorSets")); if(!vkCmdBindDescriptorSets)throw std::runtime_error("Vulkan function unavailable: vkCmdBindDescriptorSets");
        vkCmdBindPipeline=reinterpret_cast<PFN_vkCmdBindPipeline>(get_proc(instance,"vkCmdBindPipeline")); if(!vkCmdBindPipeline)throw std::runtime_error("Vulkan function unavailable: vkCmdBindPipeline");
        vkCmdBindVertexBuffers=reinterpret_cast<PFN_vkCmdBindVertexBuffers>(get_proc(instance,"vkCmdBindVertexBuffers")); if(!vkCmdBindVertexBuffers)throw std::runtime_error("Vulkan function unavailable: vkCmdBindVertexBuffers");
        vkCmdCopyImageToBuffer=reinterpret_cast<PFN_vkCmdCopyImageToBuffer>(get_proc(instance,"vkCmdCopyImageToBuffer")); if(!vkCmdCopyImageToBuffer)throw std::runtime_error("Vulkan function unavailable: vkCmdCopyImageToBuffer");
        vkCmdDraw=reinterpret_cast<PFN_vkCmdDraw>(get_proc(instance,"vkCmdDraw")); if(!vkCmdDraw)throw std::runtime_error("Vulkan function unavailable: vkCmdDraw");
        vkCmdEndRenderPass=reinterpret_cast<PFN_vkCmdEndRenderPass>(get_proc(instance,"vkCmdEndRenderPass")); if(!vkCmdEndRenderPass)throw std::runtime_error("Vulkan function unavailable: vkCmdEndRenderPass");
        vkCmdPipelineBarrier=reinterpret_cast<PFN_vkCmdPipelineBarrier>(get_proc(instance,"vkCmdPipelineBarrier")); if(!vkCmdPipelineBarrier)throw std::runtime_error("Vulkan function unavailable: vkCmdPipelineBarrier");
        vkCmdPushConstants=reinterpret_cast<PFN_vkCmdPushConstants>(get_proc(instance,"vkCmdPushConstants")); if(!vkCmdPushConstants)throw std::runtime_error("Vulkan function unavailable: vkCmdPushConstants");
        vkCmdSetScissor=reinterpret_cast<PFN_vkCmdSetScissor>(get_proc(instance,"vkCmdSetScissor")); if(!vkCmdSetScissor)throw std::runtime_error("Vulkan function unavailable: vkCmdSetScissor");
        vkCmdSetViewport=reinterpret_cast<PFN_vkCmdSetViewport>(get_proc(instance,"vkCmdSetViewport")); if(!vkCmdSetViewport)throw std::runtime_error("Vulkan function unavailable: vkCmdSetViewport");
        vkCreateBuffer=reinterpret_cast<PFN_vkCreateBuffer>(get_proc(instance,"vkCreateBuffer")); if(!vkCreateBuffer)throw std::runtime_error("Vulkan function unavailable: vkCreateBuffer");
        vkCreateBufferView=reinterpret_cast<PFN_vkCreateBufferView>(get_proc(instance,"vkCreateBufferView")); if(!vkCreateBufferView)throw std::runtime_error("Vulkan function unavailable: vkCreateBufferView");
        vkCreateCommandPool=reinterpret_cast<PFN_vkCreateCommandPool>(get_proc(instance,"vkCreateCommandPool")); if(!vkCreateCommandPool)throw std::runtime_error("Vulkan function unavailable: vkCreateCommandPool");
        vkCreateDescriptorPool=reinterpret_cast<PFN_vkCreateDescriptorPool>(get_proc(instance,"vkCreateDescriptorPool")); if(!vkCreateDescriptorPool)throw std::runtime_error("Vulkan function unavailable: vkCreateDescriptorPool");
        vkCreateDescriptorSetLayout=reinterpret_cast<PFN_vkCreateDescriptorSetLayout>(get_proc(instance,"vkCreateDescriptorSetLayout")); if(!vkCreateDescriptorSetLayout)throw std::runtime_error("Vulkan function unavailable: vkCreateDescriptorSetLayout");
        vkCreateDevice=reinterpret_cast<PFN_vkCreateDevice>(get_proc(instance,"vkCreateDevice")); if(!vkCreateDevice)throw std::runtime_error("Vulkan function unavailable: vkCreateDevice");
        vkCreateFence=reinterpret_cast<PFN_vkCreateFence>(get_proc(instance,"vkCreateFence")); if(!vkCreateFence)throw std::runtime_error("Vulkan function unavailable: vkCreateFence");
        vkCreateFramebuffer=reinterpret_cast<PFN_vkCreateFramebuffer>(get_proc(instance,"vkCreateFramebuffer")); if(!vkCreateFramebuffer)throw std::runtime_error("Vulkan function unavailable: vkCreateFramebuffer");
        vkCreateGraphicsPipelines=reinterpret_cast<PFN_vkCreateGraphicsPipelines>(get_proc(instance,"vkCreateGraphicsPipelines")); if(!vkCreateGraphicsPipelines)throw std::runtime_error("Vulkan function unavailable: vkCreateGraphicsPipelines");
        vkCreateImage=reinterpret_cast<PFN_vkCreateImage>(get_proc(instance,"vkCreateImage")); if(!vkCreateImage)throw std::runtime_error("Vulkan function unavailable: vkCreateImage");
        vkCreateImageView=reinterpret_cast<PFN_vkCreateImageView>(get_proc(instance,"vkCreateImageView")); if(!vkCreateImageView)throw std::runtime_error("Vulkan function unavailable: vkCreateImageView");
        vkCreatePipelineLayout=reinterpret_cast<PFN_vkCreatePipelineLayout>(get_proc(instance,"vkCreatePipelineLayout")); if(!vkCreatePipelineLayout)throw std::runtime_error("Vulkan function unavailable: vkCreatePipelineLayout");
        vkCreateRenderPass=reinterpret_cast<PFN_vkCreateRenderPass>(get_proc(instance,"vkCreateRenderPass")); if(!vkCreateRenderPass)throw std::runtime_error("Vulkan function unavailable: vkCreateRenderPass");
        vkCreateShaderModule=reinterpret_cast<PFN_vkCreateShaderModule>(get_proc(instance,"vkCreateShaderModule")); if(!vkCreateShaderModule)throw std::runtime_error("Vulkan function unavailable: vkCreateShaderModule");
        vkDestroyBuffer=reinterpret_cast<PFN_vkDestroyBuffer>(get_proc(instance,"vkDestroyBuffer")); if(!vkDestroyBuffer)throw std::runtime_error("Vulkan function unavailable: vkDestroyBuffer");
        vkDestroyBufferView=reinterpret_cast<PFN_vkDestroyBufferView>(get_proc(instance,"vkDestroyBufferView")); if(!vkDestroyBufferView)throw std::runtime_error("Vulkan function unavailable: vkDestroyBufferView");
        vkDestroyCommandPool=reinterpret_cast<PFN_vkDestroyCommandPool>(get_proc(instance,"vkDestroyCommandPool")); if(!vkDestroyCommandPool)throw std::runtime_error("Vulkan function unavailable: vkDestroyCommandPool");
        vkDestroyDescriptorPool=reinterpret_cast<PFN_vkDestroyDescriptorPool>(get_proc(instance,"vkDestroyDescriptorPool")); if(!vkDestroyDescriptorPool)throw std::runtime_error("Vulkan function unavailable: vkDestroyDescriptorPool");
        vkDestroyDescriptorSetLayout=reinterpret_cast<PFN_vkDestroyDescriptorSetLayout>(get_proc(instance,"vkDestroyDescriptorSetLayout")); if(!vkDestroyDescriptorSetLayout)throw std::runtime_error("Vulkan function unavailable: vkDestroyDescriptorSetLayout");
        vkDestroyDevice=reinterpret_cast<PFN_vkDestroyDevice>(get_proc(instance,"vkDestroyDevice")); if(!vkDestroyDevice)throw std::runtime_error("Vulkan function unavailable: vkDestroyDevice");
        vkDestroyFence=reinterpret_cast<PFN_vkDestroyFence>(get_proc(instance,"vkDestroyFence")); if(!vkDestroyFence)throw std::runtime_error("Vulkan function unavailable: vkDestroyFence");
        vkDestroyFramebuffer=reinterpret_cast<PFN_vkDestroyFramebuffer>(get_proc(instance,"vkDestroyFramebuffer")); if(!vkDestroyFramebuffer)throw std::runtime_error("Vulkan function unavailable: vkDestroyFramebuffer");
        vkDestroyImage=reinterpret_cast<PFN_vkDestroyImage>(get_proc(instance,"vkDestroyImage")); if(!vkDestroyImage)throw std::runtime_error("Vulkan function unavailable: vkDestroyImage");
        vkDestroyImageView=reinterpret_cast<PFN_vkDestroyImageView>(get_proc(instance,"vkDestroyImageView")); if(!vkDestroyImageView)throw std::runtime_error("Vulkan function unavailable: vkDestroyImageView");
        vkDestroyPipeline=reinterpret_cast<PFN_vkDestroyPipeline>(get_proc(instance,"vkDestroyPipeline")); if(!vkDestroyPipeline)throw std::runtime_error("Vulkan function unavailable: vkDestroyPipeline");
        vkDestroyPipelineLayout=reinterpret_cast<PFN_vkDestroyPipelineLayout>(get_proc(instance,"vkDestroyPipelineLayout")); if(!vkDestroyPipelineLayout)throw std::runtime_error("Vulkan function unavailable: vkDestroyPipelineLayout");
        vkDestroyRenderPass=reinterpret_cast<PFN_vkDestroyRenderPass>(get_proc(instance,"vkDestroyRenderPass")); if(!vkDestroyRenderPass)throw std::runtime_error("Vulkan function unavailable: vkDestroyRenderPass");
        vkDestroyShaderModule=reinterpret_cast<PFN_vkDestroyShaderModule>(get_proc(instance,"vkDestroyShaderModule")); if(!vkDestroyShaderModule)throw std::runtime_error("Vulkan function unavailable: vkDestroyShaderModule");
        vkDeviceWaitIdle=reinterpret_cast<PFN_vkDeviceWaitIdle>(get_proc(instance,"vkDeviceWaitIdle")); if(!vkDeviceWaitIdle)throw std::runtime_error("Vulkan function unavailable: vkDeviceWaitIdle");
        vkEndCommandBuffer=reinterpret_cast<PFN_vkEndCommandBuffer>(get_proc(instance,"vkEndCommandBuffer")); if(!vkEndCommandBuffer)throw std::runtime_error("Vulkan function unavailable: vkEndCommandBuffer");
        vkEnumeratePhysicalDevices=reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(get_proc(instance,"vkEnumeratePhysicalDevices")); if(!vkEnumeratePhysicalDevices)throw std::runtime_error("Vulkan function unavailable: vkEnumeratePhysicalDevices");
        vkFreeMemory=reinterpret_cast<PFN_vkFreeMemory>(get_proc(instance,"vkFreeMemory")); if(!vkFreeMemory)throw std::runtime_error("Vulkan function unavailable: vkFreeMemory");
        vkGetBufferMemoryRequirements=reinterpret_cast<PFN_vkGetBufferMemoryRequirements>(get_proc(instance,"vkGetBufferMemoryRequirements")); if(!vkGetBufferMemoryRequirements)throw std::runtime_error("Vulkan function unavailable: vkGetBufferMemoryRequirements");
        vkGetDeviceQueue=reinterpret_cast<PFN_vkGetDeviceQueue>(get_proc(instance,"vkGetDeviceQueue")); if(!vkGetDeviceQueue)throw std::runtime_error("Vulkan function unavailable: vkGetDeviceQueue");
        vkGetImageMemoryRequirements=reinterpret_cast<PFN_vkGetImageMemoryRequirements>(get_proc(instance,"vkGetImageMemoryRequirements")); if(!vkGetImageMemoryRequirements)throw std::runtime_error("Vulkan function unavailable: vkGetImageMemoryRequirements");
        vkGetPhysicalDeviceFormatProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(get_proc(instance,"vkGetPhysicalDeviceFormatProperties")); if(!vkGetPhysicalDeviceFormatProperties)throw std::runtime_error("Vulkan function unavailable: vkGetPhysicalDeviceFormatProperties");
        vkGetPhysicalDeviceMemoryProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(get_proc(instance,"vkGetPhysicalDeviceMemoryProperties")); if(!vkGetPhysicalDeviceMemoryProperties)throw std::runtime_error("Vulkan function unavailable: vkGetPhysicalDeviceMemoryProperties");
        vkGetPhysicalDeviceProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(get_proc(instance,"vkGetPhysicalDeviceProperties")); if(!vkGetPhysicalDeviceProperties)throw std::runtime_error("Vulkan function unavailable: vkGetPhysicalDeviceProperties");
        vkGetPhysicalDeviceQueueFamilyProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(get_proc(instance,"vkGetPhysicalDeviceQueueFamilyProperties")); if(!vkGetPhysicalDeviceQueueFamilyProperties)throw std::runtime_error("Vulkan function unavailable: vkGetPhysicalDeviceQueueFamilyProperties");
        vkMapMemory=reinterpret_cast<PFN_vkMapMemory>(get_proc(instance,"vkMapMemory")); if(!vkMapMemory)throw std::runtime_error("Vulkan function unavailable: vkMapMemory");
        vkQueueSubmit=reinterpret_cast<PFN_vkQueueSubmit>(get_proc(instance,"vkQueueSubmit")); if(!vkQueueSubmit)throw std::runtime_error("Vulkan function unavailable: vkQueueSubmit");
        vkResetCommandBuffer=reinterpret_cast<PFN_vkResetCommandBuffer>(get_proc(instance,"vkResetCommandBuffer")); if(!vkResetCommandBuffer)throw std::runtime_error("Vulkan function unavailable: vkResetCommandBuffer");
        vkResetFences=reinterpret_cast<PFN_vkResetFences>(get_proc(instance,"vkResetFences")); if(!vkResetFences)throw std::runtime_error("Vulkan function unavailable: vkResetFences");
        vkUnmapMemory=reinterpret_cast<PFN_vkUnmapMemory>(get_proc(instance,"vkUnmapMemory")); if(!vkUnmapMemory)throw std::runtime_error("Vulkan function unavailable: vkUnmapMemory");
        vkUpdateDescriptorSets=reinterpret_cast<PFN_vkUpdateDescriptorSets>(get_proc(instance,"vkUpdateDescriptorSets")); if(!vkUpdateDescriptorSets)throw std::runtime_error("Vulkan function unavailable: vkUpdateDescriptorSets");
        vkWaitForFences=reinterpret_cast<PFN_vkWaitForFences>(get_proc(instance,"vkWaitForFences")); if(!vkWaitForFences)throw std::runtime_error("Vulkan function unavailable: vkWaitForFences");
    }
};
}
