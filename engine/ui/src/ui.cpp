#include "nimbus/ui.hpp"

#include <algorithm>
#include <cmath>

namespace nimbus::ui {

void Widget::layout(const Rect& available) {
  rect_ = available;
  for (auto& child : children_) {
    if (child->visible()) child->layout(child->rect());
  }
}

void Widget::draw(std::vector<DrawCommand>& out) const {
  if (!visible_) return;
  for (const auto& child : children_) child->draw(out);
}

bool Widget::handlePointer(const Vec2& position, bool pressed) {
  for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
    if ((*it)->visible() && (*it)->handlePointer(position, pressed)) return true;
  }
  (void)position;
  (void)pressed;
  return false;
}

bool Widget::handleKeyChar(char ch) {
  for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
    if ((*it)->visible() && (*it)->handleKeyChar(ch)) return true;
  }
  return false;
}

bool Widget::handleBackspace() {
  for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
    if ((*it)->visible() && (*it)->handleBackspace()) return true;
  }
  return false;
}

Widget* Widget::findByName(const std::string& name) {
  if (name_ == name) return this;
  for (auto& child : children_)
    if (Widget* found = child->findByName(name)) return found;
  return nullptr;
}

void Container::layout(const Rect& available) {
  rect_ = available;
  float cursor = axis_ == Axis::Vertical ? available.y + padding_ : available.x + padding_;
  const float crossStart = axis_ == Axis::Vertical ? available.x + padding_ : available.y + padding_;
  const float crossSize = axis_ == Axis::Vertical ? available.width - padding_ * 2.0f
                                                  : available.height - padding_ * 2.0f;
  for (auto& child : children_) {
    if (!child->visible()) continue;
    Rect childRect = child->rect();
    if (axis_ == Axis::Vertical) {
      childRect.x = crossStart;
      childRect.width = align_ == Align::Stretch ? crossSize : childRect.width;
      childRect.y = cursor;
      cursor += childRect.height + spacing_;
    } else {
      childRect.y = crossStart;
      childRect.height = align_ == Align::Stretch ? crossSize : childRect.height;
      childRect.x = cursor;
      cursor += childRect.width + spacing_;
    }
    child->layout(childRect);
  }
}

void Container::draw(std::vector<DrawCommand>& out) const {
  if (!visible_) return;
  for (const auto& child : children_) child->draw(out);
}

void ScrollView::layout(const Rect& available) {
  rect_ = available;
  const float crossStart = available.x + padding_;
  const float crossSize = std::max(0.0f, available.width - padding_ * 2.0f);
  float localY = padding_;
  for (auto& child : children_) {
    if (!child->visible()) continue;
    Rect childRect = child->rect();
    childRect.x = crossStart;
    childRect.width = align_ == Align::Stretch ? crossSize : childRect.width;
    childRect.y = available.y + localY - scrollOffset_;
    localY += childRect.height + spacing_;
    child->layout(childRect);
  }
  contentHeight_ = localY + padding_;
  setScrollOffset(scrollOffset_);
}

void ScrollView::draw(std::vector<DrawCommand>& out) const {
  if (!visible_) return;
  for (const auto& child : children_) {
    if (child->visible() && child->rect().intersects(rect_)) {
      child->draw(out);
    }
  }
}

bool ScrollView::handlePointer(const Vec2& position, bool pressed) {
  if (!rect_.contains(position)) {
    dragging_ = false;
    return false;
  }
  for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
    if ((*it)->visible() && (*it)->rect().intersects(rect_) &&
        (*it)->handlePointer(position, pressed)) {
      return true;
    }
  }
  if (pressed) {
    if (!dragging_) {
      dragging_ = true;
      lastDragY_ = position.y;
    } else {
      const float dy = position.y - lastDragY_;
      lastDragY_ = position.y;
      scrollBy(-dy);
      layout(rect_);
    }
    return true;
  }
  dragging_ = false;
  return true;
}

void ScrollView::setScrollOffset(float offset) {
  const float maxOffset = std::max(0.0f, contentHeight_ - rect_.height);
  scrollOffset_ = std::clamp(std::isfinite(offset) ? offset : 0.0f, 0.0f, maxOffset);
}

void ScrollView::scrollBy(float delta) {
  if (!std::isfinite(delta)) return;
  setScrollOffset(scrollOffset_ + delta);
}

void Label::draw(std::vector<DrawCommand>& out) const {
  if (!visible_) return;
  DrawCommand command;
  command.kind = DrawCommand::Kind::Text;
  command.bounds = rect_;
  command.color = color_;
  command.text = text_;
  out.push_back(command);
}

void Button::draw(std::vector<DrawCommand>& out) const {
  if (!visible_) return;
  DrawCommand background;
  background.kind = DrawCommand::Kind::Rect;
  background.bounds = rect_;
  background.color = pressed_ ? Color{0.25f, 0.55f, 0.9f, 1.0f} : Color{0.18f, 0.18f, 0.22f, 1.0f};
  background.cornerRadius = 6.0f;
  out.push_back(background);
  DrawCommand text;
  text.kind = DrawCommand::Kind::Text;
  text.bounds = rect_;
  text.color = Color{1, 1, 1, 1};
  text.text = label_;
  out.push_back(text);
}

