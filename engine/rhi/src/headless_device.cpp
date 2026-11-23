// Nimbus Headless RHI backend: CPU-backed validation & software rasterization
// device for headless rendering, automated GPU pipeline verification, and render targets.
#include "nimbus/rhi.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

#include "nimbus/log.hpp"
#include "nimbus/math.hpp"

namespace nimbus::rhi {
namespace {

uint32_t bytesPerPixel(Format format) {
  switch (format) {
    case Format::R8Unorm: return 1;
    case Format::R16Float: return 2;
    case Format::R32Float:
    case Format::R8G8B8A8Unorm:
    case Format::R8G8B8A8Srgb:
    case Format::B8G8R8A8Unorm:
    case Format::R8G8B8A8Uint:
    case Format::D24UnormS8Uint:
    case Format::D32Float: return 4;
    case Format::R32G32Float: return 8;
    case Format::R32G32B32A32Float: return 16;
  }
  return 4;
}

uint8_t floatToU8(float v) {
  return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
}

}  // namespace

class HeadlessDevice final : public Device {
 public:
  HeadlessDevice(uint32_t width, uint32_t height)
      : width_(width == 0 ? 320 : width), height_(height == 0 ? 240 : height) {
    info_.api = Api::Headless;
    info_.deviceName = "Nimbus Software Headless RHI";
    info_.maxTextureSize = 4096;
    info_.uniformBufferAlignment = 16;
    info_.computeShaders = false;
    info_.anisotropicFiltering = false;
    info_.maxDrawBuffers = 1;
    colorBuffer_.resize(static_cast<size_t>(width_) * height_ * 4u, 0);
    depthBuffer_.resize(static_cast<size_t>(width_) * height_, 1.0f);
    viewportWidth_ = width_;
    viewportHeight_ = height_;
  }

  const DeviceInfo& info() const override { return info_; }
  void* nativeHandle() override { return this; }

  uint32_t createBuffer(const BufferDesc& desc, const void* initialData) override {
    if (desc.size == 0) return 0;
    BufferEntry entry;
    entry.desc = desc;
    entry.data.resize(desc.size, 0);
    if (initialData) std::memcpy(entry.data.data(), initialData, desc.size);
    buffers_.push_back(std::move(entry));
    return static_cast<uint32_t>(buffers_.size());
  }

  void destroyBuffer(uint32_t handle) override {
    if (handle == 0 || handle > buffers_.size()) return;
    buffers_[handle - 1].alive = false;
    buffers_[handle - 1].data.clear();
  }

  void* mapBuffer(uint32_t handle) override {
    if (handle == 0 || handle > buffers_.size() || !buffers_[handle - 1].alive) return nullptr;
    return buffers_[handle - 1].data.data();
  }

  void unmapBuffer(uint32_t handle) override { (void)handle; }

  uint32_t createTexture(const TextureDesc& desc, const void* initialData) override {
    if (desc.width == 0 || desc.height == 0) return 0;
    TextureEntry entry;
    entry.desc = desc;
    const size_t size = static_cast<size_t>(desc.width) * desc.height * bytesPerPixel(desc.format);
    entry.pixels.resize(size, 0);
    if (initialData && !entry.pixels.empty()) {
      std::memcpy(entry.pixels.data(), initialData, size);
    }
    textures_.push_back(std::move(entry));
    return static_cast<uint32_t>(textures_.size());
  }

  void destroyTexture(uint32_t handle) override {
    if (handle == 0 || handle > textures_.size()) return;
    textures_[handle - 1].alive = false;
    textures_[handle - 1].pixels.clear();
  }

  void updateTexture(uint32_t handle, uint32_t mipLevel, const void* data, uint32_t size) override {
    (void)mipLevel;
    if (handle == 0 || handle > textures_.size() || !textures_[handle - 1].alive || !data) return;
    TextureEntry& entry = textures_[handle - 1];
    const size_t copySize = std::min<size_t>(size, entry.pixels.size());
    std::memcpy(entry.pixels.data(), data, copySize);
  }

  uint32_t createPipeline(const PipelineDesc& desc) override {
    if (desc.vertexShader.empty() || desc.fragmentShader.empty()) return 0;
    pipelines_.push_back({desc, true});
    return static_cast<uint32_t>(pipelines_.size());
  }

  void destroyPipeline(uint32_t handle) override {
    if (handle == 0 || handle > pipelines_.size()) return;
    pipelines_[handle - 1].alive = false;
  }

  uint32_t createShaderUniform(uint32_t size) override {
    return createBuffer(BufferDesc{size, false, false, true, false, true}, nullptr);
  }

