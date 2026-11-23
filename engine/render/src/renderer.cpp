#include "nimbus/render.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "nimbus/log.hpp"

namespace nimbus::render {

const char* defaultLitVertexShader() {
  return R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(std140) uniform FrameUniforms {
  mat4 uView;
  mat4 uProj;
  mat4 uViewProj;
  vec4 uCamPos;
  vec4 uCamForward;
  vec4 uAmbient;
  uvec4 uCounts;
};
layout(std140) uniform ObjectUniforms {
  mat4 uModel;
  mat4 uNormalMat;
  vec4 uBaseColor;
  vec4 uPbr;
};
out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUv;
void main() {
  vec4 wp = uModel * vec4(aPos, 1.0);
  vWorldPos = wp.xyz;
  vNormal = normalize(mat3(uNormalMat) * aNormal);
  vUv = aUv;
  gl_Position = uViewProj * wp;
}
)";
}

const char* defaultLitFragmentShader() {
  return R"(#version 300 es
precision highp float;
struct Light {
  vec4 posRange;
  vec4 dirType;
  vec4 colorIntensity;
  vec4 spotAngles;
};
layout(std140) uniform FrameUniforms {
  mat4 uView;
  mat4 uProj;
  mat4 uViewProj;
  vec4 uCamPos;
  vec4 uCamForward;
  vec4 uAmbient;
  uvec4 uCounts;
};
layout(std140) uniform LightingBlock {
  Light uLights[16];
  uvec4 uLightMeta;
  mat4 uLightSpace;
};
layout(std140) uniform ObjectUniforms {
  mat4 uModel;
  mat4 uNormalMat;
  vec4 uBaseColor;
  vec4 uPbr;
};
uniform sampler2D uAlbedoTex;
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUv;
out vec4 fragColor;
void main() {
  vec4 albedo = uBaseColor;
  if (uPbr.w > 0.5) albedo *= texture(uAlbedoTex, vUv);
  vec3 N = normalize(vNormal);
  vec3 V = normalize(uCamPos.xyz - vWorldPos);
  vec3 lit = albedo.rgb * uAmbient.rgb;
  uint n = min(uLightMeta.x, 16u);
  for (uint i = 0u; i < n; ++i) {
    vec3 L;
    float atten = uLights[i].colorIntensity.w;
    if (uLights[i].dirType.w < 0.5) {
      L = normalize(-uLights[i].dirType.xyz);
    } else {
      vec3 d = uLights[i].posRange.xyz - vWorldPos;
      float dist = max(length(d), 0.001);
      L = d / dist;
      float r = max(uLights[i].posRange.w, 0.001);
      float f = clamp(1.0 - dist / r, 0.0, 1.0);
      atten *= f * f;
    }
    vec3 H = normalize(L + V);
    float ndotl = max(dot(N, L), 0.0);
    float specPow = mix(128.0, 4.0, clamp(uPbr.y, 0.0, 1.0));
    float spec = pow(max(dot(N, H), 0.0), specPow) * mix(0.04, 0.8, clamp(uPbr.x, 0.0, 1.0));
    lit += (albedo.rgb * ndotl + vec3(spec)) * uLights[i].colorIntensity.rgb * atten;
  }
  lit += albedo.rgb * uPbr.z;
  fragColor = vec4(lit, albedo.a);
}
)";
}

const char* defaultUnlitVertexShader() {
  return R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(std140) uniform FrameUniforms {
  mat4 uView;
  mat4 uProj;
  mat4 uViewProj;
  vec4 uCamPos;
  vec4 uCamForward;
  vec4 uAmbient;
  uvec4 uCounts;
};
layout(std140) uniform ObjectUniforms {
  mat4 uModel;
  mat4 uNormalMat;
  vec4 uBaseColor;
  vec4 uPbr;
};
out vec2 vUv;
void main() {
  vUv = aUv;
  gl_Position = uViewProj * (uModel * vec4(aPos, 1.0));
}
)";
}

const char* defaultUnlitFragmentShader() {
  return R"(#version 300 es
precision highp float;
layout(std140) uniform ObjectUniforms {
  mat4 uModel;
  mat4 uNormalMat;
  vec4 uBaseColor;
  vec4 uPbr;
};
uniform sampler2D uAlbedoTex;
in vec2 vUv;
out vec4 fragColor;
void main() {
  vec4 c = uBaseColor;
  if (uPbr.w > 0.5) c *= texture(uAlbedoTex, vUv);
  fragColor = c;
}
)";
}

