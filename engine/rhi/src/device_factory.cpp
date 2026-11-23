#include "nimbus/rhi.hpp"

#include "nimbus/log.hpp"

namespace nimbus::rhi {

Device* createDevice(Api preferred, void* nativeWindow, uint32_t width, uint32_t height) {
  if (preferred == Api::Headless) {
    return createHeadlessDevice(width == 0 ? 320 : width, height == 0 ? 240 : height);
  }

  const auto tryVulkan = [&]() -> Device* {
#if defined(NIMBUS_HAS_VULKAN) && NIMBUS_HAS_VULKAN
    return createVulkanDevice(nativeWindow, width, height);
#else
    (void)nativeWindow;
    (void)width;
    (void)height;
    return nullptr;
#endif
  };
  const auto tryGles = [&]() -> Device* {
#if defined(NIMBUS_HAS_GLES) && NIMBUS_HAS_GLES
    return createGlesDevice(nativeWindow, width, height);
#else
    (void)nativeWindow;
    (void)width;
    (void)height;
    return nullptr;
#endif
  };

  if (preferred == Api::Vulkan) {
    if (Device* device = tryVulkan()) return device;
    log::write(log::Level::Warning, "rhi: Vulkan unavailable; trying OpenGL ES");
    return tryGles();
  }
  if (preferred == Api::OpenGlEs) {
    if (Device* device = tryGles()) return device;
    log::write(log::Level::Warning, "rhi: OpenGL ES unavailable; trying Vulkan");
    return tryVulkan();
  }
  if (Device* device = tryVulkan()) return device;
  if (Device* device = tryGles()) return device;
  return nullptr;
}

}  // namespace nimbus::rhi