  void destroyShaderUniform(uint32_t handle) override { destroyBuffer(handle); }

  uint32_t createRenderTarget(uint32_t width, uint32_t height, Format colorFormat,
                              bool hasDepth) override {
    if (width == 0 || height == 0) return 0;
    const uint32_t colorTex = createTexture(
        TextureDesc{width, height, 1, 1, colorFormat, true, false}, nullptr);
    if (colorTex == 0) return 0;
    uint32_t depthTex = 0;
    if (hasDepth) {
      depthTex = createTexture(
          TextureDesc{width, height, 1, 1, Format::D24UnormS8Uint, false, true}, nullptr);
    }
    RenderTargetEntry entry;
    entry.width = width;
    entry.height = height;
    entry.colorTexture = colorTex;
    entry.depthTexture = depthTex;
    entry.depthBuffer.assign(static_cast<size_t>(width) * height, 1.0f);
    renderTargets_.push_back(std::move(entry));
    return static_cast<uint32_t>(renderTargets_.size());
  }

  void destroyRenderTarget(uint32_t handle) override {
    if (handle == 0 || handle > renderTargets_.size()) return;
    RenderTargetEntry& rt = renderTargets_[handle - 1];
    if (!rt.alive) return;
    destroyTexture(rt.colorTexture);
    if (rt.depthTexture) destroyTexture(rt.depthTexture);
    rt.depthBuffer.clear();
    rt.alive = false;
  }

  void beginRenderTarget(uint32_t targetHandle, const RenderPassDesc& desc) override {
    activeRenderTarget_ = targetHandle;
    if (targetHandle != 0 && targetHandle <= renderTargets_.size() &&
        renderTargets_[targetHandle - 1].alive) {
      const RenderTargetEntry& rt = renderTargets_[targetHandle - 1];
      setViewport(0, 0, rt.width, rt.height);
    }
    beginPass(desc);
  }

  uint32_t renderTargetColorTexture(uint32_t targetHandle) const override {
    if (targetHandle == 0 || targetHandle > renderTargets_.size() ||
        !renderTargets_[targetHandle - 1].alive)
      return 0;
    return renderTargets_[targetHandle - 1].colorTexture;
  }

  void beginFrame() override {
    stats_ = time::FrameStats{};
    boundPipeline_ = 0;
    boundVertexBuffer_ = 0;
    boundIndexBuffer_ = 0;
    boundTextures_.fill(0);
    activeRenderTarget_ = 0;
  }

  void beginPass(const RenderPassDesc& desc) override {
    if (desc.clearColor) clearActiveColor(desc.clearColorValue);
    if (desc.clearDepth) clearActiveDepth(desc.clearDepthValue);
  }

  void bindPipeline(uint32_t handle) override {
    if (handle == 0 || handle > pipelines_.size() || !pipelines_[handle - 1].alive) return;
    boundPipeline_ = handle;
  }

  void bindVertexBuffer(uint32_t handle, uint32_t binding) override {
    (void)binding;
    if (handle == 0 || handle > buffers_.size() || !buffers_[handle - 1].alive) return;
    boundVertexBuffer_ = handle;
  }

  void bindIndexBuffer(uint32_t handle) override {
    if (handle == 0 || handle > buffers_.size() || !buffers_[handle - 1].alive) return;
    boundIndexBuffer_ = handle;
  }

  void bindUniform(uint32_t handle, uint32_t slot, const void* data, uint32_t size) override {
    if (handle == 0 || handle > buffers_.size() || !buffers_[handle - 1].alive || !data) return;
    BufferEntry& entry = buffers_[handle - 1];
    const size_t copySize = std::min<size_t>(size, entry.data.size());
    std::memcpy(entry.data.data(), data, copySize);
    if (slot < uniformSlots_.size()) {
      uniformSlots_[slot].assign(entry.data.begin(), entry.data.begin() + static_cast<long>(copySize));
    }
  }

  void bindTexture(uint32_t handle, uint32_t slot) override {
    if (slot >= boundTextures_.size()) return;
    if (handle == 0 || handle > textures_.size() || !textures_[handle - 1].alive) {
      boundTextures_[slot] = 0;
      return;
    }
    boundTextures_[slot] = handle;
  }

  void draw(uint32_t vertexCount, uint32_t firstVertex) override {
    if (boundPipeline_ == 0 || boundVertexBuffer_ == 0 || vertexCount == 0) return;
    ++stats_.drawCalls;
    const Primitive prim = pipelines_[boundPipeline_ - 1].desc.primitive;
    if (prim == Primitive::Triangles) {
      stats_.triangles += vertexCount / 3u;
      rasterizeRange(vertexCount, firstVertex, false, 0);
    } else if (prim == Primitive::Lines) {
      stats_.triangles += vertexCount / 2u;
    } else {
      stats_.triangles += vertexCount;
    }
  }

