#include "nimbus/audio.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

#include "nimbus/log.hpp"

namespace nimbus::audio {
namespace {

uint16_t readU16Le(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8u);
}

uint32_t readU32Le(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8u) |
         (static_cast<uint32_t>(p[2]) << 16u) | (static_cast<uint32_t>(p[3]) << 24u);
}

}  // namespace

bool Mixer::initialize() {
  shutdown();
  if (sampleRate_ == 0) {
    log::write(log::Level::Error, "audio: sample rate must be non-zero");
    return false;
  }
#ifdef __ANDROID__
  if (!backend_) {
    ownedBackend_.reset(createOpenSlesBackend());
    backend_ = ownedBackend_.get();
  }
#endif
  if (backend_ && !backend_->open(sampleRate_, 1024)) {
    log::write(log::Level::Error, "audio: backend open failed");
    return false;
  }
  mixBuffer_.resize(static_cast<size_t>(sampleRate_) / 30 * 2);
  allPaused_ = false;
  return true;
}

void Mixer::shutdown() {
  if (backend_) backend_->close();
  voices_.clear();
}

uint32_t Mixer::play(const Clip& clip, float volume, bool loop) {
  if ((clip.channels != 1 && clip.channels != 2) || clip.sampleRate == 0 ||
      clip.pcm.empty() || clip.pcm.size() % clip.channels != 0) {
    log::write(log::Level::Warning, "audio: refusing an invalid or empty clip");
    return 0;
  }
  Voice voice;
  voice.clip = &clip;
  voice.volume = std::isfinite(volume) ? std::clamp(volume, 0.0f, 4.0f) : 1.0f;
  voice.loop = loop;
  voice.playing = true;
  voice.handle = nextHandle_++;
  voices_.push_back(voice);
  return voice.handle;
}

void Mixer::stop(uint32_t handle) {
  for (size_t i = 0; i < voices_.size();) {
    if (voices_[i].handle == handle) {
      voices_[i] = voices_.back();
      voices_.pop_back();
    } else {
      ++i;
    }
  }
}

void Mixer::pause(uint32_t handle) {
  for (Voice& voice : voices_) {
    if (voice.handle == handle) voice.paused = true;
  }
}

void Mixer::resume(uint32_t handle) {
  for (Voice& voice : voices_) {
    if (voice.handle == handle) voice.paused = false;
  }
}

void Mixer::pauseAll() {
  allPaused_ = true;
  if (backend_) backend_->setPaused(true);
}

void Mixer::resumeAll() {
  allPaused_ = false;
  if (backend_) backend_->setPaused(false);
}

bool Mixer::isPaused(uint32_t handle) const {
  for (const Voice& voice : voices_) {
    if (voice.handle == handle) return voice.paused;
  }
  return false;
}

void Mixer::setVolume(uint32_t handle, float volume) {
  if (!std::isfinite(volume)) return;
  volume = std::clamp(volume, 0.0f, 4.0f);
  for (Voice& voice : voices_)
    if (voice.handle == handle) voice.volume = volume;
}

void Mixer::setPitch(uint32_t handle, float pitch) {
  if (!std::isfinite(pitch) || pitch <= 0.0f) return;
  pitch = std::clamp(pitch, 0.05f, 8.0f);
  for (Voice& voice : voices_)
    if (voice.handle == handle) voice.pitch = pitch;
}

void Mixer::setPosition3d(uint32_t handle, const Vec3& position) {
  if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
      !std::isfinite(position.z)) return;
  for (Voice& voice : voices_)
    if (voice.handle == handle) voice.position3d = position;
}

void Mixer::setListener(const Listener& listener) {
  if (!std::isfinite(listener.position.x) || !std::isfinite(listener.position.y) ||
      !std::isfinite(listener.position.z) || !std::isfinite(listener.forward.x) ||
      !std::isfinite(listener.forward.y) || !std::isfinite(listener.forward.z) ||
      !std::isfinite(listener.up.x) || !std::isfinite(listener.up.y) ||
      !std::isfinite(listener.up.z)) return;
  listener_ = listener;
}

uint32_t Mixer::activeVoices() const {
  if (allPaused_) return 0;
  uint32_t count = 0;
  for (const Voice& voice : voices_)
    if (voice.playing && !voice.paused) ++count;
  return count;
}

