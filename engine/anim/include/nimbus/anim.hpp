// Nimbus animation: skeletons, clips, blending, events, root motion, state machines, IK.
#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "nimbus/ecs.hpp"
#include "nimbus/math.hpp"

namespace nimbus::anim {

using math::Quat;
using math::Vec3;

struct Joint {
  std::string name;
  int32_t parent = -1;
  Vec3 localPosition{0, 0, 0};
  Quat localRotation{0, 0, 0, 1};
  Vec3 localScale{1, 1, 1};
};

struct Skeleton {
  std::vector<Joint> joints;
  std::vector<math::Mat4> inverseBindMatrices;
  int32_t jointIndex(const std::string& name) const {
    for (size_t i = 0; i < joints.size(); ++i)
      if (joints[i].name == name) return static_cast<int32_t>(i);
    return -1;
  }
};

struct Keyframe {
  float time = 0.0f;
  Vec3 position{0, 0, 0};
  Quat rotation{0, 0, 0, 1};
  Vec3 scale{1, 1, 1};
};

struct Track {
  int32_t joint = -1;
  std::vector<Keyframe> keys;
};

struct AnimationEvent {
  float time = 0.0f;
  std::string name;
  std::string payload;
};

struct Clip {
  std::string name;
  float duration = 0.0f;
  float framesPerSecond = 30.0f;
  std::vector<Track> tracks;
  std::vector<AnimationEvent> events;
  bool loop = true;
};

struct AnimationState {
  const Clip* clip = nullptr;
  float time = 0.0f;
  float speed = 1.0f;
  float weight = 1.0f;
  bool playing = false;
  int32_t maskStartJoint = -1;  // -1 = full body
};

struct Animator {
  std::vector<AnimationState> states;
  std::vector<math::Mat4> globalPoses;
  std::vector<math::Mat4> skinMatrices;
  uint32_t activeState = 0;

  bool setSkeleton(const Skeleton& skeleton);
  const Skeleton* skeleton() const { return skeleton_; }

  void play(uint32_t stateIndex);
  void update(float deltaSeconds);
  void crossfade(uint32_t nextState, float duration);

  void setEventCallback(std::function<void(const AnimationEvent&)> callback) {
    onEvent_ = std::move(callback);
  }
  const Vec3& rootMotionDelta() const { return rootMotionDelta_; }

 private:
  const Skeleton* skeleton_ = nullptr;
  std::vector<uint32_t> evaluationOrder_;
  std::function<void(const AnimationEvent&)> onEvent_;
  Vec3 lastRootPosition_{0, 0, 0};
  Vec3 rootMotionDelta_{0, 0, 0};
  bool hasLastRoot_ = false;
  bool fading_ = false;
  float fadeDuration_ = 0.0f;
  float fadeElapsed_ = 0.0f;
  uint32_t fadeTarget_ = 0;
};

// State machine driving Animator crossfade transitions from typed parameters.
class StateMachine {
 public:
  enum class Compare : uint8_t { Greater, Less, BoolTrue, BoolFalse, Trigger };

  struct Condition {
    std::string parameter;
    Compare op = Compare::BoolTrue;
    float threshold = 0.0f;
  };

  struct Transition {
    uint32_t fromState = 0;
    uint32_t toState = 0;
    float fadeDuration = 0.15f;
    bool hasExitTime = false;
    float exitTime = 1.0f;
    std::vector<Condition> conditions;
  };

  void setFloat(const std::string& name, float value) { floats_[name] = value; }
  void setBool(const std::string& name, bool value) { bools_[name] = value; }
  void setTrigger(const std::string& name) { triggers_[name] = true; }
  float getFloat(const std::string& name) const {
    auto it = floats_.find(name);
    return it != floats_.end() ? it->second : 0.0f;
  }
  bool getBool(const std::string& name) const {
    auto it = bools_.find(name);
    return it != bools_.end() ? it->second : false;
  }

  void addTransition(Transition transition) { transitions_.push_back(std::move(transition)); }
  void clearTransitions() { transitions_.clear(); }
  uint32_t transitionCount() const { return static_cast<uint32_t>(transitions_.size()); }

  // Evaluates transitions against current Animator state & triggers crossfades.
  void evaluate(Animator& animator);

 private:
  std::unordered_map<std::string, float> floats_;
  std::unordered_map<std::string, bool> bools_;
  std::unordered_map<std::string, bool> triggers_;
  std::vector<Transition> transitions_;
};

// Two-bone analytic IK for arms and legs.
bool solveTwoBoneIK(const Vec3& root, const Vec3& target, float lengthA, float lengthB,
                    Vec3& outMid, Vec3& outEnd);

}  // namespace nimbus::anim
