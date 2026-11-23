// Nimbus render module: render graph, camera, PBR/lighting, culling, materials,
// shadow/UI/particle/debug passes, and GLSL ES 3.00 shader library.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "nimbus/ecs.hpp"
#include "nimbus/math.hpp"
#include "nimbus/rhi.hpp"
#include "nimbus/scene.hpp"
#include "nimbus/time.hpp"

namespace nimbus::render {

using math::Vec3;
using math::Vec4;

inline constexpr uint32_t kMaxLights = 16;

struct Camera {
  Vec3 position{0, 0, 0};
  math::Quat rotation{0, 0, 0, 1};
  float fov = 60.0f;
  float nearPlane = 0.1f;
  float farPlane = 500.0f;
  float aspect = 16.0f / 9.0f;
};

struct Light {
  enum class Type : uint8_t { Directional, Point, Spot };
  Type type = Type::Directional;
  Vec3 position{0, 0, 0};
  Vec3 direction{0, -1, 0};
  Vec3 color{1, 1, 1};
  float intensity = 1.0f;
  float range = 10.0f;
  float innerCone = 30.0f;
  float outerCone = 45.0f;
};

struct Mesh {
  std::vector<float> vertices;  // pos3, normal3, uv2
  std::vector<uint32_t> indices;
  uint32_t vertexBuffer = 0;
  uint32_t indexBuffer = 0;
  uint32_t indexCount = 0;
  bool gpuReady = false;
  math::Aabb localBounds{};
  bool hasBounds = false;

  void computeBounds();
};

struct Material {
  Vec4 baseColor{1, 1, 1, 1};
  float metallic = 0.0f;
  float roughness = 0.5f;
  float emissive = 0.0f;
  uint32_t texture = 0;
  uint32_t normalTexture = 0;
  uint32_t roughnessTexture = 0;
  uint32_t emissiveTexture = 0;
  uint32_t pipeline = 0;
  bool wireframe = false;
  bool unlit = false;
  rhi::CullMode cullMode = rhi::CullMode::Back;
  std::string vertexShader;
  std::string fragmentShader;
};

struct RenderObject {
  ecs::Entity entity = ecs::kNullEntity;
  Mesh* mesh = nullptr;
  Material* material = nullptr;
  bool castShadow = true;
};

// ECS attachable components for declarative scene rendering.
struct MeshComponent {
  Mesh* mesh = nullptr;
  Material* material = nullptr;
  bool castShadow = true;
  bool visible = true;
};

struct UiQuad {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
  float u0 = 0.0f;
  float v0 = 0.0f;
  float u1 = 1.0f;
  float v1 = 1.0f;
  Vec4 color{1, 1, 1, 1};
  uint32_t texture = 0;
};

struct DebugLine {
  Vec3 start{0, 0, 0};
  Vec3 end{0, 0, 0};
  Vec4 color{0, 1, 0, 1};
};

// Built-in GLSL ES 3.00 shaders used when materials omit custom bytecode/GLSL.
const char* defaultLitVertexShader();
const char* defaultLitFragmentShader();
const char* defaultUnlitVertexShader();
const char* defaultUnlitFragmentShader();
const char* defaultUiVertexShader();
const char* defaultUiFragmentShader();

// Procedural primitives.
Mesh createCube(float size = 1.0f);
Mesh createPlane(float size = 10.0f, uint32_t subdivisions = 1);
Mesh createSphere(float radius = 0.5f, uint32_t segments = 16);
Mesh createQuad(float width = 1.0f, float height = 1.0f);

// Per-frame uniform block (slot 0).
struct FrameUniforms {
  math::Mat4 view;
  math::Mat4 projection;
  math::Mat4 viewProjection;
  Vec4 cameraPosition;
  Vec4 cameraForward;
  Vec4 ambientColor;
  uint32_t lightCount = 0;
  uint32_t objectCount = 0;
  float time = 0.0f;
  uint32_t flags = 0;
};

// GPU Light block (slot 1).
struct GpuLight {
  Vec4 positionRange;     // xyz = position, w = range
  Vec4 directionType;     // xyz = direction, w = type (0=dir, 1=point, 2=spot)
  Vec4 colorIntensity;    // xyz = color, w = intensity
  Vec4 spotAngles;        // x = cos(inner), y = cos(outer), zw = unused
};

struct LightingBlock {
  std::array<GpuLight, kMaxLights> lights{};
  uint32_t count = 0;
  uint32_t shadowEnabled = 0;
  float shadowBias = 0.005f;
  float padding = 0.0f;
  math::Mat4 lightSpaceMatrix = math::mat4Identity();
};

// Per-object uniform block (slot 2).
struct ObjectUniforms {
  math::Mat4 model = math::mat4Identity();
  math::Mat4 normalMatrix = math::mat4Identity();
  Vec4 baseColor{1, 1, 1, 1};
  Vec4 pbrParams{0.0f, 0.5f, 0.0f, 0.0f};  // metallic, roughness, emissive, hasTexture
};

class RenderGraph {
 public:
  struct Pass {
    std::string name;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    std::function<void(rhi::Device&, uint32_t)> execute;
    uint32_t order = 0;
  };