void Mixer::mix(uint32_t frames) {
  const uint32_t capacity = static_cast<uint32_t>(mixBuffer_.size() / 2);
  if (capacity == 0) return;
  if (frames > capacity) frames = capacity;
  std::fill(mixBuffer_.begin(), mixBuffer_.begin() + static_cast<size_t>(frames) * 2, 0.0f);
  if (allPaused_) {
    if (backend_ && backend_->valid()) backend_->submit(mixBuffer_.data(), frames);
    return;
  }

  Vec3 forward = math::normalize(listener_.forward);
  if (math::lengthSquared(forward) < 1e-6f) forward = {0, 0, -1};
  Vec3 up = math::normalize(listener_.up);
  if (math::lengthSquared(up) < 1e-6f) up = {0, 1, 0};
  Vec3 right = math::normalize(math::cross(forward, up));
  if (math::lengthSquared(right) < 1e-6f) right = {1, 0, 0};

  for (size_t i = 0; i < voices_.size();) {
    Voice& voice = voices_[i];
    if (!voice.playing || !voice.clip) {
      voices_[i] = voices_.back();
      voices_.pop_back();
      continue;
    }
    if (voice.paused) {
      ++i;
      continue;
    }
    const Clip& clip = *voice.clip;
    const uint32_t channels = clip.channels;
    const uint32_t sampleCount = clip.sampleCount();
    if ((channels != 1 && channels != 2) || clip.sampleRate == 0 || sampleCount == 0 ||
        clip.pcm.size() < static_cast<size_t>(sampleCount) * channels) {
      voices_[i] = voices_.back();
      voices_.pop_back();
      continue;
    }

    const Vec3 toSource = voice.position3d - listener_.position;
    const float distance = math::length(toSource);
    float attenuation = 1.0f;
    const float minDistance = std::isfinite(voice.minDistance)
                                  ? std::max(0.0f, voice.minDistance) : 1.0f;
    const float maxDistance = std::isfinite(voice.maxDistance)
                                  ? std::max(minDistance, voice.maxDistance) : 50.0f;
    if (distance > minDistance) {
      const float range = maxDistance - minDistance;
      attenuation = range > 1.0e-6f
                        ? std::clamp(1.0f - (distance - minDistance) / range, 0.0f, 1.0f)
                        : 0.0f;
    }

    // Equal-power stereo panning from signed projection on listener right vector.
    float leftGain = 1.0f;
    float rightGain = 1.0f;
    if (distance > 1e-4f) {
      const float pan = math::clamp(math::dot(toSource * (1.0f / distance), right), -1.0f, 1.0f);
      const float angle = (pan + 1.0f) * (math::kPi * 0.25f);
      leftGain = std::cos(angle) * 1.41421356f;
      rightGain = std::sin(angle) * 1.41421356f;
    }

    const double sourceStep = static_cast<double>(clip.sampleRate) /
                              static_cast<double>(sampleRate_) * voice.pitch;
    for (uint32_t frame = 0; frame < frames; ++frame) {
      if (!voice.playing || voice.paused) break;
      if (voice.position >= sampleCount) {
        if (voice.loop) {
          voice.position = std::fmod(voice.position, static_cast<double>(sampleCount));
        } else {
          voice.playing = false;
          break;
        }
      }
      const uint32_t index0 = static_cast<uint32_t>(voice.position);
      const uint32_t index1 = index0 + 1u < sampleCount ? index0 + 1u
                                                          : (voice.loop ? 0u : index0);
      const float alpha = static_cast<float>(voice.position - index0);
      const auto interpolate = [&](uint32_t channel) {
        const float a = static_cast<float>(clip.pcm[static_cast<size_t>(index0) * channels + channel]);
        const float b = static_cast<float>(clip.pcm[static_cast<size_t>(index1) * channels + channel]);
        return (a + (b - a) * alpha) / 32768.0f;
      };
      const float leftSample = interpolate(0);
      const float rightSample = channels == 2 ? interpolate(1) : leftSample;
      mixBuffer_[static_cast<size_t>(frame) * 2] +=
          leftSample * voice.volume * attenuation * leftGain;
      mixBuffer_[static_cast<size_t>(frame) * 2 + 1] +=
          rightSample * voice.volume * attenuation * rightGain;
      voice.position += sourceStep;
    }
    if (!voice.playing) {
      voices_[i] = voices_.back();
      voices_.pop_back();
      continue;
    }
    ++i;
  }
  for (size_t i = 0; i < static_cast<size_t>(frames) * 2; ++i)
    mixBuffer_[i] = std::clamp(mixBuffer_[i], -1.0f, 1.0f);
  if (backend_ && backend_->valid()) backend_->submit(mixBuffer_.data(), frames);
}

