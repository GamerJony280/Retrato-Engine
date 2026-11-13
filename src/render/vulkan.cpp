#include "ember/render.hpp"
#include "spirv_sources.hpp"
#include "vulkan_window.hpp"
#include <array>
#include <cstring>
#include <stdexcept>
#include "../../platform/common/vulkan_functions.hpp"
namespace ember {
namespace {
void check(VkResult r, const char *message) {
    if (r != VK_SUCCESS)
        throw std::runtime_error(std::string(message) + " (VkResult " + std::to_string(r) + ")");
}
struct Buffer {
    VkBuffer handle{};
    VkDeviceMemory memory{};
    VkDeviceSize size = 0;
    void *mapped = nullptr;
};
struct Image {
    VkImage handle{};
    VkDeviceMemory memory{};
    VkImageView view{};
};
class Vulkan final : public Backend, private VulkanFunctions {
    std::unique_ptr<detail::VulkanPresentation> presentation_;
    VkPhysicalDeviceProperties properties_{};
    VkFormat depth_format_ = VK_FORMAT_D32_SFLOAT;
    VkInstance instance_{};
    VkPhysicalDevice physical_{};
    VkDevice device_{};
    VkQueue queue_{};
    uint32_t family_ = 0;
    VkCommandPool pool_{};
    VkCommandBuffer command_{};
    VkFence fence_{};
    VkRenderPass renderpass_{};
    VkPipelineLayout layout_{};
    VkPipeline pipeline_{}, painter_{};
    VkShaderModule vs_{}, fs_{};
    VkFramebuffer framebuffer_{};
    Image color_, depth_;
    Buffer vertices_, download_, resources_;
    VkBufferView resource_view_{};
    VkDescriptorSetLayout descriptor_layout_{};
    VkDescriptorPool descriptor_pool_{};
    VkDescriptorSet descriptor_{};
    uint64_t resource_revision_ = 0;

