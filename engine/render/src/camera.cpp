// Nimbus render module: camera/projection helpers and shader sources.
#include "nimbus/render.hpp"

namespace nimbus::render {

math::Mat4 Renderer::viewMatrix() const {
  const math::Mat4 rotation = math::mat4FromQuat(camera_.rotation);
  const math::Mat4 translation = math::mat4FromTranslation(camera_.position * -1.0f);
  return math::mat4Multiply(rotation, translation);
}

math::Mat4 Renderer::projectionMatrix() const {
  return math::mat4Perspective(math::radians(camera_.fov), camera_.aspect, camera_.nearPlane,
                               camera_.farPlane);
}

FrameUniforms Renderer::buildUniforms() const {
  FrameUniforms uniforms{};
  uniforms.view = viewMatrix();
  uniforms.projection = projectionMatrix();
  uniforms.viewProjection = math::mat4Multiply(uniforms.projection, uniforms.view);
  uniforms.cameraPosition = {camera_.position.x, camera_.position.y, camera_.position.z, 1.0f};
  const math::Vec3 forward = math::rotate(camera_.rotation, {0, 0, -1});
  uniforms.cameraForward = {forward.x, forward.y, forward.z, 0.0f};
  uniforms.ambientColor = {0.05f, 0.05f, 0.06f, 1.0f};
  uniforms.lightCount = static_cast<uint32_t>(lights_.size());
  uniforms.objectCount = static_cast<uint32_t>(objects_.size());
  uniforms.time = 0.0f;
  return uniforms;
}

}  // namespace nimbus::render