  void drawIndexed(uint32_t indexCount, uint32_t firstIndex, uint32_t vertexOffset) override {
    (void)vertexOffset;
    if (boundPipeline_ == 0 || boundVertexBuffer_ == 0 || boundIndexBuffer_ == 0 || indexCount == 0)
      return;
    ++stats_.drawCalls;
    stats_.triangles += indexCount / 3u;
    rasterizeRange(indexCount, firstIndex, true, vertexOffset);
  }

  void endPass() override { activeRenderTarget_ = 0; }
  void endFrame() override {}
  void present() override {}

  void setViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override {
    viewportX_ = x;
    viewportY_ = y;
    viewportWidth_ = std::max(1u, width);
    viewportHeight_ = std::max(1u, height);
  }

  void resize(uint32_t width, uint32_t height) override {
    width_ = std::max(1u, width);
    height_ = std::max(1u, height);
    colorBuffer_.assign(static_cast<size_t>(width_) * height_ * 4u, 0);
    depthBuffer_.assign(static_cast<size_t>(width_) * height_, 1.0f);
    setViewport(0, 0, width_, height_);
  }

  bool isContextLost() const override { return contextLost_; }
  bool recoverContext() override {
    contextLost_ = false;
    return true;
  }

  const time::FrameStats& frameStats() const override { return stats_; }

 private:
  struct BufferEntry {
    BufferDesc desc{};
    std::vector<uint8_t> data;
    bool alive = true;
  };
  struct TextureEntry {
    TextureDesc desc{};
    std::vector<uint8_t> pixels;
    bool alive = true;
  };
  struct PipelineEntry {
    PipelineDesc desc{};
    bool alive = true;
  };
  struct RenderTargetEntry {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t colorTexture = 0;
    uint32_t depthTexture = 0;
    std::vector<float> depthBuffer;
    bool alive = true;
  };

  void clearActiveColor(const float rgba[4]) {
    const uint8_t r = floatToU8(rgba[0]);
    const uint8_t g = floatToU8(rgba[1]);
    const uint8_t b = floatToU8(rgba[2]);
    const uint8_t a = floatToU8(rgba[3]);
    std::vector<uint8_t>* pixels = &colorBuffer_;
    if (activeRenderTarget_ != 0 && activeRenderTarget_ <= renderTargets_.size() &&
        renderTargets_[activeRenderTarget_ - 1].alive) {
      const uint32_t tex = renderTargets_[activeRenderTarget_ - 1].colorTexture;
      if (tex != 0 && tex <= textures_.size()) pixels = &textures_[tex - 1].pixels;
    }
    for (size_t i = 0; i + 3 < pixels->size(); i += 4) {
      (*pixels)[i + 0] = r;
      (*pixels)[i + 1] = g;
      (*pixels)[i + 2] = b;
      (*pixels)[i + 3] = a;
    }
  }

  void clearActiveDepth(float depth) {
    std::vector<float>* db = &depthBuffer_;
    if (activeRenderTarget_ != 0 && activeRenderTarget_ <= renderTargets_.size() &&
        renderTargets_[activeRenderTarget_ - 1].alive) {
      db = &renderTargets_[activeRenderTarget_ - 1].depthBuffer;
    }
    std::fill(db->begin(), db->end(), depth);
  }

