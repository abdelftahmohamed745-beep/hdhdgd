// Nimbus RHI: render hardware interface shared by the Vulkan, GLES, and Headless backends.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "nimbus/time.hpp"

namespace nimbus::rhi {

enum class Api : uint8_t { None, Vulkan, OpenGlEs, Headless };

enum class Format : uint8_t {
  R8Unorm,
  R8G8B8A8Unorm,
  R8G8B8A8Srgb,
  B8G8R8A8Unorm,
  R16Float,
  R32Float,
  R32G32Float,
  R32G32B32A32Float,
  D24UnormS8Uint,
  D32Float,
  R8G8B8A8Uint,
};

enum class Primitive : uint8_t { Triangles, Lines, Points };

enum class CullMode : uint8_t { None, Front, Back };

enum class CompareOp : uint8_t { Never, Less, Equal, LessEqual, Greater, NotEqual, Always };

enum class BlendFactor : uint8_t { Zero, One, SrcAlpha, OneMinusSrcAlpha, DstAlpha };

struct DeviceInfo {
  Api api = Api::None;
  std::string deviceName;
  uint32_t maxTextureSize = 0;
  uint32_t uniformBufferAlignment = 256;
  bool computeShaders = false;
  bool anisotropicFiltering = false;
  uint32_t maxDrawBuffers = 1;
};

struct RenderPassDesc {
  Format colorFormat = Format::R8G8B8A8Unorm;
  Format depthFormat = Format::D24UnormS8Uint;
  bool clearColor = true;
  float clearColorValue[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  bool clearDepth = true;
  float clearDepthValue = 1.0f;
  CullMode cullMode = CullMode::Back;
  bool depthTest = true;
};

struct BufferDesc {
  uint32_t size = 0;
  bool vertex = false;
  bool index = false;
  bool uniform = false;
  bool storage = false;
  bool dynamic = false;
};

struct TextureDesc {
  uint32_t width = 1;
  uint32_t height = 1;
  uint32_t depth = 1;
  uint32_t mipLevels = 1;
  Format format = Format::R8G8B8A8Unorm;
  bool renderTarget = false;
  bool depthTarget = false;
};

struct PipelineDesc {
  std::string vertexShader;      // compiled bytecode or GLSL source for the selected API
  std::string fragmentShader;
  Format colorFormat = Format::R8G8B8A8Unorm;
  Format depthFormat = Format::D24UnormS8Uint;
  Primitive primitive = Primitive::Triangles;
  CullMode cullMode = CullMode::Back;
  bool depthTest = true;
  bool depthWrite = true;
  CompareOp depthCompare = CompareOp::Less;
  bool blendEnable = false;
  BlendFactor srcFactor = BlendFactor::SrcAlpha;
  BlendFactor dstFactor = BlendFactor::OneMinusSrcAlpha;
};

class Device {
 public:
  virtual ~Device() = default;
  virtual const DeviceInfo& info() const = 0;
  virtual void* nativeHandle() = 0;

  virtual uint32_t createBuffer(const BufferDesc& desc, const void* initialData = nullptr) = 0;
  virtual void destroyBuffer(uint32_t handle) = 0;
  virtual void* mapBuffer(uint32_t handle) = 0;
  virtual void unmapBuffer(uint32_t handle) = 0;

  virtual uint32_t createTexture(const TextureDesc& desc, const void* initialData = nullptr) = 0;
  virtual void destroyTexture(uint32_t handle) = 0;
  virtual void updateTexture(uint32_t handle, uint32_t mipLevel, const void* data, uint32_t size) = 0;

  virtual uint32_t createPipeline(const PipelineDesc& desc) = 0;
  virtual void destroyPipeline(uint32_t handle) = 0;

  virtual uint32_t createShaderUniform(uint32_t size) = 0;
  virtual void destroyShaderUniform(uint32_t handle) = 0;

  virtual uint32_t createRenderTarget(uint32_t width, uint32_t height,
                                      Format colorFormat = Format::R8G8B8A8Unorm,
                                      bool hasDepth = true) {
    (void)width;
    (void)height;
    (void)colorFormat;
    (void)hasDepth;
    return 0;
  }
  virtual void destroyRenderTarget(uint32_t handle) { (void)handle; }
  virtual void beginRenderTarget(uint32_t targetHandle, const RenderPassDesc& desc) {
    (void)targetHandle;
    beginPass(desc);
  }
  virtual uint32_t renderTargetColorTexture(uint32_t targetHandle) const {
    (void)targetHandle;
    return 0;
  }

  virtual void beginFrame() = 0;
  virtual void beginPass(const RenderPassDesc& desc) = 0;
  virtual void bindPipeline(uint32_t handle) = 0;
  virtual void bindVertexBuffer(uint32_t handle, uint32_t binding) = 0;
  virtual void bindIndexBuffer(uint32_t handle) = 0;
  virtual void bindUniform(uint32_t handle, uint32_t slot, const void* data, uint32_t size) = 0;
  virtual void bindTexture(uint32_t handle, uint32_t slot) = 0;
  virtual void draw(uint32_t vertexCount, uint32_t firstVertex) = 0;
  virtual void drawIndexed(uint32_t indexCount, uint32_t firstIndex, uint32_t vertexOffset) = 0;
  virtual void endPass() = 0;
  virtual void endFrame() = 0;
  virtual void present() = 0;
  virtual void setViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;

  virtual void resize(uint32_t width, uint32_t height) { setViewport(0, 0, width, height); }
  virtual bool isContextLost() const { return false; }
  virtual bool recoverContext() { return true; }
  virtual const time::FrameStats& frameStats() const {
    static const time::FrameStats kEmpty{};
    return kEmpty;
  }
};

// Backend factories. Return nullptr when the API is unavailable.
Device* createVulkanDevice(void* nativeWindow, uint32_t width, uint32_t height);
Device* createGlesDevice(void* nativeWindow, uint32_t width, uint32_t height);
Device* createHeadlessDevice(uint32_t width, uint32_t height);

// Picks the best API for the device: Vulkan when the driver reports a usable
// device, otherwise OpenGL ES. Passing Api::Headless creates the software RHI device.
Device* createDevice(Api preferred, void* nativeWindow, uint32_t width, uint32_t height);

}  // namespace nimbus::rhi
