#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "nimbus/audio.hpp"
#include "nimbus/engine.hpp"
#include "nimbus/log.hpp"

#ifdef NIMBUS_HAS_EDITOR
#include "nimbus/editor.hpp"
#endif

namespace {
constexpr const char* kTag = "NimbusEngine";
std::atomic<bool> gRunning{false};
std::atomic<bool> gPaused{false};
std::atomic<float> gOrbitYaw{0.4f};
std::atomic<float> gOrbitPitch{0.25f};
std::thread gRenderThread;
std::mutex gStartMutex;
std::condition_variable gStartCondition;
bool gStartCompleted = false;
bool gStartSucceeded = false;

std::mutex gEngineMutex;
nimbus::Engine* gActiveEngine = nullptr;
#ifdef NIMBUS_HAS_EDITOR
std::unique_ptr<nimbus::editor::Editor> gActiveEditor;
#endif

void androidLog(nimbus::log::Level level, const char* message, void*) {
  int priority = ANDROID_LOG_INFO;
  switch (level) {
    case nimbus::log::Level::Debug: priority = ANDROID_LOG_DEBUG; break;
    case nimbus::log::Level::Info: priority = ANDROID_LOG_INFO; break;
    case nimbus::log::Level::Warning: priority = ANDROID_LOG_WARN; break;
    case nimbus::log::Level::Error: priority = ANDROID_LOG_ERROR; break;
    case nimbus::log::Level::Fatal: priority = ANDROID_LOG_FATAL; break;
  }
  __android_log_write(priority, kTag, message ? message : "");
}

void completeStart(bool success) {
  {
    std::lock_guard<std::mutex> lock(gStartMutex);
    gStartSucceeded = success;
    gStartCompleted = true;
  }
  gStartCondition.notify_all();
}

void stopRuntime() {
  gRunning.store(false, std::memory_order_release);
  if (gRenderThread.joinable()) gRenderThread.join();
}

}  // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_com_nimbus_engine_MainActivity_nativeStart(JNIEnv* env, jobject, jobject surface,
                                                 jint width, jint height) {
  stopRuntime();
  if (!surface || width <= 0 || height <= 0) return JNI_FALSE;

  ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
  if (!window) {
    __android_log_write(ANDROID_LOG_ERROR, kTag, "ANativeWindow_fromSurface failed");
    return JNI_FALSE;
  }

  {
    std::lock_guard<std::mutex> lock(gStartMutex);
    gStartCompleted = false;
    gStartSucceeded = false;
  }
  gRunning.store(true, std::memory_order_release);
  gPaused.store(false, std::memory_order_release);

  try {
    gRenderThread = std::thread([window, width, height]() {
      struct WindowRelease {
        ANativeWindow* value;
        ~WindowRelease() { if (value) ANativeWindow_release(value); }
      } windowRelease{window};
      bool startWasReported = false;
      try {
        nimbus::log::setSink(androidLog);
        nimbus::Engine engine;
        const bool initialized = engine.initialize(
            "Nimbus Android Runtime", static_cast<uint32_t>(width),
            static_cast<uint32_t>(height), nimbus::rhi::Api::OpenGlEs, window);
        if (!initialized || !engine.device() || !engine.renderer()) {
          nimbus::log::write(nimbus::log::Level::Error,
                             "android: engine or GLES renderer initialization failed");
          engine.shutdown();
          gRunning.store(false, std::memory_order_release);
          completeStart(false);
          startWasReported = true;
          return;
        }

        {
          std::lock_guard<std::mutex> lock(gEngineMutex);
          gActiveEngine = &engine;
#ifdef NIMBUS_HAS_EDITOR
          gActiveEditor = std::make_unique<nimbus::editor::Editor>(&engine);
#endif
        }

        nimbus::render::Camera camera;
        camera.aspect = static_cast<float>(width) / static_cast<float>(height);
        camera.position = {0.0f, 1.4f, 4.2f};
        engine.renderer()->setCamera(camera);

        // Add directional sun light + lit procedural cube via built-in mesh factory.
        const nimbus::ecs::Entity sun = engine.createEntity("SunLight");
        nimbus::render::Light sunLight;
        sunLight.type = nimbus::render::Light::Type::Directional;
        sunLight.direction = nimbus::math::normalize(nimbus::math::Vec3{-0.5f, -1.0f, -0.4f});
        sunLight.color = {1.0f, 0.96f, 0.9f};
        sunLight.intensity = 1.25f;
        engine.world()->add<nimbus::render::Light>(sun, sunLight);

        const nimbus::ecs::Entity cubeEntity = engine.createEntity("Nimbus Cube");
        if (engine.scene()) {
          nimbus::scene::Transform tf;
          tf.position = {0.0f, 0.4f, 0.0f};
          engine.scene()->setLocal(cubeEntity, tf);
        }

        nimbus::render::Mesh cubeMesh = nimbus::render::createCube(1.3f);
        nimbus::render::Material cubeMaterial;
        cubeMaterial.baseColor = {0.22f, 0.74f, 0.97f, 1.0f};
        cubeMaterial.roughness = 0.35f;
        cubeMaterial.metallic = 0.15f;
        cubeMaterial.vertexShader = nimbus::render::defaultLitVertexShader();
        cubeMaterial.fragmentShader = nimbus::render::defaultLitFragmentShader();
        engine.renderer()->addObject({cubeEntity, &cubeMesh, &cubeMaterial, false});

        engine.play();
        completeStart(true);
        startWasReported = true;

        auto previous = std::chrono::steady_clock::now();
        auto nextFrame = previous;
        constexpr auto frameDuration = std::chrono::milliseconds(16);
        float angle = 0.0f;
        while (gRunning.load(std::memory_order_acquire)) {
          const auto now = std::chrono::steady_clock::now();
          const double delta = std::chrono::duration<double>(now - previous).count();
          previous = now;

          {
            std::lock_guard<std::mutex> lock(gEngineMutex);
            const float yaw = gOrbitYaw.load(std::memory_order_relaxed);
            const float pitch = gOrbitPitch.load(std::memory_order_relaxed);
            nimbus::render::Camera cam = camera;
            const float r = 4.5f;
            cam.position = {
                r * std::cos(pitch) * std::sin(yaw),
                1.0f + r * std::sin(pitch),
                r * std::cos(pitch) * std::cos(yaw),
            };
            cam.rotation = nimbus::math::quatFromAxisAngle({0.0f, 1.0f, 0.0f}, yaw);
            engine.renderer()->setCamera(cam);

            if (!gPaused.load(std::memory_order_acquire)) {
              angle += static_cast<float>(delta) * 0.9f;
              if (engine.scene()) {
                if (auto* tf = engine.scene()->local(cubeEntity)) {
                  tf->rotation = nimbus::math::quatFromAxisAngle({0.0f, 1.0f, 0.0f}, angle);
                  engine.scene()->markDirty(cubeEntity);
                }
              }
              engine.tick(delta);
            }
            engine.renderFrame();
          }

          nextFrame += frameDuration;
          const auto afterRender = std::chrono::steady_clock::now();
          if (nextFrame > afterRender) std::this_thread::sleep_until(nextFrame);
          else nextFrame = afterRender;
        }

        {
          std::lock_guard<std::mutex> lock(gEngineMutex);
#ifdef NIMBUS_HAS_EDITOR
          gActiveEditor.reset();
#endif
          gActiveEngine = nullptr;
        }
        engine.renderer()->clearObjects();
        engine.renderer()->releaseMesh(cubeMesh);
        engine.renderer()->releaseMaterial(cubeMaterial);
        engine.shutdown();
        nimbus::log::write(nimbus::log::Level::Info, "android: runtime stopped cleanly");
      } catch (const std::exception& error) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "Native runtime exception: %s", error.what());
        gRunning.store(false, std::memory_order_release);
        if (!startWasReported) completeStart(false);
      } catch (...) {
        __android_log_write(ANDROID_LOG_ERROR, kTag, "Unknown native runtime exception");
        gRunning.store(false, std::memory_order_release);
        if (!startWasReported) completeStart(false);
      }
    });
  } catch (const std::exception& error) {
    ANativeWindow_release(window);
    gRunning.store(false, std::memory_order_release);
    __android_log_print(ANDROID_LOG_ERROR, kTag, "Could not create render thread: %s", error.what());
    return JNI_FALSE;
  }

  std::unique_lock<std::mutex> lock(gStartMutex);
  const bool signaled = gStartCondition.wait_for(
      lock, std::chrono::seconds(15), [] { return gStartCompleted; });
  const bool succeeded = signaled && gStartSucceeded;
  lock.unlock();
  if (!succeeded) {
    gRunning.store(false, std::memory_order_release);
    if (gRenderThread.joinable()) gRenderThread.join();
    return JNI_FALSE;
  }
  return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_nimbus_engine_MainActivity_nativeStop(JNIEnv*, jobject) {
  stopRuntime();
}

