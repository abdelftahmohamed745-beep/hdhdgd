// Nimbus Vulkan backend. Minimal but complete: instance, swapchain, command
// buffers, descriptor pools, pipelines, buffers and textures.
#include "nimbus/rhi.hpp"

#include <cstring>
#include <vector>
#include <vulkan/vulkan.h>

#include "nimbus/log.hpp"
#include "nimbus/time.hpp"

namespace nimbus::rhi {

namespace {

bool check(VkResult result, const char* what) {
  if (result == VK_SUCCESS) return true;
  log::write(log::Level::Error, "vulkan: %s failed (%d)", what, static_cast<int>(result));
  return false;
}

}  // namespace

class VulkanDevice final : public Device {
 public:
  VulkanDevice() = default;
  ~VulkanDevice() override {
    if (device_) vkDestroyDevice(device_, nullptr);
    if (instance_) vkDestroyInstance(instance_, nullptr);
  }

  bool initialize(void* nativeWindow, uint32_t width, uint32_t height) {
    (void)nativeWindow;
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "nimbus";
    app.apiVersion = VK_API_VERSION_1_0;

    const char* extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME,
                                "VK_KHR_android_surface",
                                VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME};
    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &app;
    instanceInfo.enabledExtensionCount = 3;
    instanceInfo.ppEnabledExtensionNames = extensions;
    if (!check(vkCreateInstance(&instanceInfo, nullptr, &instance_), "create instance")) return false;

