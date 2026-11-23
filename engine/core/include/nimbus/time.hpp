// Nimbus timing primitives shared by the runtime and rendering backends.
#pragma once

#include <algorithm>
#include <cstdint>

namespace nimbus::time {

struct FrameStats {
  uint32_t drawCalls = 0;
  uint32_t triangles = 0;
  float frameTimeMs = 0.0f;
  float cpuTimeMs = 0.0f;
  float gpuTimeMs = 0.0f;
  float framesPerSecond = 0.0f;
  float fps = 0.0f;  // Compatibility field consumed by the MCP diagnostics response.
  uint64_t memoryBytes = 0;
};

class FixedStep {
 public:
  explicit FixedStep(double stepSeconds = 1.0 / 60.0, uint32_t maxSteps = 8)
      : stepSeconds_(stepSeconds > 0.0 ? stepSeconds : 1.0 / 60.0),
        maxSteps_(std::max(1u, maxSteps)) {}

  // Adds wall-clock time, clamps spikes, and returns the number of simulation
  // steps now available. Call consumeStep() after each executed fixed update.
  uint32_t advance(double elapsedSeconds) {
    if (elapsedSeconds > 0.0) accumulator_ += std::min(elapsedSeconds, stepSeconds_ * maxSteps_);
    return static_cast<uint32_t>(std::min<double>(accumulator_ / stepSeconds_, maxSteps_));
  }
  bool consumeStep() {
    if (accumulator_ + 1.0e-12 < stepSeconds_) return false;
    accumulator_ -= stepSeconds_;
    return true;
  }
  void reset() { accumulator_ = 0.0; }
  double stepSeconds() const { return stepSeconds_; }
  double interpolationAlpha() const { return std::clamp(accumulator_ / stepSeconds_, 0.0, 1.0); }
  double accumulator() const { return accumulator_; }

 private:
  double stepSeconds_;
  uint32_t maxSteps_;
  double accumulator_ = 0.0;
};

}  // namespace nimbus::time