extern "C" JNIEXPORT void JNICALL
Java_com_nimbus_engine_MainActivity_nativeTouch(JNIEnv*, jobject, jint pointerId, jint action,
                                                 jfloat x, jfloat y, jfloat dx, jfloat dy) {
  if (action == 2) {  // MOVE
    gOrbitYaw.store(gOrbitYaw.load() - dx * 0.008f, std::memory_order_relaxed);
    const float nextPitch = std::max(-1.1f, std::min(1.2f, gOrbitPitch.load() + dy * 0.008f));
    gOrbitPitch.store(nextPitch, std::memory_order_relaxed);
  }
  std::lock_guard<std::mutex> lock(gEngineMutex);
  if (gActiveEngine && gActiveEngine->input()) {
    const auto phase = action == 0   ? nimbus::input::TouchPhase::Began
                       : action == 2 ? nimbus::input::TouchPhase::Moved
                                     : nimbus::input::TouchPhase::Ended;
    gActiveEngine->input()->feedTouch(static_cast<int32_t>(pointerId), phase, x, y);
  }
}

extern "C" JNIEXPORT void JNICALL
Java_com_nimbus_engine_MainActivity_nativeSetPaused(JNIEnv*, jobject, jboolean paused) {
  gPaused.store(paused == JNI_TRUE, std::memory_order_release);
  std::lock_guard<std::mutex> lock(gEngineMutex);
  if (gActiveEngine) {
    if (paused == JNI_TRUE) gActiveEngine->pause();
    else gActiveEngine->play();
  }
}

