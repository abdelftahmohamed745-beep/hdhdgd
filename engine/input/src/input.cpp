#include "nimbus/input.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace nimbus::input {

InputSystem::InputSystem() {
  // Default cross-platform FPS/third-person & editor-friendly bindings.
  bindAxis("move_x", {Key::D, Key::A, GamepadAxis::LeftX, 1.0f});
  bindAxis("move_y", {Key::W, Key::S, GamepadAxis::LeftY, 1.0f});
  bindAxis("look_x", {Key::Right, Key::Left, GamepadAxis::RightX, 1.0f});
  bindAxis("look_y", {Key::Up, Key::Down, GamepadAxis::RightY, 1.0f});
  bindAction("jump", {Key::Space, MouseButton::Count, GamepadButton::South});
  bindAction("fire", {Key::Unknown, MouseButton::Left, GamepadButton::RightShoulder});
  bindAction("interact", {Key::E, MouseButton::Count, GamepadButton::West});
  bindAction("sprint", {Key::Shift, MouseButton::Count, GamepadButton::LeftThumb});
  bindAction("pause", {Key::Escape, MouseButton::Count, GamepadButton::Start});
}

void InputSystem::reset() {
  std::memset(keys_, 0, sizeof(keys_));
  std::memset(prevKeys_, 0, sizeof(prevKeys_));
  std::memset(mouse_, 0, sizeof(mouse_));
  std::memset(prevMouse_, 0, sizeof(prevMouse_));
  pointerPos_ = {};
  pointerDelta_ = {};
  wheelDelta_ = {};
  hasPointer_ = false;
  touches_.clear();
  gestures_ = {};
  prevPinchDistance_ = 0.0f;
  currentTime_ = 0.0;
  lastTapTime_ = -10.0;
  for (auto& pad : gamepads_) pad = GamepadState{};
}

void InputSystem::beginFrame(double timeSeconds) {
  if (std::isfinite(timeSeconds) && timeSeconds >= currentTime_) currentTime_ = timeSeconds;
  updateGestures();
}

void InputSystem::endFrame() {
  std::memcpy(prevKeys_, keys_, sizeof(keys_));
  std::memcpy(prevMouse_, mouse_, sizeof(mouse_));
  for (auto& pad : gamepads_) std::memcpy(pad.prevButtons, pad.buttons, sizeof(pad.buttons));
  pointerDelta_ = {};
  wheelDelta_ = {};
  gestures_.tap = false;
  gestures_.doubleTap = false;
  gestures_.dragDelta = {};
  gestures_.pinchDelta = 0.0f;

  // Remove Ended/Cancelled touch entries after one frame so press/release consumers see them.
  touches_.erase(std::remove_if(touches_.begin(), touches_.end(),
                                [](const TouchPoint& tp) {
                                  return tp.phase == TouchPhase::Ended ||
                                         tp.phase == TouchPhase::Cancelled;
                                }),
                 touches_.end());
  for (TouchPoint& tp : touches_) {
    tp.delta = {};
    if (tp.phase == TouchPhase::Began) tp.phase = TouchPhase::Moved;
  }
}

void InputSystem::feedKey(Key key, bool down) {
  const auto idx = static_cast<size_t>(key);
  if (idx < static_cast<size_t>(Key::Count)) keys_[idx] = down;
}

void InputSystem::feedPointerMove(float x, float y) {
  if (!std::isfinite(x) || !std::isfinite(y)) return;
  const Vec2 next{x, y};
  if (hasPointer_) pointerDelta_ += (next - pointerPos_);
  pointerPos_ = next;
  hasPointer_ = true;
}

void InputSystem::feedMouseButton(MouseButton button, bool down) {
  const auto idx = static_cast<size_t>(button);
  if (idx < static_cast<size_t>(MouseButton::Count)) mouse_[idx] = down;
}

void InputSystem::feedMouseWheel(float deltaX, float deltaY) {
  if (std::isfinite(deltaX)) wheelDelta_.x += deltaX;
  if (std::isfinite(deltaY)) wheelDelta_.y += deltaY;
}