const char* defaultUiVertexShader() {
  return R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
layout(location = 2) in vec2 aUv;
out vec3 vColor;
out vec2 vUv;
void main() {
  vColor = aColor;
  vUv = aUv;
  gl_Position = vec4(aPos.xy, 0.0, 1.0);
}
)";
}

const char* defaultUiFragmentShader() {
  return R"(#version 300 es
precision highp float;
in vec3 vColor;
in vec2 vUv;
out vec4 fragColor;
void main() {
  fragColor = vec4(vColor, 1.0);
}
)";
}

Renderer::~Renderer() {
  if (!device_) return;
  if (frameUniform_ != 0) device_->destroyShaderUniform(frameUniform_);
  if (lightingUniform_ != 0) device_->destroyShaderUniform(lightingUniform_);
  if (objectUniform_ != 0) device_->destroyShaderUniform(objectUniform_);
  if (shadowTarget_ != 0) device_->destroyRenderTarget(shadowTarget_);
  if (shadowPipeline_ != 0) device_->destroyPipeline(shadowPipeline_);
  if (uiPipeline_ != 0) device_->destroyPipeline(uiPipeline_);
  if (debugPipeline_ != 0) device_->destroyPipeline(debugPipeline_);
  if (uiVertexBuffer_ != 0) device_->destroyBuffer(uiVertexBuffer_);
  if (debugVertexBuffer_ != 0) device_->destroyBuffer(debugVertexBuffer_);
}

LightingBlock Renderer::buildLightingBlock() const {
  LightingBlock block{};
  const uint32_t count = std::min<uint32_t>(static_cast<uint32_t>(lights_.size()), kMaxLights);
  block.count = count;
  block.shadowEnabled = shadowsEnabled_ ? 1u : 0u;
  for (uint32_t i = 0; i < count; ++i) {
    const Light& l = lights_[i];
    GpuLight& g = block.lights[i];
    g.positionRange = {l.position.x, l.position.y, l.position.z, l.range};
    const float typeVal = l.type == Light::Type::Directional ? 0.0f
                          : (l.type == Light::Type::Point ? 1.0f : 2.0f);
    const Vec3 dir = math::lengthSquared(l.direction) > 1e-6f ? math::normalize(l.direction)
                                                               : Vec3{0, -1, 0};
    g.directionType = {dir.x, dir.y, dir.z, typeVal};
    g.colorIntensity = {l.color.x, l.color.y, l.color.z, l.intensity};
    g.spotAngles = {std::cos(math::radians(l.innerCone)), std::cos(math::radians(l.outerCone)), 0.0f,
                    0.0f};
    if (l.type == Light::Type::Directional && i == 0) {
      const Vec3 target = camera_.position;
      const Vec3 lightPos = target - dir * 30.0f;
      const math::Mat4 lv = math::mat4LookAt(lightPos, target, {0, 1, 0});
      const math::Mat4 lp = math::mat4Ortho(-25.0f, 25.0f, -25.0f, 25.0f, 0.1f, 80.0f);
      block.lightSpaceMatrix = math::mat4Multiply(lp, lv);
    }
  }
  return block;
}

void Renderer::setShadowsEnabled(bool enabled, uint32_t resolution) {
  shadowsEnabled_ = enabled;
  shadowResolution_ = std::max(256u, resolution);
  if (!device_) return;
  if (!enabled && shadowTarget_ != 0) {
    device_->destroyRenderTarget(shadowTarget_);
    shadowTarget_ = 0;
  } else if (enabled && shadowTarget_ == 0) {
    shadowTarget_ =
        device_->createRenderTarget(shadowResolution_, shadowResolution_,
                                    rhi::Format::R8G8B8A8Unorm, true);
  }
}

void Renderer::submitParticles(const float* instanceData, uint32_t count) {
  particleCount_ = count;
  if (!instanceData || count == 0) {
    particleInstances_.clear();
    particleCount_ = 0;
    return;
  }
  particleInstances_.assign(instanceData, instanceData + static_cast<size_t>(count) * 8u);
}