  void addPass(Pass pass) { passes_.push_back(std::move(pass)); }
  void execute(rhi::Device& device) {
    for (Pass& pass : passes_) {
      if (pass.execute) pass.execute(device, pass.order);
    }
  }
  void clear() { passes_.clear(); }
  uint32_t passCount() const { return static_cast<uint32_t>(passes_.size()); }

 private:
  std::vector<Pass> passes_;
};

class Renderer {
 public:
  Renderer(rhi::Device* device, scene::Scene* scene)
      : device_(device), scene_(scene) {}
  ~Renderer();
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  void setCamera(const Camera& camera) { camera_ = camera; }
  const Camera& camera() const { return camera_; }

  math::Mat4 viewMatrix() const;
  math::Mat4 projectionMatrix() const;
  FrameUniforms buildUniforms() const;
  LightingBlock buildLightingBlock() const;

  void addLight(const Light& light) { lights_.push_back(light); }
  void clearLights() { lights_.clear(); }
  uint32_t lightCount() const { return static_cast<uint32_t>(lights_.size()); }

  void addObject(const RenderObject& object) { objects_.push_back(object); }
  void clearObjects() { objects_.clear(); }
  uint32_t objectCount() const { return static_cast<uint32_t>(objects_.size()); }

  // Overlay UI quads, particle instances (8 floats per particle: pos3+size, color3+lifeRatio),
  // and debug wireframes rendered cleanly after the 3D scene pass.
  void clearOverlays() {
    uiQuads_.clear();
    debugLines_.clear();
    particleInstances_.clear();
    particleCount_ = 0;
  }
  void submitUiQuad(const UiQuad& quad) { uiQuads_.push_back(quad); }
  void submitParticles(const float* instanceData, uint32_t count);
  void drawDebugLine(const Vec3& start, const Vec3& end, const Vec4& color = {0, 1, 0, 1}) {
    debugLines_.push_back({start, end, color});
  }
  void drawDebugBox(const math::Aabb& box, const Vec4& color = {0, 1, 0, 1});

  // Shadow pass toggle (allocates offscreen shadow render target on demand).
  void setShadowsEnabled(bool enabled, uint32_t resolution = 1024);
  bool shadowsEnabled() const { return shadowsEnabled_; }

  RenderGraph& graph() { return graph_; }
  const RenderGraph& graph() const { return graph_; }

  void setScreenSize(uint32_t width, uint32_t height) {
    screenWidth_ = std::max(1u, width);
    screenHeight_ = std::max(1u, height);
  }

  // Frustum culls objects against the camera (using AABB when available), executes
  // shadow/main/particle/debug/UI passes and custom RenderGraph passes.
  void render(const FrameUniforms& shared);

  bool uploadMesh(Mesh& mesh);
  bool uploadMaterial(Material& material);
  void releaseMesh(Mesh& mesh);
  void releaseMaterial(Material& material);

  rhi::Device* device() { return device_; }
  const time::FrameStats& stats() const { return stats_; }
  uint32_t culledCount() const { return culledCount_; }
  uint32_t shadowTarget() const { return shadowTarget_; }

 private:
  void renderUiPass(const FrameUniforms& shared);
  void renderDebugPass();
  void renderParticlePass();

  rhi::Device* device_;
  scene::Scene* scene_;
  Camera camera_{};
  std::vector<Light> lights_;
  std::vector<RenderObject> objects_;
  std::vector<UiQuad> uiQuads_;
  std::vector<DebugLine> debugLines_;
  std::vector<float> particleInstances_;
  uint32_t particleCount_ = 0;

  RenderGraph graph_;
  uint32_t frameUniform_ = 0;
  uint32_t lightingUniform_ = 0;
  uint32_t objectUniform_ = 0;
  uint32_t shadowTarget_ = 0;
  uint32_t shadowResolution_ = 1024;
  bool shadowsEnabled_ = false;
  uint32_t shadowPipeline_ = 0;
  uint32_t uiPipeline_ = 0;
  uint32_t debugPipeline_ = 0;
  uint32_t uiVertexBuffer_ = 0;
  uint32_t debugVertexBuffer_ = 0;
  uint32_t screenWidth_ = 1280;
  uint32_t screenHeight_ = 720;
  uint32_t culledCount_ = 0;
  time::FrameStats stats_;
};

}  // namespace nimbus::render