void InputSystem::feedTouch(int32_t pointerId, TouchPhase phase, float x, float y, float pressure) {
  if (pointerId < 0 || !std::isfinite(x) || !std::isfinite(y)) return;
  const float safePressure = std::isfinite(pressure) ? math::saturate(pressure) : 1.0f;
  auto it = std::find_if(touches_.begin(), touches_.end(),
                         [pointerId](const TouchPoint& t) { return t.id == pointerId; });
  if (phase == TouchPhase::Began || it == touches_.end()) {
    TouchPoint tp;
    tp.id = pointerId;
    tp.phase = phase;
    tp.position = {x, y};
    tp.startPosition = {x, y};
    tp.delta = {};
    tp.pressure = safePressure;
    tp.startTime = currentTime_;
    if (it == touches_.end()) touches_.push_back(tp);
    else *it = tp;
  } else {
    it->phase = phase;
    it->delta += Vec2{x, y} - it->position;
    it->position = {x, y};
    it->pressure = safePressure;
    if (phase == TouchPhase::Ended) {
      const float moveDist = math::length(it->position - it->startPosition);
      if (moveDist <= 18.0f) {
        gestures_.tap = true;
        gestures_.tapPosition = it->position;
        if (currentTime_ - lastTapTime_ <= 0.35 &&
            math::length(it->position - lastTapPos_) <= 32.0f) {
          gestures_.doubleTap = true;
        }
        lastTapTime_ = currentTime_;
        lastTapPos_ = it->position;
      }
    }
  }
  updateGestures();
}

void InputSystem::updateGestures() {
  std::vector<const TouchPoint*> active;
  for (const TouchPoint& tp : touches_) {
    if (tp.phase == TouchPhase::Began || tp.phase == TouchPhase::Moved) active.push_back(&tp);
  }
  if (active.size() == 1) {
    gestures_.pinching = false;
    prevPinchDistance_ = 0.0f;
    const float moved = math::length(active[0]->position - active[0]->startPosition);
    if (moved > 8.0f) {
      gestures_.dragging = true;
      gestures_.dragDelta = active[0]->delta;
    }
  } else if (active.size() >= 2) {
    gestures_.dragging = false;
    const float dist = math::length(active[1]->position - active[0]->position);
    if (!gestures_.pinching || prevPinchDistance_ <= 1e-3f) {
      gestures_.pinching = true;
      gestures_.pinchScale = 1.0f;
      gestures_.pinchDelta = 0.0f;
    } else if (dist > 1e-3f) {
      const float ratio = dist / prevPinchDistance_;
      gestures_.pinchDelta = dist - prevPinchDistance_;
      gestures_.pinchScale *= ratio;
    }
    prevPinchDistance_ = dist;
  } else {
    gestures_.dragging = false;
    gestures_.pinching = false;
    prevPinchDistance_ = 0.0f;
  }
}

void InputSystem::feedGamepadConnected(uint32_t index, bool connected) {
  if (index >= kMaxGamepads) return;
  gamepads_[index].connected = connected;
  if (!connected) gamepads_[index] = GamepadState{};
}

void InputSystem::feedGamepadAxis(uint32_t index, GamepadAxis axis, float value) {
  if (index >= kMaxGamepads) return;
  const auto a = static_cast<size_t>(axis);
  if (a < static_cast<size_t>(GamepadAxis::Count) && std::isfinite(value))
    gamepads_[index].axes[a] = math::clamp(value, -1.0f, 1.0f);
}

void InputSystem::feedGamepadButton(uint32_t index, GamepadButton button, bool down) {
  if (index >= kMaxGamepads) return;
  const auto b = static_cast<size_t>(button);
  if (b < static_cast<size_t>(GamepadButton::Count)) gamepads_[index].buttons[b] = down;
}

bool InputSystem::keyDown(Key key) const {
  const auto idx = static_cast<size_t>(key);
  return idx < static_cast<size_t>(Key::Count) && keys_[idx];
}

bool InputSystem::keyPressed(Key key) const {
  const auto idx = static_cast<size_t>(key);
  return idx < static_cast<size_t>(Key::Count) && keys_[idx] && !prevKeys_[idx];
}

bool InputSystem::keyReleased(Key key) const {
  const auto idx = static_cast<size_t>(key);
  return idx < static_cast<size_t>(Key::Count) && !keys_[idx] && prevKeys_[idx];
}

bool InputSystem::mouseDown(MouseButton button) const {
  const auto idx = static_cast<size_t>(button);
  return idx < static_cast<size_t>(MouseButton::Count) && mouse_[idx];
}

bool InputSystem::mousePressed(MouseButton button) const {
  const auto idx = static_cast<size_t>(button);
  return idx < static_cast<size_t>(MouseButton::Count) && mouse_[idx] && !prevMouse_[idx];
}

bool InputSystem::mouseReleased(MouseButton button) const {
  const auto idx = static_cast<size_t>(button);
  return idx < static_cast<size_t>(MouseButton::Count) && !mouse_[idx] && prevMouse_[idx];
}