  void rasterizeRange(uint32_t count, uint32_t first, bool indexed, uint32_t vertexOffset) {
    // Performs lightweight centroid/bounding-box software rasterization check when MVP is available
    // in slot 0 so headless verification exercises real vertex & uniform buffers without blowing CPU budgets.
    const BufferEntry& vb = buffers_[boundVertexBuffer_ - 1];
    if (vb.data.size() < sizeof(float) * 8u) return;
    const float* verts = reinterpret_cast<const float*>(vb.data.data());
    const size_t totalVertices = vb.data.size() / (sizeof(float) * 8u);

    const uint32_t* indices = nullptr;
    size_t totalIndices = 0;
    if (indexed) {
      const BufferEntry& ib = buffers_[boundIndexBuffer_ - 1];
      indices = reinterpret_cast<const uint32_t*>(ib.data.data());
      totalIndices = ib.data.size() / sizeof(uint32_t);
    }

    math::Mat4 vp = math::mat4Identity();
    if (uniformSlots_[0].size() >= sizeof(math::Mat4) * 3u) {
      std::memcpy(&vp, uniformSlots_[0].data() + sizeof(math::Mat4) * 2u, sizeof(math::Mat4));
    }
    math::Mat4 model = math::mat4Identity();
    if (uniformSlots_[2].size() >= sizeof(math::Mat4)) {
      std::memcpy(&model, uniformSlots_[2].data(), sizeof(math::Mat4));
    }
    const math::Mat4 mvp = math::mat4Multiply(vp, model);

    uint8_t baseR = 220, baseG = 220, baseB = 220, baseA = 255;
    if (uniformSlots_[2].size() >= sizeof(math::Mat4) * 2u + sizeof(float) * 4u) {
      const float* baseColor =
          reinterpret_cast<const float*>(uniformSlots_[2].data() + sizeof(math::Mat4) * 2u);
      baseR = floatToU8(baseColor[0]);
      baseG = floatToU8(baseColor[1]);
      baseB = floatToU8(baseColor[2]);
      baseA = floatToU8(baseColor[3]);
    }

    std::vector<uint8_t>* targetPixels = &colorBuffer_;
    uint32_t targetW = width_;
    uint32_t targetH = height_;
    if (activeRenderTarget_ != 0 && activeRenderTarget_ <= renderTargets_.size() &&
        renderTargets_[activeRenderTarget_ - 1].alive) {
      const RenderTargetEntry& rt = renderTargets_[activeRenderTarget_ - 1];
      targetPixels = &textures_[rt.colorTexture - 1].pixels;
      targetW = rt.width;
      targetH = rt.height;
    }

    const uint32_t triCount = std::min<uint32_t>(count / 3u, 64u);
    for (uint32_t t = 0; t < triCount; ++t) {
      uint32_t idx[3]{0, 0, 0};
      for (int k = 0; k < 3; ++k) {
        const uint32_t elem = first + t * 3u + static_cast<uint32_t>(k);
        if (indexed) {
          if (elem >= totalIndices) return;
          idx[k] = indices[elem] + vertexOffset;
        } else {
          idx[k] = elem;
        }
        if (idx[k] >= totalVertices) return;
      }
      math::Vec4 clipCenter{0, 0, 0, 0};
      for (int k = 0; k < 3; ++k) {
        const float* v = verts + static_cast<size_t>(idx[k]) * 8u;
        const math::Vec4 c = math::mat4Transform(mvp, {v[0], v[1], v[2], 1.0f});
        clipCenter = clipCenter + c * (1.0f / 3.0f);
      }
      if (clipCenter.w <= 1e-4f) continue;
      const float ndcX = clipCenter.x / clipCenter.w;
      const float ndcY = clipCenter.y / clipCenter.w;
      const float ndcZ = clipCenter.z / clipCenter.w;
      if (ndcX < -1.0f || ndcX > 1.0f || ndcY < -1.0f || ndcY > 1.0f || ndcZ < -1.0f || ndcZ > 1.0f)
        continue;
      const uint32_t px = std::min(targetW - 1, static_cast<uint32_t>((ndcX * 0.5f + 0.5f) * targetW));
      const uint32_t py = std::min(targetH - 1, static_cast<uint32_t>((0.5f - ndcY * 0.5f) * targetH));
      const size_t offset = (static_cast<size_t>(py) * targetW + px) * 4u;
      if (offset + 3 < targetPixels->size()) {
        (*targetPixels)[offset + 0] = baseR;
        (*targetPixels)[offset + 1] = baseG;
        (*targetPixels)[offset + 2] = baseB;
        (*targetPixels)[offset + 3] = baseA;
      }
    }
  }

  DeviceInfo info_{};
  uint32_t width_ = 320;
  uint32_t height_ = 240;
  uint32_t viewportX_ = 0;
  uint32_t viewportY_ = 0;
  uint32_t viewportWidth_ = 320;
  uint32_t viewportHeight_ = 240;
  std::vector<uint8_t> colorBuffer_;
  std::vector<float> depthBuffer_;
  std::vector<BufferEntry> buffers_;
  std::vector<TextureEntry> textures_;
  std::vector<PipelineEntry> pipelines_;
  std::vector<RenderTargetEntry> renderTargets_;
  std::array<std::vector<uint8_t>, 8> uniformSlots_{};
  std::array<uint32_t, 8> boundTextures_{};
  uint32_t boundPipeline_ = 0;
  uint32_t boundVertexBuffer_ = 0;
  uint32_t boundIndexBuffer_ = 0;
  uint32_t activeRenderTarget_ = 0;
  bool contextLost_ = false;
  time::FrameStats stats_{};
};

Device* createHeadlessDevice(uint32_t width, uint32_t height) {
  return new HeadlessDevice(width, height);
}

}  // namespace nimbus::rhi
