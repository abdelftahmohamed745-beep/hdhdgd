// OpenSL ES backend for Android.
#include "nimbus/audio.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#ifdef __ANDROID__
#include <array>
#include <deque>
#include <mutex>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#endif

#include "nimbus/log.hpp"

namespace nimbus::audio {

class OpenSlesBackend final : public Backend {
 public:
  ~OpenSlesBackend() override { close(); }

  bool open(uint32_t sampleRate, uint32_t bufferFrames) override {
#ifdef __ANDROID__
    close();
    if (sampleRate == 0 || bufferFrames == 0 ||
        sampleRate > std::numeric_limits<SLuint32>::max() / 1000u ||
        static_cast<size_t>(bufferFrames) >
            std::numeric_limits<size_t>::max() / (2u * sizeof(int16_t))) {
      log::write(log::Level::Error, "audio: invalid OpenSL ES format or buffer size");
      return false;
    }

    auto fail = [this](const char* operation, SLresult result) {
      log::write(log::Level::Error,
                 std::string("audio: OpenSL ES ") + operation +
                     " failed (SLResult=" + std::to_string(result) + ")");
      close();
      return false;
    };

    SLresult result = slCreateEngine(&engineObject_, 0, nullptr, 0, nullptr, nullptr);
    if (result != SL_RESULT_SUCCESS) return fail("slCreateEngine", result);
    result = (*engineObject_)->Realize(engineObject_, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) return fail("engine Realize", result);
    result = (*engineObject_)->GetInterface(engineObject_, SL_IID_ENGINE, &engine_);
    if (result != SL_RESULT_SUCCESS) return fail("engine GetInterface", result);

    result = (*engine_)->CreateOutputMix(engine_, &outputMix_, 0, nullptr, nullptr);
    if (result != SL_RESULT_SUCCESS) return fail("CreateOutputMix", result);
    result = (*outputMix_)->Realize(outputMix_, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) return fail("output mix Realize", result);

    SLDataLocator_AndroidSimpleBufferQueue locator{};
    locator.locatorType = SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE;
    locator.numBuffers = static_cast<SLuint32>(buffers_.size());

    SLDataFormat_PCM format{};
    format.formatType = SL_DATAFORMAT_PCM;
    format.numChannels = 2;
    format.samplesPerSec = static_cast<SLuint32>(sampleRate * 1000u);
    format.bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16;
    format.containerSize = 16;
    format.channelMask = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
    format.endianness = SL_BYTEORDER_LITTLEENDIAN;

    SLDataSource source{&locator, &format};
    SLDataLocator_OutputMix output{};
    output.locatorType = SL_DATALOCATOR_OUTPUTMIX;
    output.outputMix = outputMix_;
    SLDataSink sink{&output, nullptr};

    const SLInterfaceID ids[] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE};
    const SLboolean required[] = {SL_BOOLEAN_TRUE};
    result = (*engine_)->CreateAudioPlayer(engine_, &player_, &source, &sink,
                                            1, ids, required);
    if (result != SL_RESULT_SUCCESS) return fail("CreateAudioPlayer", result);
    result = (*player_)->Realize(player_, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) return fail("player Realize", result);
    result = (*player_)->GetInterface(player_, SL_IID_PLAY, &play_);
    if (result != SL_RESULT_SUCCESS) return fail("play GetInterface", result);
    result = (*player_)->GetInterface(player_, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &queue_);
    if (result != SL_RESULT_SUCCESS) return fail("buffer queue GetInterface", result);

    const size_t samples = static_cast<size_t>(bufferFrames) * 2u;
    for (auto& buffer : buffers_) buffer.assign(samples, 0);
    {
      std::lock_guard<std::mutex> lock(queueMutex_);
      queuedSlots_.clear();
      slotInUse_.fill(false);
    }
    bufferFrames_ = bufferFrames;

    result = (*queue_)->RegisterCallback(queue_, &OpenSlesBackend::onBufferFinished, this);
    if (result != SL_RESULT_SUCCESS) return fail("RegisterCallback", result);
    result = (*play_)->SetPlayState(play_, SL_PLAYSTATE_PLAYING);
    if (result != SL_RESULT_SUCCESS) return fail("SetPlayState", result);

    valid_ = true;
    return true;
#else
    (void)sampleRate;
    (void)bufferFrames;
    log::write(log::Level::Warning, "audio: OpenSL ES backend unavailable on this host");
    return false;
#endif
  }

