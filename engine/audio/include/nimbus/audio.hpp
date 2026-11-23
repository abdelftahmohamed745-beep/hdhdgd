// Nimbus audio: mixer graph, voices, 3D stereo spatialization, focus/pause, WAV loader, OpenSL ES backend.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "nimbus/math.hpp"

namespace nimbus::audio {

using math::Vec3;

enum class Format : uint8_t { Mono16, Stereo16, MonoFloat };

struct Clip {
  std::string name;
  Format format = Format::Mono16;
  uint32_t sampleRate = 44100;
  uint32_t channels = 1;
  std::vector<int16_t> pcm;
  uint32_t sampleCount() const {
    return static_cast<uint32_t>(pcm.size() / (channels == 2 ? 2 : 1));
  }
  bool valid() const {
    return (channels == 1 || channels == 2) && sampleRate > 0 && !pcm.empty() &&
           pcm.size() % channels == 0;
  }
};

struct Voice {
  const Clip* clip = nullptr;
  double position = 0.0;
  float volume = 1.0f;
  float pitch = 1.0f;
  bool loop = false;
  bool playing = false;
  bool paused = false;
  Vec3 position3d{0, 0, 0};
  float minDistance = 1.0f;
  float maxDistance = 50.0f;
  uint32_t handle = 0;
};

struct Listener {
  Vec3 position{0, 0, 0};
  Vec3 forward{0, 0, -1};
  Vec3 up{0, 1, 0};
};

class Backend {
 public:
  virtual ~Backend() = default;
  virtual bool open(uint32_t sampleRate, uint32_t bufferFrames) = 0;
  virtual void close() = 0;
  virtual void submit(const float* interleavedStereo, uint32_t frames) = 0;
  virtual bool valid() const = 0;
  virtual void setPaused(bool paused) { (void)paused; }
};

Backend* createOpenSlesBackend();

class Mixer {
 public:
  explicit Mixer(uint32_t sampleRate = 44100) : sampleRate_(sampleRate) {}
  ~Mixer() { shutdown(); }
  Mixer(const Mixer&) = delete;
  Mixer& operator=(const Mixer&) = delete;

  void setBackend(Backend* backend) {
    if (backend_ == backend) return;
    if (backend_) backend_->close();
    ownedBackend_.reset();
    backend_ = backend;
  }
  bool initialize();
  void shutdown();

  uint32_t play(const Clip& clip, float volume = 1.0f, bool loop = false);
  void stop(uint32_t handle);
  void pause(uint32_t handle);
  void resume(uint32_t handle);
  void pauseAll();
  void resumeAll();
  bool isPaused(uint32_t handle) const;
  bool allPaused() const { return allPaused_; }

  void setVolume(uint32_t handle, float volume);
  void setPitch(uint32_t handle, float pitch);
  void setPosition3d(uint32_t handle, const Vec3& position);
  void setListener(const Listener& listener);

  void mix(uint32_t frames);

  uint32_t activeVoices() const;
  uint32_t sampleRate() const { return sampleRate_; }
  Backend* backend() { return backend_; }
  const std::vector<float>& lastMixBuffer() const { return mixBuffer_; }

  static Clip generateTone(float frequency, float duration, uint32_t sampleRate = 44100);
  static Clip loadWavFromMemory(const uint8_t* data, size_t size, const std::string& name = "");
  static Clip loadWav(const std::string& path);

 private:
  uint32_t sampleRate_;
  Backend* backend_ = nullptr;
  std::unique_ptr<Backend> ownedBackend_;
  std::vector<Voice> voices_;
  std::vector<float> mixBuffer_;
  Listener listener_{};
  uint32_t nextHandle_ = 1;
  bool allPaused_ = false;
};

}  // namespace nimbus::audio