uint32_t InputSystem::activeTouchCount() const {
  uint32_t count = 0;
  for (const TouchPoint& tp : touches_)
    if (tp.phase == TouchPhase::Began || tp.phase == TouchPhase::Moved) ++count;
  return count;
}

const TouchPoint* InputSystem::findTouch(int32_t id) const {
  for (const TouchPoint& tp : touches_)
    if (tp.id == id) return &tp;
  return nullptr;
}

const GamepadState& InputSystem::gamepad(uint32_t index) const {
  static const GamepadState empty{};
  return index < kMaxGamepads ? gamepads_[index] : empty;
}

float InputSystem::gamepadAxis(uint32_t index, GamepadAxis axis) const {
  if (index >= kMaxGamepads) return 0.0f;
  const auto a = static_cast<size_t>(axis);
  if (a >= static_cast<size_t>(GamepadAxis::Count) || !gamepads_[index].connected) return 0.0f;
  const float raw = gamepads_[index].axes[a];
  return std::fabs(raw) < gamepads_[index].deadzone ? 0.0f : raw;
}

bool InputSystem::gamepadDown(uint32_t index, GamepadButton button) const {
  if (index >= kMaxGamepads || !gamepads_[index].connected) return false;
  const auto b = static_cast<size_t>(button);
  return b < static_cast<size_t>(GamepadButton::Count) && gamepads_[index].buttons[b];
}

bool InputSystem::gamepadPressed(uint32_t index, GamepadButton button) const {
  if (index >= kMaxGamepads || !gamepads_[index].connected) return false;
  const auto b = static_cast<size_t>(button);
  return b < static_cast<size_t>(GamepadButton::Count) &&
         gamepads_[index].buttons[b] && !gamepads_[index].prevButtons[b];
}

void InputSystem::bindAction(const std::string& name, ActionBinding binding) {
  if (!name.empty()) actions_[name].push_back(binding);
}

void InputSystem::rebindAction(const std::string& name, Key newKey) {
  if (name.empty()) return;
  actions_[name].clear();
  actions_[name].push_back({newKey, MouseButton::Count, GamepadButton::Count});
}

void InputSystem::bindAxis(const std::string& name, AxisBinding binding) {
  if (!name.empty()) axes_[name].push_back(binding);
}

void InputSystem::clearBindings() {
  actions_.clear();
  axes_.clear();
}

bool InputSystem::actionDown(const std::string& name) const {
  const auto it = actions_.find(name);
  if (it == actions_.end()) return false;
  for (const ActionBinding& b : it->second) {
    if (b.key != Key::Unknown && keyDown(b.key)) return true;
    if (b.mouseButton != MouseButton::Count && mouseDown(b.mouseButton)) return true;
    if (b.gamepadButton != GamepadButton::Count && gamepadDown(0, b.gamepadButton)) return true;
  }
  return false;
}

bool InputSystem::actionPressed(const std::string& name) const {
  const auto it = actions_.find(name);
  if (it == actions_.end()) return false;
  for (const ActionBinding& b : it->second) {
    if (b.key != Key::Unknown && keyPressed(b.key)) return true;
    if (b.mouseButton != MouseButton::Count && mousePressed(b.mouseButton)) return true;
    if (b.gamepadButton != GamepadButton::Count && gamepadPressed(0, b.gamepadButton)) return true;
  }
  return false;
}

bool InputSystem::actionReleased(const std::string& name) const {
  const auto it = actions_.find(name);
  if (it == actions_.end()) return false;
  for (const ActionBinding& b : it->second) {
    if (b.key != Key::Unknown && keyReleased(b.key)) return true;
    if (b.mouseButton != MouseButton::Count && mouseReleased(b.mouseButton)) return true;
  }
  return false;
}

float InputSystem::axisValue(const std::string& name) const {
  const auto it = axes_.find(name);
  if (it == axes_.end()) return 0.0f;
  float total = 0.0f;
  for (const AxisBinding& b : it->second) {
    if (b.positiveKey != Key::Unknown && keyDown(b.positiveKey)) total += b.scale;
    if (b.negativeKey != Key::Unknown && keyDown(b.negativeKey)) total -= b.scale;
    if (b.gamepadAxis != GamepadAxis::Count)
      total += gamepadAxis(0, b.gamepadAxis) * b.scale;
  }
  return math::clamp(total, -1.0f, 1.0f);
}

}  // namespace nimbus::input