bool Button::handlePointer(const Vec2& position, bool pressed) {
  if (rect_.contains(position)) {
    if (pressed && !pressed_) pressed_ = true;
    if (!pressed && pressed_) {
      pressed_ = false;
      if (onClick_) onClick_();
    }
    return true;
  }
  pressed_ = false;
  return false;
}

void Slider::setRange(float minValue, float maxValue) {
  if (!std::isfinite(minValue) || !std::isfinite(maxValue) || maxValue <= minValue) return;
  minValue_ = minValue;
  maxValue_ = maxValue;
  setValue(value_);
}

void Slider::setValue(float value) {
  if (!std::isfinite(value)) return;
  value_ = std::clamp(value, minValue_, maxValue_);
}

void Slider::updateFromX(float x) {
  if (rect_.width <= 1e-4f) return;
  const float norm = std::clamp((x - rect_.x) / rect_.width, 0.0f, 1.0f);
  const float next = minValue_ + norm * (maxValue_ - minValue_);
  if (std::abs(next - value_) > 1e-6f) {
    value_ = next;
    if (onChange_) onChange_(value_);
  }
}

void Slider::draw(std::vector<DrawCommand>& out) const {
  if (!visible_) return;
  const float span = std::max(1e-5f, maxValue_ - minValue_);
  const float norm = std::clamp((value_ - minValue_) / span, 0.0f, 1.0f);
  const float trackH = std::max(4.0f, rect_.height * 0.25f);
  const float trackY = rect_.y + (rect_.height - trackH) * 0.5f;

  DrawCommand track;
  track.kind = DrawCommand::Kind::Rect;
  track.bounds = {rect_.x, trackY, rect_.width, trackH};
  track.color = {0.20f, 0.22f, 0.28f, 1.0f};
  track.cornerRadius = trackH * 0.5f;
  out.push_back(track);

  DrawCommand fill;
  fill.kind = DrawCommand::Kind::Rect;
  fill.bounds = {rect_.x, trackY, rect_.width * norm, trackH};
  fill.color = {0.22f, 0.56f, 0.94f, 1.0f};
  fill.cornerRadius = trackH * 0.5f;
  out.push_back(fill);

  const float knobSize = std::max(14.0f, rect_.height * 0.7f);
  DrawCommand knob;
  knob.kind = DrawCommand::Kind::Rect;
  knob.bounds = {rect_.x + rect_.width * norm - knobSize * 0.5f,
                 rect_.y + (rect_.height - knobSize) * 0.5f, knobSize, knobSize};
  knob.color = {0.95f, 0.96f, 0.98f, 1.0f};
  knob.cornerRadius = knobSize * 0.5f;
  out.push_back(knob);
}

bool Slider::handlePointer(const Vec2& position, bool pressed) {
  if (pressed && (rect_.contains(position) || dragging_)) {
    dragging_ = true;
    updateFromX(position.x);
    return true;
  }
  if (!pressed && dragging_) {
    dragging_ = false;
    updateFromX(position.x);
    return true;
  }
  return false;
}

void ListView::setItems(std::vector<std::string> items) {
  items_ = std::move(items);
  if (selectedIndex_ >= static_cast<int32_t>(items_.size())) selectedIndex_ = -1;
  setScrollOffset(scrollOffset_);
}

void ListView::setSelectedIndex(int32_t index) {
  if (index < -1 || index >= static_cast<int32_t>(items_.size())) return;
  selectedIndex_ = index;
}

void ListView::setScrollOffset(float offset) {
  const float totalH = static_cast<float>(items_.size()) * itemHeight_;
  const float maxOffset = std::max(0.0f, totalH - rect_.height);
  scrollOffset_ = std::clamp(std::isfinite(offset) ? offset : 0.0f, 0.0f, maxOffset);
}

void ListView::draw(std::vector<DrawCommand>& out) const {
  if (!visible_) return;
  DrawCommand bg;
  bg.kind = DrawCommand::Kind::Rect;
  bg.bounds = rect_;
  bg.color = {0.11f, 0.13f, 0.17f, 1.0f};
  bg.cornerRadius = 4.0f;
  out.push_back(bg);

  for (size_t i = 0; i < items_.size(); ++i) {
    const float y = rect_.y + static_cast<float>(i) * itemHeight_ - scrollOffset_;
    if (y + itemHeight_ < rect_.y || y > rect_.y + rect_.height) continue;
    const Rect rowRect{rect_.x + 2.0f, y, std::max(0.0f, rect_.width - 4.0f), itemHeight_};
    const bool selected = static_cast<int32_t>(i) == selectedIndex_;

    DrawCommand row;
    row.kind = DrawCommand::Kind::Rect;
    row.bounds = rowRect;
    row.color = selected ? Color{0.22f, 0.56f, 0.94f, 0.85f}
                         : (i % 2 == 0 ? Color{0.15f, 0.17f, 0.22f, 1.0f}
                                       : Color{0.13f, 0.15f, 0.19f, 1.0f});
    out.push_back(row);

    DrawCommand txt;
    txt.kind = DrawCommand::Kind::Text;
    txt.bounds = {rowRect.x + 8.0f, rowRect.y, std::max(0.0f, rowRect.width - 16.0f), rowRect.height};
    txt.color = {0.95f, 0.96f, 0.98f, 1.0f};
    txt.text = items_[i];
    out.push_back(txt);
  }
}