  void close() override {
#ifdef __ANDROID__
    valid_ = false;
    // Stop and clear before destroying the player. The callback only touches the
    // queue bookkeeping guarded by queueMutex_; it never accesses audio buffers.
    if (play_) (*play_)->SetPlayState(play_, SL_PLAYSTATE_STOPPED);
    if (queue_) (*queue_)->Clear(queue_);
    if (player_) (*player_)->Destroy(player_);
    player_ = nullptr;
    play_ = nullptr;
    queue_ = nullptr;
    if (outputMix_) (*outputMix_)->Destroy(outputMix_);
    outputMix_ = nullptr;
    if (engineObject_) (*engineObject_)->Destroy(engineObject_);
    engineObject_ = nullptr;
    engine_ = nullptr;
    {
      std::lock_guard<std::mutex> lock(queueMutex_);
      queuedSlots_.clear();
      slotInUse_.fill(false);
    }
    for (auto& buffer : buffers_) buffer.clear();
    bufferFrames_ = 0;
#endif
  }

  void submit(const float* interleavedStereo, uint32_t frames) override {
#ifdef __ANDROID__
    if (!valid_ || !queue_ || !interleavedStereo || frames == 0) return;
    frames = std::min(frames, bufferFrames_);
    if (frames == 0) return;

    std::lock_guard<std::mutex> lock(queueMutex_);
    size_t slot = buffers_.size();
    for (size_t i = 0; i < slotInUse_.size(); ++i) {
      if (!slotInUse_[i]) {
        slot = i;
        break;
      }
    }
    // The device is allowed to fall behind. Dropping this block is safer than
    // overwriting a buffer that OpenSL ES may still be consuming.
    if (slot == buffers_.size()) return;

    auto& buffer = buffers_[slot];
    const size_t sampleCount = static_cast<size_t>(frames) * 2u;
    for (size_t i = 0; i < sampleCount; ++i) {
      const float sample = std::isfinite(interleavedStereo[i])
                               ? std::clamp(interleavedStereo[i], -1.0f, 1.0f)
                               : 0.0f;
      const float scaled = sample < 0.0f ? sample * 32768.0f : sample * 32767.0f;
      buffer[i] = static_cast<int16_t>(std::clamp(scaled, -32768.0f, 32767.0f));
    }

    slotInUse_[slot] = true;
    queuedSlots_.push_back(slot);
    const SLresult result = (*queue_)->Enqueue(
        queue_, buffer.data(), static_cast<SLuint32>(sampleCount * sizeof(int16_t)));
    if (result != SL_RESULT_SUCCESS) {
      // The callback can only retire buffers successfully queued by OpenSL ES.
      if (!queuedSlots_.empty() && queuedSlots_.back() == slot) queuedSlots_.pop_back();
      slotInUse_[slot] = false;
      log::write(log::Level::Warning, "audio: OpenSL ES queue rejected an audio block");
    }
#else
    (void)interleavedStereo;
    (void)frames;
#endif
  }

  bool valid() const override { return valid_; }

  void setPaused(bool paused) override {
#ifdef __ANDROID__
    if (play_) {
      (*play_)->SetPlayState(play_, paused ? SL_PLAYSTATE_PAUSED : SL_PLAYSTATE_PLAYING);
    }
#else
    (void)paused;
#endif
  }

 private:
#ifdef __ANDROID__
  static void onBufferFinished(SLAndroidSimpleBufferQueueItf, void* context) {
    auto* self = static_cast<OpenSlesBackend*>(context);
    if (!self) return;
    std::lock_guard<std::mutex> lock(self->queueMutex_);
    if (!self->queuedSlots_.empty()) {
      const size_t slot = self->queuedSlots_.front();
      self->queuedSlots_.pop_front();
      if (slot < self->slotInUse_.size()) self->slotInUse_[slot] = false;
    }
  }

  SLEngineItf engine_ = nullptr;
  SLObjectItf engineObject_ = nullptr;
  SLObjectItf outputMix_ = nullptr;
  SLObjectItf player_ = nullptr;
  SLPlayItf play_ = nullptr;
  SLAndroidSimpleBufferQueueItf queue_ = nullptr;
  std::array<std::vector<int16_t>, 2> buffers_;
  std::array<bool, 2> slotInUse_{};
  std::deque<size_t> queuedSlots_;
  std::mutex queueMutex_;
  uint32_t bufferFrames_ = 0;
#endif
  bool valid_ = false;
};

Backend* createOpenSlesBackend() { return new OpenSlesBackend(); }

}  // namespace nimbus::audio