extern "C" JNIEXPORT void JNICALL
Java_com_nimbus_engine_MainActivity_nativePlayTone(JNIEnv*, jobject, jfloat hz) {
  std::lock_guard<std::mutex> lock(gEngineMutex);
  if (gActiveEngine && gActiveEngine->audio()) {
    const auto clip = nimbus::audio::Mixer::generateTone(hz > 20.0f ? hz : 440.0f, 0.25f, 44100);
    gActiveEngine->audio()->play(clip, 0.5f, false);
  }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_nimbus_engine_MainActivity_nativeCreateEntity(JNIEnv* env, jobject, jstring name) {
  const char* raw = name ? env->GetStringUTFChars(name, nullptr) : nullptr;
  std::string entityName = raw ? raw : "Entity";
  if (raw) env->ReleaseStringUTFChars(name, raw);

  std::lock_guard<std::mutex> lock(gEngineMutex);
  if (!gActiveEngine) return 0;
  const auto e = gActiveEngine->createEntity(entityName);
  return static_cast<jint>(e);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_nimbus_engine_MainActivity_nativeSaveProject(JNIEnv* env, jobject, jstring path) {
#ifdef NIMBUS_HAS_EDITOR
  const char* raw = path ? env->GetStringUTFChars(path, nullptr) : nullptr;
  std::string projectDir = raw ? raw : "";
  if (raw) env->ReleaseStringUTFChars(path, raw);
  std::lock_guard<std::mutex> lock(gEngineMutex);
  if (!gActiveEditor || projectDir.empty()) return JNI_FALSE;
  if (gActiveEditor->projectPath().empty()) {
    gActiveEditor->newProject(projectDir, "AndroidProject", "3d_action");
  }
  return gActiveEditor->saveProject(projectDir) ? JNI_TRUE : JNI_FALSE;
#else
  (void)env;
  (void)path;
  return JNI_FALSE;
#endif
}

extern "C" JNIEXPORT jint JNICALL
Java_com_nimbus_engine_MainActivity_nativeEntityCount(JNIEnv*, jobject) {
  std::lock_guard<std::mutex> lock(gEngineMutex);
  if (!gActiveEngine || !gActiveEngine->world()) return 0;
  return static_cast<jint>(gActiveEngine->world()->entityCount());
}