bool ListView::handlePointer(const Vec2& position, bool pressed) {
  if (!rect_.contains(position)) return false;
  if (!pressed) {
    const float localY = (position.y - rect_.y) + scrollOffset_;
    const int32_t index = static_cast<int32_t>(localY / itemHeight_);
    if (index >= 0 && index < static_cast<int32_t>(items_.size())) {
      selectedIndex_ = index;
      if (onSelect_) onSelect_(selectedIndex_, items_[static_cast<size_t>(selectedIndex_)]);
      return true;
    }
  }
  return true;
}

void TextField::draw(std::vector<DrawCommand>& out) const {
  if (!visible_) return;
  DrawCommand bg;
  bg.kind = DrawCommand::Kind::Rect;
  bg.bounds = rect_;
  bg.color = focused_ ? Color{0.18f, 0.22f, 0.30f, 1.0f} : Color{0.12f, 0.14f, 0.18f, 1.0f};
  bg.cornerRadius = 4.0f;
  out.push_back(bg);

  DrawCommand txt;
  txt.kind = DrawCommand::Kind::Text;
  txt.bounds = {rect_.x + 8.0f, rect_.y, std::max(0.0f, rect_.width - 16.0f), rect_.height};
  const bool empty = text_.empty();
  txt.text = empty ? placeholder_ : text_;
  txt.color = empty ? Color{0.55f, 0.58f, 0.65f, 1.0f} : Color{0.96f, 0.97f, 0.99f, 1.0f};
  out.push_back(txt);
}

bool TextField::handlePointer(const Vec2& position, bool pressed) {
  if (pressed) {
    focused_ = rect_.contains(position);
    return focused_;
  }
  return rect_.contains(position);
}

bool TextField::handleKeyChar(char ch) {
  if (!focused_) return false;
  if (ch == '\n' || ch == '\r') {
    if (onSubmit_) onSubmit_(text_);
    return true;
  }
  if (static_cast<unsigned char>(ch) >= 32 && static_cast<unsigned char>(ch) <= 126) {
    text_.push_back(ch);
    if (onChange_) onChange_(text_);
    return true;
  }
  return false;
}

bool TextField::handleBackspace() {
  if (!focused_) return false;
  if (!text_.empty()) {
    text_.pop_back();
    if (onChange_) onChange_(text_);
  }
  return true;
}

void Image::draw(std::vector<DrawCommand>& out) const {
  if (!visible_) return;
  DrawCommand command;
  command.kind = DrawCommand::Kind::Image;
  command.bounds = rect_;
  command.texture = texture_;
  out.push_back(command);
}

void Canvas::draw(std::vector<DrawCommand>& out) const {
  if (!visible_) return;
  for (const DrawCommand& command : commands_) out.push_back(command);
}

void Root::setDpiScale(float scale) {
  if (std::isfinite(scale) && scale > 0.25f && scale <= 8.0f) {
    dpiScale_ = scale;
  }
}

void Root::layout(float width, float height) {
  width_ = width;
  height_ = height;
  if (root_) root_->layout(Rect{0, 0, logicalWidth(), logicalHeight()});
}

void Root::draw(std::vector<DrawCommand>& out) const {
  if (root_) root_->draw(out);
}

bool Root::pointer(const Vec2& position, bool pressed) {
  if (!root_) return false;
  const Vec2 logical{position.x / dpiScale_, position.y / dpiScale_};
  return root_->handlePointer(logical, pressed);
}

bool Root::keyChar(char ch) {
  return root_ ? root_->handleKeyChar(ch) : false;
}

bool Root::backspace() {
  return root_ ? root_->handleBackspace() : false;
}

Widget* Root::pick(const Vec2& position) const {
  const Vec2 logical{position.x / dpiScale_, position.y / dpiScale_};
  if (!root_ || !root_->rect().contains(logical)) return nullptr;
  std::function<Widget*(Widget*)> search = [&](Widget* widget) -> Widget* {
    for (auto it = widget->children().rbegin(); it != widget->children().rend(); ++it) {
      if (Widget* found = search(it->get())) return found;
    }
    return widget->rect().contains(logical) ? widget : nullptr;
  };
  return search(root_.get());
}

float Root::measureTextWidth(const std::string& text, float fontSize) {
  return static_cast<float>(text.size()) * (fontSize * 0.56f);
}

}  // namespace nimbus::ui