    int width_ = 0, height_ = 0;
    std::vector<uint32_t> pixels_;
    uint32_t memory_type(uint32_t mask, VkMemoryPropertyFlags flags) {
        VkPhysicalDeviceMemoryProperties p;
        vkGetPhysicalDeviceMemoryProperties(physical_, &p);
        for (uint32_t i = 0; i < p.memoryTypeCount; i++)
            if ((mask & (1u << i)) && (p.memoryTypes[i].propertyFlags & flags) == flags)
                return i;
        throw std::runtime_error("Vulkan memory type unavailable");
    }
    void destroy(Buffer &b) {
        if (b.mapped)
            vkUnmapMemory(device_, b.memory);
        if (b.handle)
            vkDestroyBuffer(device_, b.handle, nullptr);
        if (b.memory)
            vkFreeMemory(device_, b.memory, nullptr);
        b = {};
    }
    void destroy(Image &i) {
        if (i.view)
            vkDestroyImageView(device_, i.view, nullptr);
        if (i.handle)
            vkDestroyImage(device_, i.handle, nullptr);
        if (i.memory)
            vkFreeMemory(device_, i.memory, nullptr);
        i = {};
    }
    void buffer(Buffer &b, VkDeviceSize size, VkBufferUsageFlags usage) {
        destroy(b);
        b.size = size;
        VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        ci.size = size;
        ci.usage = usage;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(device_, &ci, nullptr, &b.handle), "create buffer");
        VkMemoryRequirements r;
        vkGetBufferMemoryRequirements(device_, b.handle, &r);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = r.size;
        ai.memoryTypeIndex = memory_type(r.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(vkAllocateMemory(device_, &ai, nullptr, &b.memory), "allocate buffer");
        check(vkBindBufferMemory(device_, b.handle, b.memory, 0), "bind buffer");
        check(vkMapMemory(device_, b.memory, 0, size, 0, &b.mapped), "map buffer");
    }
    void image(Image &i, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect) {
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = format;
        ci.extent = {uint32_t(width_), uint32_t(height_), 1};
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = usage;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateImage(device_, &ci, nullptr, &i.handle), "create image");
        VkMemoryRequirements r;
        vkGetImageMemoryRequirements(device_, i.handle, &r);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = r.size;
        ai.memoryTypeIndex = memory_type(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device_, &ai, nullptr, &i.memory), "allocate image");
        check(vkBindImageMemory(device_, i.handle, i.memory, 0), "bind image");
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = i.handle;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format;
        vi.subresourceRange = {aspect, 0, 1, 0, 1};
        check(vkCreateImageView(device_, &vi, nullptr, &i.view), "image view");
    }
    void cleanup() {
        presentation_.reset();
        if (device_) {
            vkDeviceWaitIdle(device_);
            if (resource_view_)
                vkDestroyBufferView(device_, resource_view_, nullptr);
            destroy(resources_);
            if (descriptor_pool_)
                vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
            destroy(vertices_);
            destroy(download_);
            if (framebuffer_)
                vkDestroyFramebuffer(device_, framebuffer_, nullptr);
            destroy(color_);
            destroy(depth_);
            if (pipeline_)
                vkDestroyPipeline(device_, pipeline_, nullptr);
            if (painter_)
                vkDestroyPipeline(device_, painter_, nullptr);
            if (layout_)
                vkDestroyPipelineLayout(device_, layout_, nullptr);
            if (descriptor_layout_)
                vkDestroyDescriptorSetLayout(device_, descriptor_layout_, nullptr);
            if (vs_)
                vkDestroyShaderModule(device_, vs_, nullptr);
            if (fs_)
                vkDestroyShaderModule(device_, fs_, nullptr);
            if (renderpass_)
                vkDestroyRenderPass(device_, renderpass_, nullptr);
            if (fence_)
                vkDestroyFence(device_, fence_, nullptr);
            if (pool_)
                vkDestroyCommandPool(device_, pool_, nullptr);
            vkDestroyDevice(device_, nullptr);
        }
        if (instance_ && vkDestroyInstance)
            vkDestroyInstance(instance_, nullptr);
        device_ = VK_NULL_HANDLE;
        instance_ = VK_NULL_HANDLE;
    }
    VkShaderModule module(const unsigned char *bytes, size_t size) {
        std::vector<uint32_t> words(size / 4);
        std::memcpy(words.data(), bytes, size);
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = size;
        ci.pCode = words.data();
        VkShaderModule s;
        check(vkCreateShaderModule(device_, &ci, nullptr, &s), "shader module");
        return s;
    }
    VkPipeline make_pipeline(bool depth) {
        VkPipelineShaderStageCreateInfo stages[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
                                                     {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs_;
        stages[0].pName = "main";
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fs_;
        stages[1].pName = "main";
        VkVertexInputBindingDescription binding{0, sizeof(DrawVertex), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attrs[8]{};
        for(uint32_t i=0;i<8;++i) {
            auto a=vertex_attributes[i];
            VkFormat format=a.components==2?VK_FORMAT_R32G32_SFLOAT:a.components==3?VK_FORMAT_R32G32B32_SFLOAT:VK_FORMAT_R32G32B32A32_SFLOAT;
            attrs[i]={i,0,format,a.offset};
        }
        VkPipelineVertexInputStateCreateInfo vertex{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertex.vertexBindingDescriptionCount = 1;
        vertex.pVertexBindingDescriptions = &binding;
        vertex.vertexAttributeDescriptionCount = 8;
        vertex.pVertexAttributeDescriptions = attrs;
        VkPipelineInputAssemblyStateCreateInfo assembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        ds.depthTestEnable = depth;
        ds.depthWriteEnable = depth;
        ds.depthCompareOp = VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.colorWriteMask = 15;
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1;
        blend.pAttachments = &attachment;
        VkDynamicState dynamic[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates = dynamic;
        VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        ci.stageCount = 2;
        ci.pStages = stages;
        ci.pVertexInputState = &vertex;
        ci.pInputAssemblyState = &assembly;
        ci.pViewportState = &viewport;
        ci.pRasterizationState = &raster;
        ci.pMultisampleState = &ms;
        ci.pDepthStencilState = &ds;
        ci.pColorBlendState = &blend;
        ci.pDynamicState = &dyn;
        ci.layout = layout_;
        ci.renderPass = renderpass_;
        VkPipeline p;
        check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &ci, nullptr, &p), "graphics pipeline");
        return p;
    }

  public:
    explicit Vulkan(detail::VulkanWindow *window = nullptr) {
        try {
            load_global();
            VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
            app.pApplicationName = "Retrato";
            app.apiVersion = VK_API_VERSION_1_0;
            VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
            ci.pApplicationInfo = &app;
            if (window) {
                ci.enabledExtensionCount = uint32_t(window->extensions.size());
                ci.ppEnabledExtensionNames = window->extensions.data();
            }
            check(vkCreateInstance(&ci, nullptr, &instance_), "Vulkan instance");
            load_instance(instance_);
            if (window) presentation_ = std::make_unique<detail::VulkanPresentation>(static_cast<VulkanFunctions &>(*this), instance_, *window);
            uint32_t present_family = 0;
            uint32_t n = 0;
            check(vkEnumeratePhysicalDevices(instance_, &n, nullptr), "enumerate GPUs");
            std::vector<VkPhysicalDevice> devices(n);
            check(vkEnumeratePhysicalDevices(instance_, &n, devices.data()), "enumerate GPUs");
            for (auto d : devices) {
                VkFormatProperties formats{};
                bool depth_supported = false;
                for (auto format : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM}) {
                    vkGetPhysicalDeviceFormatProperties(d, format, &formats);
                    if (formats.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                        depth_format_ = format; depth_supported = true; break;
                    }
                }
                if (!depth_supported) continue;
                vkGetPhysicalDeviceFormatProperties(d, VK_FORMAT_R8G8B8A8_UNORM, &formats);
                VkFormatFeatureFlags required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
                if (window) {
                    required |= VK_FORMAT_FEATURE_BLIT_SRC_BIT;
                    if (!window->nearest) required |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
                }
                if ((formats.optimalTilingFeatures & required) != required) continue;
                uint32_t count;
                vkGetPhysicalDeviceQueueFamilyProperties(d, &count, nullptr);
                std::vector<VkQueueFamilyProperties> props(count);
                vkGetPhysicalDeviceQueueFamilyProperties(d, &count, props.data());
                for (uint32_t i = 0; i < count; i++)
                    if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                        (!presentation_ || presentation_->supports(d, i, present_family))) {
                        physical_ = d;
                        family_ = i;
                        break;
                    }
                if (physical_)
                    break;
            }
            if (!physical_)
                throw std::runtime_error("No Vulkan graphics queue");
            vkGetPhysicalDeviceProperties(physical_, &properties_);
            float priority = 1;
            VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            qi.queueFamilyIndex = family_;
            qi.queueCount = 1;
            qi.pQueuePriorities = &priority;
            VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
            VkDeviceQueueCreateInfo queues[] = {qi, qi};
            queues[1].queueFamilyIndex = present_family;
            di.queueCreateInfoCount = presentation_ && present_family != family_ ? 2 : 1;
            di.pQueueCreateInfos = queues;
            const char *swapchain_extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
            if (presentation_) { di.enabledExtensionCount = 1; di.ppEnabledExtensionNames = &swapchain_extension; }
            check(vkCreateDevice(physical_, &di, nullptr, &device_), "logical device");
            vkGetDeviceQueue(device_, family_, 0, &queue_);
            if (presentation_) presentation_->attach(physical_, device_, family_, present_family);
            VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pi.queueFamilyIndex = family_;
            pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            check(vkCreateCommandPool(device_, &pi, nullptr, &pool_), "command pool");
            VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            ca.commandPool = pool_;
            ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            ca.commandBufferCount = 1;
            check(vkAllocateCommandBuffers(device_, &ca, &command_), "command buffer");
            VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            check(vkCreateFence(device_, &fi, nullptr, &fence_), "fence");
            VkAttachmentDescription attachments[2]{};
            attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
            attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
            attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            attachments[1].format = depth_format_;
            attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
            attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                depth{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
            VkSubpassDescription sub{};
            sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            sub.colorAttachmentCount = 1;
            sub.pColorAttachments = &color;
            sub.pDepthStencilAttachment = &depth;
            VkSubpassDependency dep{};
            dep.srcSubpass = 0;
            dep.dstSubpass = VK_SUBPASS_EXTERNAL;
            dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dep.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            dep.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
            rp.attachmentCount = 2;
            rp.pAttachments = attachments;
            rp.subpassCount = 1;
            rp.pSubpasses = &sub;
            rp.dependencyCount = 1;
            rp.pDependencies = &dep;
            check(vkCreateRenderPass(device_, &rp, nullptr, &renderpass_), "render pass");
            VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1,
                                                 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
            VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            dl.bindingCount = 1;
            dl.pBindings = &binding;
            check(vkCreateDescriptorSetLayout(device_, &dl, nullptr, &descriptor_layout_),
                  "descriptor layout");
            VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1};
            VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            dp.maxSets = 1;
            dp.poolSizeCount = 1;
            dp.pPoolSizes = &ps;
            check(vkCreateDescriptorPool(device_, &dp, nullptr, &descriptor_pool_), "descriptor pool");
            VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            da.descriptorPool = descriptor_pool_;
            da.descriptorSetCount = 1;
            da.pSetLayouts = &descriptor_layout_;
            check(vkAllocateDescriptorSets(device_, &da, &descriptor_), "descriptor set");
            VkPushConstantRange range{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ShaderSettings)};
            VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            li.setLayoutCount = 1;
            li.pSetLayouts = &descriptor_layout_;
            li.pushConstantRangeCount = 1;
            li.pPushConstantRanges = &range;
            check(vkCreatePipelineLayout(device_, &li, nullptr, &layout_), "pipeline layout");
            vs_ = module(spirv_vertex, sizeof spirv_vertex);
            fs_ = module(spirv_fragment, sizeof spirv_fragment);
            pipeline_ = make_pipeline(true);
            painter_ = make_pipeline(false);
        } catch (...) {
            cleanup();
            throw;
        }
    }
    ~Vulkan() { cleanup(); }
    const char *name() const override { return "vulkan"; }
    std::string device_description() const override {
        return properties_.deviceName;
    }
    void draw(const Frame &f) override {
        validate_frame(f);
        if (uint32_t(f.width) > properties_.limits.maxImageDimension2D ||
            uint32_t(f.height) > properties_.limits.maxImageDimension2D)
            throw std::runtime_error("Frame dimensions exceed Vulkan device limits");
        check(vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX), "wait frame");
        if (width_ != f.width || height_ != f.height) {
            if (framebuffer_)
                vkDestroyFramebuffer(device_, framebuffer_, nullptr);
            framebuffer_ = VK_NULL_HANDLE;
            destroy(color_);
            destroy(depth_);
            width_ = f.width;
            height_ = f.height;
            image(color_, VK_FORMAT_R8G8B8A8_UNORM,
                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                  VK_IMAGE_ASPECT_COLOR_BIT);
            image(depth_, depth_format_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                  VK_IMAGE_ASPECT_DEPTH_BIT);
            VkImageView views[] = {color_.view, depth_.view};
            VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fb.renderPass = renderpass_;
            fb.attachmentCount = 2;
            fb.pAttachments = views;
            fb.width = width_;
            fb.height = height_;
            fb.layers = 1;
            check(vkCreateFramebuffer(device_, &fb, nullptr, &framebuffer_), "framebuffer");
            destroy(download_); // Capture storage is allocated only by readback().
        }
        size_t resource_bytes = f.resources.size() * 4;
        if (f.resources.size() > properties_.limits.maxTexelBufferElements)
            throw std::runtime_error("Texture resources exceed this GPU's buffer limit");
        const size_t allocation_bytes = std::max<size_t>(4, resource_bytes);
        bool new_buffer = resources_.size != allocation_bytes;
        if (new_buffer) {
            if (resource_view_)
                vkDestroyBufferView(device_, resource_view_, nullptr);
            resource_view_ = VK_NULL_HANDLE;
            buffer(resources_, allocation_bytes, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT);
            VkBufferViewCreateInfo vi{VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO};
            vi.buffer = resources_.handle;
            vi.format = VK_FORMAT_R32_UINT;
            vi.range = allocation_bytes;
            check(vkCreateBufferView(device_, &vi, nullptr, &resource_view_), "resource view");
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = descriptor_;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
            write.pTexelBufferView = &resource_view_;
            vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
        }
        size_t resource_start =
            new_buffer || resource_revision_ != f.texture_revision ? 0 : f.texture_words * 4;
        if (resource_bytes > resource_start)
            std::memcpy((char *)resources_.mapped + resource_start,
                        (const char *)f.resources.data() + resource_start, resource_bytes - resource_start);
        resource_revision_ = f.texture_revision;
        size_t bytes = f.vertices.size() * sizeof(DrawVertex);
        if (bytes > vertices_.size)
            buffer(vertices_, std::max<size_t>(4096, bytes * 2), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        if (bytes)
            std::memcpy(vertices_.mapped, f.vertices.data(), bytes);
        check(vkResetCommandBuffer(command_, 0), "reset command");
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(command_, &bi), "begin command");
        VkClearValue clears[2]{};
        clears[0].color = {{.09f, .14f, .21f, 1}};
        clears[1].depthStencil = {1, 0};
        VkRenderPassBeginInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        ri.renderPass = renderpass_;
        ri.framebuffer = framebuffer_;
        ri.renderArea.extent = {uint32_t(width_), uint32_t(height_)};
        ri.clearValueCount = 2;
        ri.pClearValues = clears;
        vkCmdBeginRenderPass(command_, &ri, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(command_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          frame_depth_test(f) ? pipeline_ : painter_);
        VkViewport viewport{0, 0, float(width_), float(height_), 0, 1};
        VkRect2D scissor{{0, 0}, {uint32_t(width_), uint32_t(height_)}};
        vkCmdSetViewport(command_, 0, 1, &viewport);
        vkCmdSetScissor(command_, 0, 1, &scissor);
        vkCmdBindDescriptorSets(command_, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &descriptor_, 0,
                                nullptr);
        vkCmdPushConstants(command_, layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(f.settings),
                           &f.settings);
        if (bytes) {
            VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(command_, 0, 1, &vertices_.handle, &offset);
            vkCmdDraw(command_, uint32_t(f.vertices.size()), 1, 0, 0);
        }
        vkCmdEndRenderPass(command_);
        check(vkEndCommandBuffer(command_), "end command");
        check(vkResetFences(device_, 1, &fence_), "reset fence");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command_;
        check(vkQueueSubmit(queue_, 1, &submit, fence_), "submit frame");
    }
    bool direct_presentation() const noexcept override { return bool(presentation_); }
    bool present() override {
        if (!presentation_) return false;
        if (width_ && height_) presentation_->present(color_.handle, width_, height_, queue_, command_, fence_);
        return true;
    }
    const std::vector<uint32_t> &readback() override {
        if (width_ == 0 || height_ == 0)
            return pixels_;
        check(vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX), "wait readback");
        if (!download_.handle) buffer(download_, size_t(width_) * height_ * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        check(vkResetCommandBuffer(command_, 0), "reset readback");
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(command_, &bi), "begin readback");
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {uint32_t(width_), uint32_t(height_), 1};
        vkCmdCopyImageToBuffer(command_, color_.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               download_.handle, 1, &copy);
        VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = download_.handle;
        barrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(command_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0,
                             nullptr, 1, &barrier, 0, nullptr);
        check(vkEndCommandBuffer(command_), "end readback");
        check(vkResetFences(device_, 1, &fence_), "reset fence");
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command_;
        check(vkQueueSubmit(queue_, 1, &submit, fence_), "submit readback");
        check(vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX), "readback ready");
        pixels_.resize(size_t(width_) * height_);
        std::memcpy(pixels_.data(), download_.mapped, pixels_.size() * 4);
        return pixels_;
    }
};
} // namespace
std::unique_ptr<Backend> make_vulkan() {
    return std::make_unique<Vulkan>();
}
namespace detail {
std::unique_ptr<Backend> make_vulkan_window(VulkanWindow window) { return std::make_unique<Vulkan>(&window); }
}
} // namespace ember
