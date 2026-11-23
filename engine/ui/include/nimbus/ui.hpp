// Nimbus UI: retained widget tree, layout, theme, DPI scaling, rich widgets, and draw list.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "nimbus/math.hpp"

namespace nimbus::ui {

using math::Vec2;
using math::Vec4;

enum class Align : uint8_t { Start, Center, End, Stretch };
enum class Axis : uint8_t { Horizontal, Vertical };

struct Rect {
  float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
  bool contains(const Vec2& point) const {
    return point.x >= x && point.x <= x + width && point.y >= y && point.y <= y + height;
  }
  bool intersects(const Rect& other) const {
    return x < other.x + other.width && x + width > other.x &&
           y < other.y + other.height && y + height > other.y;
  }
};

struct Color {
  float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
};

struct Theme {
  Color surface{0.08f, 0.09f, 0.11f, 1.0f};
  Color panel{0.13f, 0.15f, 0.19f, 1.0f};
  Color control{0.18f, 0.20f, 0.25f, 1.0f};
  Color accent{0.22f, 0.56f, 0.94f, 1.0f};
  Color accentPressed{0.16f, 0.42f, 0.78f, 1.0f};
  Color text{0.94f, 0.95f, 0.97f, 1.0f};
  Color mutedText{0.62f, 0.66f, 0.74f, 1.0f};
  Color border{0.25f, 0.28f, 0.34f, 1.0f};
  float cornerRadius = 6.0f;
  float fontSize = 14.0f;
  float minTouchSize = 44.0f;

  static Theme dark() { return Theme{}; }
  static Theme light() {
    Theme t;
    t.surface = {0.95f, 0.96f, 0.98f, 1.0f};
    t.panel = {1.0f, 1.0f, 1.0f, 1.0f};
    t.control = {0.88f, 0.90f, 0.93f, 1.0f};
    t.accent = {0.14f, 0.46f, 0.88f, 1.0f};
    t.accentPressed = {0.10f, 0.35f, 0.72f, 1.0f};
    t.text = {0.10f, 0.12f, 0.15f, 1.0f};
    t.mutedText = {0.42f, 0.46f, 0.52f, 1.0f};
    t.border = {0.78f, 0.81f, 0.86f, 1.0f};
    return t;
  }
};

struct DrawCommand {
  enum class Kind : uint8_t { Rect, Text, Image, Line };
  Kind kind = Kind::Rect;
  Rect bounds{};
  Color color{1, 1, 1, 1};
  std::string text;
  float cornerRadius = 0.0f;
  float thickness = 1.0f;
  uint32_t texture = 0;
};

class Widget {
 public:
  virtual ~Widget() = default;
  virtual void layout(const Rect& available);
  virtual void draw(std::vector<DrawCommand>& out) const;
  virtual bool handlePointer(const Vec2& position, bool pressed);
  virtual bool handleKeyChar(char ch);
  virtual bool handleBackspace();

  void setRect(const Rect& rect) { rect_ = rect; }
  const Rect& rect() const { return rect_; }
  void setName(const std::string& name) { name_ = name; }
  const std::string& name() const { return name_; }
  void setVisible(bool visible) { visible_ = visible; }
  bool visible() const { return visible_; }
  void addChild(std::unique_ptr<Widget> child) {
    child->parent_ = this;
    children_.push_back(std::move(child));
  }
  const std::vector<std::unique_ptr<Widget>>& children() const { return children_; }
  Widget* parent() const { return parent_; }
  Widget* findByName(const std::string& name);

 protected:
  Rect rect_{};
  std::string name_;
  bool visible_ = true;
  Widget* parent_ = nullptr;
  std::vector<std::unique_ptr<Widget>> children_;
};

class Container : public Widget {
 public:
  void layout(const Rect& available) override;
  void draw(std::vector<DrawCommand>& out) const override;

  void setAxis(Axis axis) { axis_ = axis; }
  void setPadding(float value) { padding_ = value; }
  void setSpacing(float value) { spacing_ = value; }
  void setAlign(Align align) { align_ = align; }

 protected:
  Axis axis_ = Axis::Vertical;
  float padding_ = 8.0f;
  float spacing_ = 4.0f;
  Align align_ = Align::Start;
};

class ScrollView : public Container {
 public:
  void layout(const Rect& available) override;
  void draw(std::vector<DrawCommand>& out) const override;
  bool handlePointer(const Vec2& position, bool pressed) override;

  void setScrollOffset(float offset);
  void scrollBy(float delta);
  float scrollOffset() const { return scrollOffset_; }
  float contentHeight() const { return contentHeight_; }

