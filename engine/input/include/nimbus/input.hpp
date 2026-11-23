// Nimbus Input System: keyboard, mouse/pointer, multi-touch, gestures, gamepad, action/axis mapping.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "nimbus/math.hpp"

namespace nimbus::input {

using math::Vec2;

enum class Key : uint16_t {
  Unknown = 0,
  W, A, S, D, Q, E, R, F, Z, X, C, V,
  Space, Shift, Ctrl, Alt, Enter, Escape, Tab, Backspace, Delete,
  Left, Right, Up, Down,
  Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
  Count
};

enum class MouseButton : uint8_t { Left = 0, Right = 1, Middle = 2, Count = 3 };

enum class TouchPhase : uint8_t { Began, Moved, Ended, Cancelled };

struct TouchPoint {
  int32_t id = -1;
  TouchPhase phase = TouchPhase::Ended;
  Vec2 position{0, 0};
  Vec2 startPosition{0, 0};
  Vec2 delta{0, 0};
  float pressure = 1.0f;
  double startTime = 0.0;
};

struct GestureState {
  bool tap = false;
  Vec2 tapPosition{0, 0};
  bool doubleTap = false;
  bool dragging = false;
  Vec2 dragDelta{0, 0};
  bool pinching = false;
  float pinchScale = 1.0f;
  float pinchDelta = 0.0f;
};

enum class GamepadButton : uint8_t {
  South = 0, East, West, North,
  LeftShoulder, RightShoulder, LeftThumb, RightThumb,
  Start, Select, DpadUp, DpadDown, DpadLeft, DpadRight,
  Count
};

enum class GamepadAxis : uint8_t {
  LeftX = 0, LeftY, RightX, RightY, LeftTrigger, RightTrigger, Count
};

struct GamepadState {
  bool connected = false;
  float axes[static_cast<size_t>(GamepadAxis::Count)]{};
  bool buttons[static_cast<size_t>(GamepadButton::Count)]{};
  bool prevButtons[static_cast<size_t>(GamepadButton::Count)]{};
  float deadzone = 0.15f;
};

struct ActionBinding {
  Key key = Key::Unknown;
  MouseButton mouseButton = MouseButton::Count;
  GamepadButton gamepadButton = GamepadButton::Count;
};

struct AxisBinding {
  Key positiveKey = Key::Unknown;
  Key negativeKey = Key::Unknown;
  GamepadAxis gamepadAxis = GamepadAxis::Count;
  float scale = 1.0f;
};

class InputSystem {
 public:
  InputSystem();

  // Frame lifecycle: call endFrame() after all fixed-step updates consume this frame's edges.
  void beginFrame(double timeSeconds = 0.0);
  void endFrame();
  void reset();

  // Platform / JNI / Editor event injection.
  void feedKey(Key key, bool down);
  void feedPointerMove(float x, float y);
  void feedMouseButton(MouseButton button, bool down);
  void feedMouseWheel(float deltaX, float deltaY);
  void feedTouch(int32_t pointerId, TouchPhase phase, float x, float y, float pressure = 1.0f);
  void feedGamepadConnected(uint32_t index, bool connected);
  void feedGamepadAxis(uint32_t index, GamepadAxis axis, float value);
  void feedGamepadButton(uint32_t index, GamepadButton button, bool down);

  // Raw device queries.
  bool keyDown(Key key) const;
  bool keyPressed(Key key) const;
  bool keyReleased(Key key) const;

  Vec2 pointerPosition() const { return pointerPos_; }
  Vec2 pointerDelta() const { return pointerDelta_; }
  Vec2 wheelDelta() const { return wheelDelta_; }
  bool mouseDown(MouseButton button) const;
  bool mousePressed(MouseButton button) const;
  bool mouseReleased(MouseButton button) const;

  const std::vector<TouchPoint>& touches() const { return touches_; }
  uint32_t activeTouchCount() const;
  const TouchPoint* findTouch(int32_t id) const;
  const GestureState& gestures() const { return gestures_; }

  const GamepadState& gamepad(uint32_t index = 0) const;
  float gamepadAxis(uint32_t index, GamepadAxis axis) const;
  bool gamepadDown(uint32_t index, GamepadButton button) const;
  bool gamepadPressed(uint32_t index, GamepadButton button) const;

  // Action & Axis Mapping + Rebinding.
  void bindAction(const std::string& name, ActionBinding binding);
  void rebindAction(const std::string& name, Key newKey);
  void bindAxis(const std::string& name, AxisBinding binding);
  void clearBindings();

  bool actionDown(const std::string& name) const;
  bool actionPressed(const std::string& name) const;
  bool actionReleased(const std::string& name) const;
  float axisValue(const std::string& name) const;

  const std::unordered_map<std::string, std::vector<ActionBinding>>& actionBindings() const {
    return actions_;
  }
  const std::unordered_map<std::string, std::vector<AxisBinding>>& axisBindings() const {
    return axes_;
  }

 private:
  void updateGestures();

  bool keys_[static_cast<size_t>(Key::Count)]{};
  bool prevKeys_[static_cast<size_t>(Key::Count)]{};
  bool mouse_[static_cast<size_t>(MouseButton::Count)]{};
  bool prevMouse_[static_cast<size_t>(MouseButton::Count)]{};
  Vec2 pointerPos_{0, 0};
  Vec2 pointerDelta_{0, 0};
  Vec2 wheelDelta_{0, 0};
  bool hasPointer_ = false;

  std::vector<TouchPoint> touches_;
  GestureState gestures_{};
  float prevPinchDistance_ = 0.0f;
  double currentTime_ = 0.0;
  double lastTapTime_ = -10.0;
  Vec2 lastTapPos_{0, 0};

  static constexpr uint32_t kMaxGamepads = 4;
  GamepadState gamepads_[kMaxGamepads]{};

  std::unordered_map<std::string, std::vector<ActionBinding>> actions_;
  std::unordered_map<std::string, std::vector<AxisBinding>> axes_;
};

}  // namespace nimbus::input