Clip Mixer::generateTone(float frequency, float duration, uint32_t sampleRate) {
  Clip clip;
  clip.format = Format::Mono16;
  clip.sampleRate = sampleRate;
  clip.channels = 1;
  if (!std::isfinite(frequency) || !std::isfinite(duration) || frequency <= 0.0f ||
      duration <= 0.0f || sampleRate == 0) return clip;
  const double requestedSamples = static_cast<double>(duration) * sampleRate;
  if (!std::isfinite(requestedSamples) ||
      requestedSamples > std::numeric_limits<uint32_t>::max()) return clip;
  const uint32_t samples = static_cast<uint32_t>(requestedSamples);
  if (samples == 0) return clip;
  clip.pcm.resize(samples);
  for (uint32_t i = 0; i < samples; ++i) {
    const float t = static_cast<float>(i) / sampleRate;
    clip.pcm[i] = static_cast<int16_t>(std::sin(t * frequency * math::kPi * 2.0f) * 12000.0f);
  }
  return clip;
}

Clip Mixer::loadWavFromMemory(const uint8_t* data, size_t size, const std::string& name) {
  Clip clip;
  clip.name = name;
  if (!data || size < 44u) return clip;
  if (std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) return clip;

  bool hasFmt = false;
  uint16_t audioFormat = 0;
  uint16_t channels = 0;
  uint32_t sampleRate = 0;
  uint16_t bitsPerSample = 0;
  const uint8_t* pcmPtr = nullptr;
  uint32_t pcmBytes = 0;

  size_t offset = 12;
  while (offset + 8u <= size) {
    const uint32_t chunkSize = readU32Le(data + offset + 4);
    const size_t payload = offset + 8u;
    if (chunkSize > size - payload) break;
    if (std::memcmp(data + offset, "fmt ", 4) == 0 && chunkSize >= 16u) {
      audioFormat = readU16Le(data + payload);
      channels = readU16Le(data + payload + 2);
      sampleRate = readU32Le(data + payload + 4);
      bitsPerSample = readU16Le(data + payload + 14);
      hasFmt = true;
    } else if (std::memcmp(data + offset, "data", 4) == 0) {
      pcmPtr = data + payload;
      pcmBytes = chunkSize;
      break;
    }
    offset = payload + chunkSize + (chunkSize & 1u);
  }

  if (!hasFmt || !pcmPtr || pcmBytes == 0 || audioFormat != 1 ||
      (channels != 1 && channels != 2) || sampleRate == 0 ||
      (bitsPerSample != 8 && bitsPerSample != 16)) {
    return clip;
  }

  clip.channels = channels;
  clip.sampleRate = sampleRate;
  clip.format = channels == 2 ? Format::Stereo16 : Format::Mono16;
  if (bitsPerSample == 16) {
    const size_t sampleCount = pcmBytes / 2u;
    clip.pcm.resize(sampleCount);
    for (size_t i = 0; i < sampleCount; ++i) {
      clip.pcm[i] = static_cast<int16_t>(readU16Le(pcmPtr + i * 2u));
    }
  } else {
    clip.pcm.resize(pcmBytes);
    for (size_t i = 0; i < pcmBytes; ++i) {
      const int32_t s = (static_cast<int32_t>(pcmPtr[i]) - 128) * 256;
      clip.pcm[i] = static_cast<int16_t>(std::clamp(s, -32768, 32767));
    }
  }
  return clip;
}

Clip Mixer::loadWav(const std::string& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input.is_open()) return {};
  const std::streamsize len = input.tellg();
  if (len <= 0 || len > 128 * 1024 * 1024) return {};
  input.seekg(0, std::ios::beg);
  std::vector<uint8_t> bytes(static_cast<size_t>(len));
  if (!input.read(reinterpret_cast<char*>(bytes.data()), len)) return {};
  return loadWavFromMemory(bytes.data(), bytes.size(), path);
}

}  // namespace nimbus::audio
