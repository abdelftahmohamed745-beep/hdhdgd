#include "nimbus/anim.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

namespace nimbus::anim {
namespace {

bool finite(const Vec3& value) {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finite(const Quat& value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z) && std::isfinite(value.w);
}

bool finite(const Keyframe& key) {
  return std::isfinite(key.time) && finite(key.position) &&
         finite(key.rotation) && finite(key.scale);
}

struct PoseTransform {
  Vec3 position{0, 0, 0};
  Quat rotation{0, 0, 0, 1};
  Vec3 scale{1, 1, 1};
};

bool sampleTrack(const Track& track, float time, Keyframe& result) {
  if (track.keys.empty()) return false;
  for (size_t i = 0; i < track.keys.size(); ++i) {
    if (!finite(track.keys[i]) || (i > 0 && track.keys[i].time < track.keys[i - 1].time))
      return false;
  }
  if (track.keys.size() == 1 || time <= track.keys.front().time) {
    result = track.keys.front();
    return true;
  }
  if (time >= track.keys.back().time) {
    result = track.keys.back();
    return true;
  }

  const auto upper = std::upper_bound(
      track.keys.begin(), track.keys.end(), time,
      [](float value, const Keyframe& key) { return value < key.time; });
  const Keyframe& b = *upper;
  const Keyframe& a = *(upper - 1);
  const float span = b.time - a.time;
  const float t = span > 1.0e-8f ? math::saturate((time - a.time) / span) : 1.0f;
  result.time = time;
  result.position = math::lerp(a.position, b.position, t);
  result.rotation = math::quatSlerp(a.rotation, b.rotation, t);
  result.scale = math::lerp(a.scale, b.scale, t);
  return true;
}

bool jointInSubtree(const Skeleton* skeleton, size_t jointIdx, int32_t rootMaskJoint) {
  if (rootMaskJoint < 0 || !skeleton) return true;
  int32_t cur = static_cast<int32_t>(jointIdx);
  while (cur >= 0) {
    if (cur == rootMaskJoint) return true;
    cur = skeleton->joints[static_cast<size_t>(cur)].parent;
  }
  return false;
}

void dispatchClipEvents(const Clip& clip, float prevTime, float curTime, bool looped,
                        const std::function<void(const AnimationEvent&)>& cb) {
  if (!cb || clip.events.empty()) return;
  for (const AnimationEvent& ev : clip.events) {
    bool hit = false;
    if (!looped) {
      hit = (ev.time > prevTime && ev.time <= curTime) ||
            (prevTime == 0.0f && curTime > 0.0f && ev.time == 0.0f);
    } else {
      hit = (ev.time > prevTime && ev.time <= clip.duration) ||
            (ev.time >= 0.0f && ev.time <= curTime);
    }
    if (hit) cb(ev);
  }
}

}  // namespace

bool Animator::setSkeleton(const Skeleton& skeleton) {
  skeleton_ = nullptr;
  evaluationOrder_.clear();
  globalPoses.clear();
  skinMatrices.clear();
  hasLastRoot_ = false;
  rootMotionDelta_ = {0, 0, 0};
  const size_t count = skeleton.joints.size();
  if (!skeleton.inverseBindMatrices.empty() && skeleton.inverseBindMatrices.size() < count)
    return false;

  std::vector<uint8_t> marks(count, 0);
  evaluationOrder_.reserve(count);
  std::function<bool(uint32_t)> visit = [&](uint32_t index) {
    if (marks[index] == 2) return true;
    if (marks[index] == 1) return false;
    marks[index] = 1;
    const int32_t parent = skeleton.joints[index].parent;
    if (parent < -1 || parent >= static_cast<int32_t>(count) ||
        parent == static_cast<int32_t>(index)) return false;
    if (parent >= 0 && !visit(static_cast<uint32_t>(parent))) return false;
    marks[index] = 2;
    evaluationOrder_.push_back(index);
    return true;
  };
  for (uint32_t index = 0; index < count; ++index) {
    if (!visit(index)) {
      evaluationOrder_.clear();
      return false;
    }
  }

  skeleton_ = &skeleton;
  globalPoses.resize(count, math::mat4Identity());
  skinMatrices.resize(count, math::mat4Identity());
  return true;
}

void Animator::play(uint32_t stateIndex) {
  if (stateIndex >= states.size()) return;
  for (uint32_t i = 0; i < states.size(); ++i) {
    states[i].playing = i == stateIndex;
    states[i].weight = i == stateIndex ? 1.0f : 0.0f;
  }
  activeState = stateIndex;
  states[stateIndex].time = 0.0f;
  fading_ = false;
  hasLastRoot_ = false;
  rootMotionDelta_ = {0, 0, 0};
}

void Animator::crossfade(uint32_t nextState, float duration) {
  if (nextState >= states.size() || !std::isfinite(duration)) return;
  if (nextState == activeState) return;
  states[nextState].weight = 0.0f;
  states[nextState].playing = true;
  states[nextState].time = 0.0f;
  fadeDuration_ = std::max(0.0f, duration);
  fadeTarget_ = nextState;
  fadeElapsed_ = 0.0f;
  if (fadeDuration_ == 0.0f) {
    if (activeState < states.size()) {
      states[activeState].playing = false;
      states[activeState].weight = 0.0f;
    }
    states[nextState].weight = 1.0f;
    activeState = nextState;
    fading_ = false;
  } else {
    fading_ = true;
  }
}

void Animator::update(float deltaSeconds) {
  rootMotionDelta_ = {0, 0, 0};
  if (!skeleton_ || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0f) return;

  for (AnimationState& state : states) {
    if (!state.playing || !state.clip || !std::isfinite(state.speed) ||
        !std::isfinite(state.clip->duration) || state.clip->duration <= 0.0f) continue;
    const float prevTime = state.time;
    state.time += deltaSeconds * state.speed;
    const float duration = state.clip->duration;
    bool looped = false;
    if (state.clip->loop) {
      if (state.time >= duration) {
        looped = true;
        hasLastRoot_ = false;
      }
      state.time = std::fmod(state.time, duration);
      if (state.time < 0.0f) state.time += duration;
    } else {
      state.time = math::clamp(state.time, 0.0f, duration);
      if ((state.speed >= 0.0f && state.time >= duration) ||
          (state.speed < 0.0f && state.time <= 0.0f)) state.playing = false;
    }
    dispatchClipEvents(*state.clip, prevTime, state.time, looped, onEvent_);
  }

  if (fading_ && fadeTarget_ < states.size() && activeState < states.size()) {
    fadeElapsed_ += deltaSeconds;
    const float t = fadeDuration_ > 0.0f
        ? math::saturate(fadeElapsed_ / fadeDuration_) : 1.0f;
    states[fadeTarget_].weight = t;
    states[activeState].weight = 1.0f - t;
    if (t >= 1.0f) {
      states[activeState].playing = false;
      states[activeState].weight = 0.0f;
      activeState = fadeTarget_;
      fading_ = false;
    }
  }

  struct ActiveTrackSet {
    const AnimationState* state = nullptr;
    std::vector<const Track*> tracks;
    float weight = 0.0f;
  };
  std::vector<ActiveTrackSet> active;
  active.reserve(states.size());
  for (const AnimationState& state : states) {
    if (!state.playing || !state.clip || !std::isfinite(state.weight) || state.weight <= 0.0f)
      continue;
    ActiveTrackSet set;
    set.state = &state;
    set.weight = math::saturate(state.weight);
    set.tracks.resize(skeleton_->joints.size(), nullptr);
    for (const Track& track : state.clip->tracks) {
      if (track.joint < 0 || static_cast<size_t>(track.joint) >= set.tracks.size() ||
          track.keys.empty()) continue;
      if (!jointInSubtree(skeleton_, static_cast<size_t>(track.joint), state.maskStartJoint))
        continue;
      set.tracks[static_cast<size_t>(track.joint)] = &track;
    }
    active.push_back(std::move(set));
  }

  std::vector<PoseTransform> localPose(skeleton_->joints.size());
  for (size_t joint = 0; joint < skeleton_->joints.size(); ++joint) {
    const Joint& rest = skeleton_->joints[joint];
    Keyframe base{0.0f, rest.localPosition, rest.localRotation, rest.localScale};
    float totalWeight = 0.0f;
    for (const ActiveTrackSet& set : active) {
      const Track* track = set.tracks[joint];
      Keyframe sample;
      if (track && sampleTrack(*track, set.state->time, sample)) totalWeight += set.weight;
    }
    const float normalization = totalWeight > 1.0f ? 1.0f / totalWeight : 1.0f;
    const float restWeight = totalWeight < 1.0f ? 1.0f - totalWeight : 0.0f;
    Vec3 position = base.position * restWeight;
    Vec3 scale = base.scale * restWeight;
    Quat rotation = base.rotation;
    float rotationWeight = restWeight;
    bool rotationInitialized = restWeight > 0.0f;

    for (const ActiveTrackSet& set : active) {
      const Track* track = set.tracks[joint];
      Keyframe sample;
      if (!track || !sampleTrack(*track, set.state->time, sample)) continue;
      const float weight = set.weight * normalization;
      position += sample.position * weight;
      scale += sample.scale * weight;
      if (!rotationInitialized) {
        rotation = math::quatNormalize(sample.rotation);
        rotationWeight = weight;
        rotationInitialized = true;
      } else {
        const float blend = weight / std::max(rotationWeight + weight, 1.0e-8f);
        rotation = math::quatSlerp(rotation, sample.rotation, blend);
        rotationWeight += weight;
      }
    }
    localPose[joint] = {position, math::quatNormalize(rotation), scale};
  }

  if (!localPose.empty()) {
    const Vec3 currentRoot = localPose[0].position;
    if (hasLastRoot_) rootMotionDelta_ = currentRoot - lastRootPosition_;
    lastRootPosition_ = currentRoot;
    hasLastRoot_ = true;
  }

  for (uint32_t index : evaluationOrder_) {
    const Joint& joint = skeleton_->joints[index];
    const PoseTransform& local = localPose[index];
    const math::Mat4 localMatrix =
        math::mat4FromTrs(local.position, local.rotation, local.scale);
    globalPoses[index] = joint.parent >= 0
        ? math::mat4Multiply(globalPoses[static_cast<size_t>(joint.parent)], localMatrix)
        : localMatrix;
  }
  for (size_t index = 0; index < skeleton_->joints.size(); ++index) {
    skinMatrices[index] = skeleton_->inverseBindMatrices.empty()
        ? globalPoses[index]
        : math::mat4Multiply(globalPoses[index], skeleton_->inverseBindMatrices[index]);
  }
}

void StateMachine::evaluate(Animator& animator) {
  if (animator.states.empty() || animator.activeState >= animator.states.size()) return;
  const uint32_t current = animator.activeState;
  const AnimationState& state = animator.states[current];
  const float normalizedTime =
      (state.clip && state.clip->duration > 0.0f) ? (state.time / state.clip->duration) : 1.0f;

  for (const Transition& t : transitions_) {
    if (t.fromState != current || t.toState >= animator.states.size()) continue;
    if (t.hasExitTime && normalizedTime < t.exitTime) continue;

    bool allMet = true;
    std::vector<std::string> consumedTriggers;
    for (const Condition& c : t.conditions) {
      switch (c.op) {
        case Compare::Greater:
          if (!(getFloat(c.parameter) > c.threshold)) allMet = false;
          break;
        case Compare::Less:
          if (!(getFloat(c.parameter) < c.threshold)) allMet = false;
          break;
        case Compare::BoolTrue:
          if (!getBool(c.parameter)) allMet = false;
          break;
        case Compare::BoolFalse:
          if (getBool(c.parameter)) allMet = false;
          break;
        case Compare::Trigger: {
          auto it = triggers_.find(c.parameter);
          if (it == triggers_.end() || !it->second) allMet = false;
          else consumedTriggers.push_back(c.parameter);
          break;
        }
      }
      if (!allMet) break;
    }
    if (allMet) {
      for (const std::string& trig : consumedTriggers) triggers_[trig] = false;
      animator.crossfade(t.toState, t.fadeDuration);
      return;
    }
  }
}

bool solveTwoBoneIK(const Vec3& root, const Vec3& target, float lengthA, float lengthB,
                    Vec3& outMid, Vec3& outEnd) {
  if (!finite(root) || !finite(target) || !std::isfinite(lengthA) ||
      !std::isfinite(lengthB) || lengthA <= 0.0f || lengthB <= 0.0f) return false;
  const Vec3 toTarget = target - root;
  float distance = math::length(toTarget);
  const float maxLength = lengthA + lengthB;
  if (distance > maxLength) distance = maxLength;
  if (distance < 1e-5f) return false;
  const Vec3 direction = toTarget * (1.0f / distance);
  const float cosA = (lengthA * lengthA + distance * distance - lengthB * lengthB) /
                     (2.0f * lengthA * distance);
  if (cosA < -1.0f || cosA > 1.0f) return false;
  const float sinA = std::sqrt(std::max(0.0f, 1.0f - cosA * cosA));
  Vec3 side = math::cross(direction, Vec3{0, 1, 0});
  if (math::length(side) < 1e-5f) side = math::cross(direction, Vec3{1, 0, 0});
  side = math::normalize(side);
  const Vec3 bend = side * sinA;
  outMid = root + direction * (cosA * lengthA) + bend * lengthA;
  outEnd = root + direction * distance;
  return true;
}

}  // namespace nimbus::anim