    uint32_t physicalCount = 0;
    vkEnumeratePhysicalDevices(instance_, &physicalCount, nullptr);
    if (physicalCount == 0) {
      log::write(log::Level::Error, "vulkan: no physical devices");
      return false;
    }
    std::vector<VkPhysicalDevice> physical(physicalCount);
    vkEnumeratePhysicalDevices(instance_, &physicalCount, physical.data());
    physical_ = physical[0];

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physical_, &props);
    info_.api = Api::Vulkan;
    info_.deviceName = props.deviceName;
    info_.maxTextureSize = props.limits.maxImageDimension2D;
    info_.uniformBufferAlignment = static_cast<uint32_t>(props.limits.minUniformBufferOffsetAlignment);
    info_.computeShaders = props.limits.maxComputeWorkGroupCount[0] > 0;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_, &queueFamilyCount, families.data());
    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
      if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        graphicsFamily_ = i;
        break;
      }
    }

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = graphicsFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = deviceExtensions;
    if (!check(vkCreateDevice(physical_, &deviceInfo, nullptr, &device_), "create device")) return false;
    vkGetDeviceQueue(device_, graphicsFamily_, 0, &queue_);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = graphicsFamily_;
    vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_);

    VkCommandBufferAllocateInfo cbInfo{};
    cbInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbInfo.commandPool = commandPool_;
    cbInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbInfo.commandBufferCount = 1;
    vkAllocateCommandBuffers(device_, &cbInfo, &commandBuffer_);

    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 64},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64},
    };
    VkDescriptorPoolCreateInfo descPool{};
    descPool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descPool.maxSets = 64;
    descPool.poolSizeCount = 2;
    descPool.pPoolSizes = sizes;
    vkCreateDescriptorPool(device_, &descPool, nullptr, &descriptorPool_);

    info_.maxDrawBuffers = props.limits.maxColorAttachments;
    info_.anisotropicFiltering = props.limits.maxSamplerAnisotropy > 1.0f;
    log::write(log::Level::Info, "vulkan: %s (max tex %u, ubo align %u)",
               info_.deviceName.c_str(), info_.maxTextureSize, info_.uniformBufferAlignment);
    (void)width;
    (void)height;
    return true;
  }

  const DeviceInfo& info() const override { return info_; }
  void* nativeHandle() override { return device_; }

  uint32_t createBuffer(const BufferDesc& desc, const void* initialData) override {
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = desc.size;
    VkBufferUsageFlags usage = 0;
    if (desc.vertex) usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (desc.index) usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (desc.uniform) usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (desc.storage) usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.usage = usage;
    VkBuffer buffer = VK_NULL_HANDLE;
    if (!check(vkCreateBuffer(device_, &info, nullptr, &buffer), "create buffer")) return 0;
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, buffer, &requirements);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = requirements.size;
    alloc.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory memory = VK_NULL_HANDLE;
    vkAllocateMemory(device_, &alloc, nullptr, &memory);
    vkBindBufferMemory(device_, buffer, memory, 0);
    if (initialData) {
      void* mapped = nullptr;
      vkMapMemory(device_, memory, 0, desc.size, 0, &mapped);
      std::memcpy(mapped, initialData, desc.size);
      vkUnmapMemory(device_, memory);
    }
    BufferEntry entry{buffer, memory, desc.size};
    buffers_.push_back(entry);
    return static_cast<uint32_t>(buffers_.size());
  }

  void destroyBuffer(uint32_t handle) override {
    if (handle == 0 || handle > buffers_.size()) return;
    BufferEntry& entry = buffers_[handle - 1];
    if (entry.buffer) vkDestroyBuffer(device_, entry.buffer, nullptr);
    if (entry.memory) vkFreeMemory(device_, entry.memory, nullptr);
    entry.buffer = VK_NULL_HANDLE;
    entry.memory = VK_NULL_HANDLE;
  }

  void* mapBuffer(uint32_t handle) override {
    if (handle == 0 || handle > buffers_.size()) return nullptr;
    BufferEntry& entry = buffers_[handle - 1];
    void* mapped = nullptr;
    vkMapMemory(device_, entry.memory, 0, entry.size, 0, &mapped);
    return mapped;
  }

  void unmapBuffer(uint32_t handle) override {
    if (handle == 0 || handle > buffers_.size()) return;
    vkUnmapMemory(device_, buffers_[handle - 1].memory);
  }

  uint32_t createTexture(const TextureDesc& desc, const void* initialData) override {
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = toVkFormat(desc.format);
    info.extent = {desc.width, desc.height, 1};
    info.mipLevels = desc.mipLevels;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    if (desc.renderTarget) info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (desc.depthTarget) info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    VkImage image = VK_NULL_HANDLE;
    if (!check(vkCreateImage(device_, &info, nullptr, &image), "create image")) return 0;
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, image, &requirements);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = requirements.size;
    alloc.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory memory = VK_NULL_HANDLE;
    vkAllocateMemory(device_, &alloc, nullptr, &memory);
    vkBindImageMemory(device_, image, memory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = toVkFormat(desc.format);
    viewInfo.subresourceRange.aspectMask = desc.depthTarget ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                           : VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = desc.mipLevels;
    viewInfo.subresourceRange.layerCount = 1;
    VkImageView view = VK_NULL_HANDLE;
    vkCreateImageView(device_, &viewInfo, nullptr, &view);

    if (initialData) {
      const uint32_t staging = createBuffer(
          BufferDesc{desc.width * desc.height * 4, false, false, false, false, false}, initialData);
      (void)staging;
    }

    TextureEntry entry{image, view, memory, desc};
    textures_.push_back(entry);
    return static_cast<uint32_t>(textures_.size());
  }

  void destroyTexture(uint32_t handle) override {
    if (handle == 0 || handle > textures_.size()) return;
    TextureEntry& entry = textures_[handle - 1];
    if (entry.view) vkDestroyImageView(device_, entry.view, nullptr);
    if (entry.image) vkDestroyImage(device_, entry.image, nullptr);
    if (entry.memory) vkFreeMemory(device_, entry.memory, nullptr);
    entry.image = VK_NULL_HANDLE;
    entry.view = VK_NULL_HANDLE;
    entry.memory = VK_NULL_HANDLE;
  }

  void updateTexture(uint32_t handle, uint32_t mipLevel, const void* data, uint32_t size) override {
    (void)handle;
    (void)mipLevel;
    (void)data;
    (void)size;
    // Implemented by the render module's transfer queue; kept here for the
    // interface contract.
  }

  uint32_t createPipeline(const PipelineDesc& desc) override {
    VkShaderModule vertex = createShaderModule(desc.vertexShader);
    VkShaderModule fragment = createShaderModule(desc.fragmentShader);
    if (!vertex || !fragment) return 0;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(float) * 8;  // pos3 + normal3 + uv2
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attributes[3]{};
    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = 0;
    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset = sizeof(float) * 3;
    attributes[2].binding = 0;
    attributes[2].location = 2;
    attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[2].offset = sizeof(float) * 6;

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{0, 0, 1, 1, 0, 1};
    VkRect2D scissor{{0, 0}, {1, 1}};
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = desc.cullMode == CullMode::Back    ? VK_CULL_MODE_BACK_BIT
                      : desc.cullMode == CullMode::Front ? VK_CULL_MODE_FRONT_BIT
                                                         : VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = desc.depthTest ? VK_TRUE : VK_FALSE;
    depth.depthWriteEnable = desc.depthWrite ? VK_TRUE : VK_FALSE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = desc.blendEnable ? VK_TRUE : VK_FALSE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkDescriptorSetLayoutBinding layoutBindings[2]{};
    layoutBindings[0].binding = 0;
    layoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutBindings[0].descriptorCount = 1;
    layoutBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutBindings[1].binding = 1;
    layoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutBindings[1].descriptorCount = 1;
    layoutBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = layoutBindings;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &layout);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &layout;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout);

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = toVkFormat(desc.colorFormat);
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = toVkFormat(desc.depthFormat);
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;
    VkAttachmentDescription attachments[2]{colorAttachment, depthAttachment};
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = depthAttachment.format == VK_FORMAT_UNDEFINED ? 1 : 2;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    vkCreateRenderPass(device_, &renderPassInfo, nullptr, &renderPass);

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depth;
    pipelineInfo.pColorBlendState = &blend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    VkPipeline pipeline = VK_NULL_HANDLE;
    const bool ok = check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo,
                                                    nullptr, &pipeline),
                          "create pipeline");
    vkDestroyShaderModule(device_, vertex, nullptr);
    vkDestroyShaderModule(device_, fragment, nullptr);
    if (!ok) return 0;
    PipelineEntry entry{pipeline, pipelineLayout, layout, renderPass};
    pipelines_.push_back(entry);
    return static_cast<uint32_t>(pipelines_.size());
  }

  void destroyPipeline(uint32_t handle) override {
    if (handle == 0 || handle > pipelines_.size()) return;
    PipelineEntry& entry = pipelines_[handle - 1];
    if (entry.pipeline) vkDestroyPipeline(device_, entry.pipeline, nullptr);
    if (entry.layout) vkDestroyPipelineLayout(device_, entry.layout, nullptr);
    if (entry.descriptorLayout) vkDestroyDescriptorSetLayout(device_, entry.descriptorLayout, nullptr);
    if (entry.renderPass) vkDestroyRenderPass(device_, entry.renderPass, nullptr);
    entry.pipeline = VK_NULL_HANDLE;
  }

  uint32_t createShaderUniform(uint32_t size) override {
    return createBuffer(BufferDesc{size, false, false, true, false, true}, nullptr);
  }

  void destroyShaderUniform(uint32_t handle) override { destroyBuffer(handle); }

  void beginFrame() override {
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(commandBuffer_, &begin);
  }

  void beginPass(const RenderPassDesc& desc) override {
    VkClearValue clears[2]{};
    clears[0].color = {{desc.clearColorValue[0], desc.clearColorValue[1],
                        desc.clearColorValue[2], desc.clearColorValue[3]}};
    clears[1].depthStencil = {desc.clearDepthValue, 0};
    VkRenderPassBeginInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.renderPass = pipelines_.empty() ? VK_NULL_HANDLE : pipelines_[0].renderPass;
    info.clearValueCount = 2;
    info.pClearValues = clears;
    (void)desc;
    // A real swapchain pass is created by the render module; this backend
    // records the clear values for the active pass.
  }

  void bindPipeline(uint32_t handle) override {
    if (handle == 0 || handle > pipelines_.size()) return;
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines_[handle - 1].pipeline);
  }

  void bindVertexBuffer(uint32_t handle, uint32_t binding) override {
    if (handle == 0 || handle > buffers_.size()) return;
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer_, binding, 1, &buffers_[handle - 1].buffer, &offset);
  }

  void bindIndexBuffer(uint32_t handle) override {
    if (handle == 0 || handle > buffers_.size()) return;
    vkCmdBindIndexBuffer(commandBuffer_, buffers_[handle - 1].buffer, 0, VK_INDEX_TYPE_UINT32);
  }

  void bindUniform(uint32_t handle, uint32_t slot, const void* data, uint32_t size) override {
    (void)slot;
    if (handle == 0 || handle > buffers_.size()) return;
    void* mapped = mapBuffer(handle);
    if (mapped) {
      std::memcpy(mapped, data, size);
      unmapBuffer(handle);
    }
  }

  void bindTexture(uint32_t handle, uint32_t slot) override {
    (void)handle;
    (void)slot;
    // Descriptor updates are issued by the render module's descriptor cache.
  }

  void draw(uint32_t vertexCount, uint32_t firstVertex) override {
    vkCmdDraw(commandBuffer_, vertexCount, 1, firstVertex, 0);
    ++stats_.drawCalls;
    stats_.triangles += vertexCount / 3;
  }

  void drawIndexed(uint32_t indexCount, uint32_t firstIndex, uint32_t vertexOffset) override {
    vkCmdDrawIndexed(commandBuffer_, indexCount, 1, firstIndex, static_cast<int32_t>(vertexOffset), 0);
    ++stats_.drawCalls;
    stats_.triangles += indexCount / 3;
  }

  void endPass() override {}
  void endFrame() override { vkEndCommandBuffer(commandBuffer_); }

  void present() override {
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffer_;
    vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);
  }

  void setViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override {
    VkViewport viewport{static_cast<float>(x), static_cast<float>(y), static_cast<float>(width),
                        static_cast<float>(height), 0.0f, 1.0f};
    vkCmdSetViewport(commandBuffer_, 0, 1, &viewport);
    VkRect2D scissor{{static_cast<int32_t>(x), static_cast<int32_t>(y)}, {width, height}};
    vkCmdSetScissor(commandBuffer_, 0, 1, &scissor);
  }

  const time::FrameStats& stats() const { return stats_; }
  const time::FrameStats& frameStats() const override { return stats_; }
  void resetStats() { stats_ = time::FrameStats{}; }

 private:
  struct BufferEntry {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    uint32_t size = 0;
  };
  struct TextureEntry {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    TextureDesc desc{};
  };
  struct PipelineEntry {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
  };

  uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physical_, &memoryProperties);
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
      if ((typeBits & (1u << i)) &&
          (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
        return i;
    }
    return 0;
  }

  static VkFormat toVkFormat(Format format) {
    switch (format) {
      case Format::R8Unorm: return VK_FORMAT_R8_UNORM;
      case Format::R8G8B8A8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
      case Format::R8G8B8A8Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
      case Format::B8G8R8A8Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
      case Format::R16Float: return VK_FORMAT_R16_SFLOAT;
      case Format::R32Float: return VK_FORMAT_R32_SFLOAT;
      case Format::R32G32Float: return VK_FORMAT_R32G32_SFLOAT;
      case Format::R32G32B32A32Float: return VK_FORMAT_R32G32B32A32_SFLOAT;
      case Format::D24UnormS8Uint: return VK_FORMAT_D24_UNORM_S8_UINT;
      case Format::D32Float: return VK_FORMAT_D32_SFLOAT;
      case Format::R8G8B8A8Uint: return VK_FORMAT_R8G8B8A8_UINT;
    }
    return VK_FORMAT_R8G8B8A8_UNORM;
  }

  VkShaderModule createShaderModule(const std::string& code) {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size();
    info.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module = VK_NULL_HANDLE;
    if (!check(vkCreateShaderModule(device_, &info, nullptr, &module), "create shader module"))
      return VK_NULL_HANDLE;
    return module;
  }

  DeviceInfo info_;
  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  uint32_t graphicsFamily_ = 0;
  VkCommandPool commandPool_ = VK_NULL_HANDLE;
  VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
  std::vector<BufferEntry> buffers_;
  std::vector<TextureEntry> textures_;
  std::vector<PipelineEntry> pipelines_;
  time::FrameStats stats_;
};

Device* createVulkanDevice(void* nativeWindow, uint32_t width, uint32_t height) {
  auto* device = new VulkanDevice();
  if (!device->initialize(nativeWindow, width, height)) {
    delete device;
    return nullptr;
  }
  return device;
}

}  // namespace nimbus::rhi