void Renderer::drawDebugBox(const math::Aabb& box, const Vec4& color) {
  const Vec3& mn = box.min;
  const Vec3& mx = box.max;
  const Vec3 c[8] = {
      {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z},
      {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z},
  };
  const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                            {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
  for (const auto& e : edges) drawDebugLine(c[e[0]], c[e[1]], color);
}

bool Renderer::uploadMesh(Mesh& mesh) {
  if (!device_) return false;
  if (mesh.gpuReady)
    return mesh.vertexBuffer != 0 && (mesh.indices.empty() || mesh.indexBuffer != 0);
  if (mesh.vertices.empty() || mesh.vertices.size() % 8u != 0u) return false;

  if (!mesh.hasBounds) mesh.computeBounds();

  const size_t vertexCount = mesh.vertices.size() / 8u;
  if (vertexCount == 0 || vertexCount > std::numeric_limits<uint32_t>::max() ||
      mesh.vertices.size() > std::numeric_limits<uint32_t>::max() / sizeof(float))
    return false;

  if (mesh.indices.empty()) {
    if (vertexCount % 3u != 0u) return false;
  } else {
    if (mesh.indices.size() % 3u != 0u ||
        mesh.indices.size() > std::numeric_limits<uint32_t>::max() / sizeof(uint32_t))
      return false;
    for (const uint32_t index : mesh.indices)
      if (index >= vertexCount) return false;
  }

  const rhi::BufferDesc vertexDesc{static_cast<uint32_t>(mesh.vertices.size() * sizeof(float)),
                                   true, false, false, false, false};
  const uint32_t vertexBuffer = device_->createBuffer(vertexDesc, mesh.vertices.data());
  if (vertexBuffer == 0) return false;

  uint32_t indexBuffer = 0;
  if (!mesh.indices.empty()) {
    const rhi::BufferDesc indexDesc{static_cast<uint32_t>(mesh.indices.size() * sizeof(uint32_t)),
                                    false, true, false, false, false};
    indexBuffer = device_->createBuffer(indexDesc, mesh.indices.data());
    if (indexBuffer == 0) {
      device_->destroyBuffer(vertexBuffer);
      return false;
    }
  }

  mesh.vertexBuffer = vertexBuffer;
  mesh.indexBuffer = indexBuffer;
  mesh.indexCount = static_cast<uint32_t>(mesh.indices.size());
  mesh.gpuReady = true;
  return true;
}

bool Renderer::uploadMaterial(Material& material) {
  if (!device_) return false;
  if (material.pipeline != 0) return true;
  if (material.vertexShader.empty()) {
    material.vertexShader = material.unlit ? defaultUnlitVertexShader() : defaultLitVertexShader();
  }
  if (material.fragmentShader.empty()) {
    material.fragmentShader =
        material.unlit ? defaultUnlitFragmentShader() : defaultLitFragmentShader();
  }
  rhi::PipelineDesc desc;
  desc.vertexShader = material.vertexShader;
  desc.fragmentShader = material.fragmentShader;
  desc.colorFormat = rhi::Format::R8G8B8A8Unorm;
  desc.depthFormat = rhi::Format::D24UnormS8Uint;
  desc.primitive = material.wireframe ? rhi::Primitive::Lines : rhi::Primitive::Triangles;
  desc.cullMode = material.cullMode;
  desc.depthTest = true;
  desc.depthWrite = material.baseColor.w >= 1.0f;
  desc.depthCompare = rhi::CompareOp::Less;
  desc.blendEnable = material.baseColor.w < 1.0f;
  material.pipeline = device_->createPipeline(desc);
  return material.pipeline != 0;
}

void Renderer::releaseMesh(Mesh& mesh) {
  if (device_) {
    if (mesh.indexBuffer != 0) device_->destroyBuffer(mesh.indexBuffer);
    if (mesh.vertexBuffer != 0) device_->destroyBuffer(mesh.vertexBuffer);
  }
  mesh.vertexBuffer = 0;
  mesh.indexBuffer = 0;
  mesh.indexCount = 0;
  mesh.gpuReady = false;
}

void Renderer::releaseMaterial(Material& material) {
  if (device_ && material.pipeline != 0) device_->destroyPipeline(material.pipeline);
  material.pipeline = 0;
}

void Renderer::renderUiPass(const FrameUniforms& shared) {
  (void)shared;
  if (uiQuads_.empty() || !device_) return;
  if (uiPipeline_ == 0) {
    rhi::PipelineDesc desc;
    desc.vertexShader = defaultUiVertexShader();
    desc.fragmentShader = defaultUiFragmentShader();
    desc.cullMode = rhi::CullMode::None;
    desc.depthTest = false;
    desc.depthWrite = false;
    desc.blendEnable = true;
    uiPipeline_ = device_->createPipeline(desc);
  }
  if (uiPipeline_ == 0) return;

  std::vector<float> verts;
  verts.reserve(uiQuads_.size() * 6u * 8u);
  const float invW = 2.0f / static_cast<float>(screenWidth_);
  const float invH = 2.0f / static_cast<float>(screenHeight_);
  for (const UiQuad& q : uiQuads_) {
    const float x0 = q.x * invW - 1.0f;
    const float x1 = (q.x + q.width) * invW - 1.0f;
    const float y0 = 1.0f - q.y * invH;
    const float y1 = 1.0f - (q.y + q.height) * invH;
    const float corners[6][8] = {
        {x0, y0, 0, q.color.x, q.color.y, q.color.z, q.u0, q.v0},
        {x0, y1, 0, q.color.x, q.color.y, q.color.z, q.u0, q.v1},
        {x1, y1, 0, q.color.x, q.color.y, q.color.z, q.u1, q.v1},
        {x0, y0, 0, q.color.x, q.color.y, q.color.z, q.u0, q.v0},
        {x1, y1, 0, q.color.x, q.color.y, q.color.z, q.u1, q.v1},
        {x1, y0, 0, q.color.x, q.color.y, q.color.z, q.u1, q.v0},
    };
    for (const auto& v : corners) verts.insert(verts.end(), std::begin(v), std::end(v));
  }

  if (uiVertexBuffer_ != 0) device_->destroyBuffer(uiVertexBuffer_);
  uiVertexBuffer_ = device_->createBuffer(
      rhi::BufferDesc{static_cast<uint32_t>(verts.size() * sizeof(float)), true, false, false, false, true},
      verts.data());
  if (uiVertexBuffer_ == 0) return;

  device_->bindPipeline(uiPipeline_);
  device_->bindVertexBuffer(uiVertexBuffer_, 0);
  const uint32_t vertexCount = static_cast<uint32_t>(verts.size() / 8u);
  device_->draw(vertexCount, 0);
  stats_.drawCalls += 1;
  stats_.triangles += vertexCount / 3u;
}

void Renderer::renderDebugPass() {
  if (debugLines_.empty() || !device_) return;
  if (debugPipeline_ == 0) {
    rhi::PipelineDesc desc;
    desc.vertexShader = defaultUnlitVertexShader();
    desc.fragmentShader = defaultUnlitFragmentShader();
    desc.primitive = rhi::Primitive::Lines;
    desc.cullMode = rhi::CullMode::None;
    desc.depthTest = true;
    desc.depthWrite = false;
    debugPipeline_ = device_->createPipeline(desc);
  }
  if (debugPipeline_ == 0) return;

  std::vector<float> verts;
  verts.reserve(debugLines_.size() * 2u * 8u);
  for (const DebugLine& line : debugLines_) {
    const float v0[8] = {line.start.x, line.start.y, line.start.z, line.color.x, line.color.y,
                         line.color.z, 0.0f, 0.0f};
    const float v1[8] = {line.end.x, line.end.y, line.end.z, line.color.x, line.color.y,
                         line.color.z, 1.0f, 1.0f};
    verts.insert(verts.end(), std::begin(v0), std::end(v0));
    verts.insert(verts.end(), std::begin(v1), std::end(v1));
  }
  if (debugVertexBuffer_ != 0) device_->destroyBuffer(debugVertexBuffer_);
  debugVertexBuffer_ = device_->createBuffer(
      rhi::BufferDesc{static_cast<uint32_t>(verts.size() * sizeof(float)), true, false, false, false, true},
      verts.data());
  if (debugVertexBuffer_ == 0) return;

  ObjectUniforms identityObj{};
  device_->bindPipeline(debugPipeline_);
  device_->bindUniform(objectUniform_, 2, &identityObj, sizeof(identityObj));
  device_->bindVertexBuffer(debugVertexBuffer_, 0);
  device_->draw(static_cast<uint32_t>(debugLines_.size() * 2u), 0);
  stats_.drawCalls += 1;
}

void Renderer::renderParticlePass() {
  if (particleCount_ == 0 || particleInstances_.size() < 8u || !device_) return;
  // Submit particle billboard quads sorted by distance or sequentially.
  if (uiPipeline_ == 0) {
    rhi::PipelineDesc desc;
    desc.vertexShader = defaultUnlitVertexShader();
    desc.fragmentShader = defaultUnlitFragmentShader();
    desc.cullMode = rhi::CullMode::None;
    desc.depthTest = true;
    desc.depthWrite = false;
    desc.blendEnable = true;
    uiPipeline_ = device_->createPipeline(desc);
  }
  if (uiPipeline_ == 0) return;
  const uint32_t count = std::min<uint32_t>(
      particleCount_, static_cast<uint32_t>(particleInstances_.size() / 8u));
  std::vector<float> verts;
  verts.reserve(count * 6u * 8u);
  const Vec3 right = math::rotate(camera_.rotation, {1, 0, 0});
  const Vec3 up = math::rotate(camera_.rotation, {0, 1, 0});
  for (uint32_t i = 0; i < count; ++i) {
    const float* p = &particleInstances_[static_cast<size_t>(i) * 8u];
    const Vec3 center{p[0], p[1], p[2]};
    const float hs = std::max(0.01f, p[3] * 0.5f);
    const Vec3 r = right * hs;
    const Vec3 u = up * hs;
    const Vec3 p0 = center - r - u;
    const Vec3 p1 = center + r - u;
    const Vec3 p2 = center + r + u;
    const Vec3 p3 = center - r + u;
    const float tri[6][8] = {
        {p0.x, p0.y, p0.z, p[4], p[5], p[6], 0, 0},
        {p1.x, p1.y, p1.z, p[4], p[5], p[6], 1, 0},
        {p2.x, p2.y, p2.z, p[4], p[5], p[6], 1, 1},
        {p0.x, p0.y, p0.z, p[4], p[5], p[6], 0, 0},
        {p2.x, p2.y, p2.z, p[4], p[5], p[6], 1, 1},
        {p3.x, p3.y, p3.z, p[4], p[5], p[6], 0, 1},
    };
    for (const auto& v : tri) verts.insert(verts.end(), std::begin(v), std::end(v));
  }
  const uint32_t pbo = device_->createBuffer(
      rhi::BufferDesc{static_cast<uint32_t>(verts.size() * sizeof(float)), true, false, false, false, true},
      verts.data());
  if (pbo == 0) return;
  ObjectUniforms identityObj{};
  device_->bindPipeline(uiPipeline_);
  device_->bindUniform(objectUniform_, 2, &identityObj, sizeof(identityObj));
  device_->bindVertexBuffer(pbo, 0);
  const uint32_t vCount = static_cast<uint32_t>(verts.size() / 8u);
  device_->draw(vCount, 0);
  device_->destroyBuffer(pbo);
  stats_.drawCalls += 1;
  stats_.triangles += vCount / 3u;
}

void Renderer::render(const FrameUniforms& shared) {
  stats_ = time::FrameStats{};
  culledCount_ = 0;
  if (!device_ || !scene_) return;
  if (frameUniform_ == 0) frameUniform_ = device_->createShaderUniform(sizeof(FrameUniforms));
  if (lightingUniform_ == 0)
    lightingUniform_ = device_->createShaderUniform(sizeof(LightingBlock));
  if (objectUniform_ == 0) objectUniform_ = device_->createShaderUniform(sizeof(ObjectUniforms));
  if (frameUniform_ == 0 || lightingUniform_ == 0 || objectUniform_ == 0) {
    log::write(log::Level::Error, "render: failed to allocate uniform buffers");
    return;
  }

  device_->beginFrame();

  const LightingBlock lighting = buildLightingBlock();
  const math::Frustum frustum = math::frustumFromViewProj(shared.viewProjection);

  // Gather visible objects via world AABB frustum test.
  std::vector<std::pair<const RenderObject*, math::Mat4>> visible;
  visible.reserve(objects_.size());
  for (const RenderObject& object : objects_) {
    if (!object.mesh || !object.material) continue;
    const scene::Node* node = scene_->node(object.entity);
    if (!node) continue;
    const math::Mat4 world = scene_->world(object.entity);
    if (!object.mesh->hasBounds) object.mesh->computeBounds();
    bool isVisible = true;
    if (object.mesh->hasBounds) {
      const math::Aabb worldAabb = math::aabbTransform(object.mesh->localBounds, world);
      isVisible = math::frustumIntersectsAabb(frustum, worldAabb);
    } else {
      const math::Vec4 center = math::mat4Transform(world, {0, 0, 0, 1});
      isVisible = math::frustumContainsPoint(frustum, {center.x, center.y, center.z});
    }
    if (!isVisible) {
      ++culledCount_;
      continue;
    }
    if (!uploadMesh(*object.mesh) || !uploadMaterial(*object.material)) continue;
    visible.emplace_back(&object, world);
  }

  // Optional directional shadow pass into offscreen render target.
  if (shadowsEnabled_ && shadowTarget_ != 0 && !lights_.empty()) {
    rhi::RenderPassDesc shadowPass;
    shadowPass.clearColor = true;
    shadowPass.clearDepth = true;
    device_->beginRenderTarget(shadowTarget_, shadowPass);
    FrameUniforms shadowFrame = shared;
    shadowFrame.viewProjection = lighting.lightSpaceMatrix;
    device_->bindUniform(frameUniform_, 0, &shadowFrame, sizeof(shadowFrame));
    for (const auto& [obj, world] : visible) {
      if (!obj->castShadow) continue;
      ObjectUniforms ou{};
      ou.model = world;
      device_->bindPipeline(obj->material->pipeline);
      device_->bindUniform(objectUniform_, 2, &ou, sizeof(ou));
      device_->bindVertexBuffer(obj->mesh->vertexBuffer, 0);
      if (obj->mesh->indexBuffer) {
        device_->bindIndexBuffer(obj->mesh->indexBuffer);
        device_->drawIndexed(obj->mesh->indexCount, 0, 0);
      } else {
        device_->draw(static_cast<uint32_t>(obj->mesh->vertices.size() / 8u), 0);
      }
    }
    device_->endPass();
  }

  // Custom render graph passes (if registered).
  if (graph_.passCount() > 0) {
    graph_.execute(*device_);
  }

  // Main forward PBR/Lit scene pass.
  device_->bindUniform(frameUniform_, 0, &shared, sizeof(shared));
  device_->bindUniform(lightingUniform_, 1, &lighting, sizeof(lighting));
  rhi::RenderPassDesc pass;
  pass.colorFormat = rhi::Format::R8G8B8A8Unorm;
  pass.depthFormat = rhi::Format::D24UnormS8Uint;
  device_->beginPass(pass);

  uint32_t drawn = 0;
  uint64_t triangles = 0;
  for (const auto& [obj, world] : visible) {
    ObjectUniforms ou{};
    ou.model = world;
    ou.normalMatrix = world;
    ou.baseColor = obj->material->baseColor;
    ou.pbrParams = {obj->material->metallic, obj->material->roughness, obj->material->emissive,
                    obj->material->texture != 0 ? 1.0f : 0.0f};
    device_->bindPipeline(obj->material->pipeline);
    device_->bindUniform(objectUniform_, 2, &ou, sizeof(ou));
    if (obj->material->texture != 0) device_->bindTexture(obj->material->texture, 0);
    if (obj->material->normalTexture != 0) device_->bindTexture(obj->material->normalTexture, 1);
    if (obj->material->roughnessTexture != 0)
      device_->bindTexture(obj->material->roughnessTexture, 2);

    device_->bindVertexBuffer(obj->mesh->vertexBuffer, 0);
    if (obj->mesh->indexBuffer) {
      device_->bindIndexBuffer(obj->mesh->indexBuffer);
      device_->drawIndexed(obj->mesh->indexCount, 0, 0);
      triangles += obj->mesh->indexCount / 3u;
    } else {
      const uint32_t vertexCount = static_cast<uint32_t>(obj->mesh->vertices.size() / 8u);
      device_->draw(vertexCount, 0);
      triangles += vertexCount / 3u;
    }
    ++drawn;
  }

  renderParticlePass();
  renderDebugPass();
  renderUiPass(shared);

  device_->endPass();
  device_->endFrame();
  device_->present();
  stats_.drawCalls += drawn;
  stats_.triangles += static_cast<uint32_t>(
      std::min<uint64_t>(triangles, std::numeric_limits<uint32_t>::max()));
  log::write(log::Level::Debug, "render: %u drawn, %u culled, %u lights", drawn, culledCount_,
             static_cast<uint32_t>(lights_.size()));
}

}  // namespace nimbus::render