 private:
  float scrollOffset_ = 0.0f;
  float contentHeight_ = 0.0f;
  bool dragging_ = false;
  float lastDragY_ = 0.0f;
};

class Label : public Widget {
 public:
  void draw(std::vector<DrawCommand>& out) const override;
  void setText(const std::string& text) { text_ = text; }
  const std::string& text() const { return text_; }
  void setTextColor(const Color& color) { color_ = color; }

 private:
  std::string text_;
  Color color_{1, 1, 1, 1};
};

class Button : public Widget {
 public:
  void draw(std::vector<DrawCommand>& out) const override;
  bool handlePointer(const Vec2& position, bool pressed) override;
  void setLabel(const std::string& label) { label_ = label; }
  void onClick(std::function<void()> callback) { onClick_ = std::move(callback); }

 private:
  std::string label_;
  std::function<void()> onClick_;
  bool pressed_ = false;
};

class Slider : public Widget {
 public:
  void draw(std::vector<DrawCommand>& out) const override;
  bool handlePointer(const Vec2& position, bool pressed) override;

  void setRange(float minValue, float maxValue);
  void setValue(float value);
  float value() const { return value_; }
  float minValue() const { return minValue_; }
  float maxValue() const { return maxValue_; }
  void onChange(std::function<void(float)> callback) { onChange_ = std::move(callback); }

 private:
  void updateFromX(float x);
  float minValue_ = 0.0f;
  float maxValue_ = 1.0f;
  float value_ = 0.0f;
  bool dragging_ = false;
  std::function<void(float)> onChange_;
};

class ListView : public Widget {
 public:
  void draw(std::vector<DrawCommand>& out) const override;
  bool handlePointer(const Vec2& position, bool pressed) override;

  void setItems(std::vector<std::string> items);
  const std::vector<std::string>& items() const { return items_; }
  void setItemHeight(float height) { itemHeight_ = height > 8.0f ? height : 32.0f; }
  void setSelectedIndex(int32_t index);
  int32_t selectedIndex() const { return selectedIndex_; }
  void setScrollOffset(float offset);
  float scrollOffset() const { return scrollOffset_; }
  void onSelect(std::function<void(int32_t, const std::string&)> cb) {
    onSelect_ = std::move(cb);
  }

 private:
  std::vector<std::string> items_;
  int32_t selectedIndex_ = -1;
  float itemHeight_ = 36.0f;
  float scrollOffset_ = 0.0f;
  std::function<void(int32_t, const std::string&)> onSelect_;
};

class TextField : public Widget {
 public:
  void draw(std::vector<DrawCommand>& out) const override;
  bool handlePointer(const Vec2& position, bool pressed) override;
  bool handleKeyChar(char ch) override;
  bool handleBackspace() override;

  void setText(const std::string& text) { text_ = text; }
  const std::string& text() const { return text_; }
  void setPlaceholder(const std::string& placeholder) { placeholder_ = placeholder; }
  void setFocused(bool focused) { focused_ = focused; }
  bool focused() const { return focused_; }
  void onChange(std::function<void(const std::string&)> cb) { onChange_ = std::move(cb); }
  void onSubmit(std::function<void(const std::string&)> cb) { onSubmit_ = std::move(cb); }

 private:
  std::string text_;
  std::string placeholder_;
  bool focused_ = false;
  std::function<void(const std::string&)> onChange_;
  std::function<void(const std::string&)> onSubmit_;
};

class Image : public Widget {
 public:
  void draw(std::vector<DrawCommand>& out) const override;
  void setTexture(uint32_t texture) { texture_ = texture; }

 private:
  uint32_t texture_ = 0;
};

class Canvas : public Widget {
 public:
  void draw(std::vector<DrawCommand>& out) const override;
  void addCommand(const DrawCommand& command) { commands_.push_back(command); }
  void clear() { commands_.clear(); }

 private:
  std::vector<DrawCommand> commands_;
};

// Root owns the tree, routes pointer/text events, tracks DPI scaling & theme.
class Root {
 public:
  void setRoot(std::unique_ptr<Widget> root) { root_ = std::move(root); }
  Widget* root() { return root_.get(); }
  void setTheme(const Theme& theme) { theme_ = theme; }
  const Theme& theme() const { return theme_; }
  void setDpiScale(float scale);
  float dpiScale() const { return dpiScale_; }
  float logicalWidth() const { return width_ / dpiScale_; }
  float logicalHeight() const { return height_ / dpiScale_; }

  void layout(float width, float height);
  void draw(std::vector<DrawCommand>& out) const;
  bool pointer(const Vec2& position, bool pressed);
  bool keyChar(char ch);
  bool backspace();
  Widget* pick(const Vec2& position) const;

  static float measureTextWidth(const std::string& text, float fontSize = 14.0f);

 private:
  std::unique_ptr<Widget> root_;
  Theme theme_{};
  float width_ = 1280.0f;
  float height_ = 720.0f;
  float dpiScale_ = 1.0f;
};

}  // namespace nimbus::ui
