// Nimbus ECS storage primitives. Entity handles include a generation counter
// so stale handles cannot become valid when an index is recycled.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nimbus::ecs {

using Entity = uint64_t;
inline constexpr Entity kNullEntity = std::numeric_limits<Entity>::max();
inline constexpr uint64_t kEntityIndexMask = 0xFFFFFFFFull;

constexpr uint32_t entityIndex(Entity entity) {
  return static_cast<uint32_t>(entity & kEntityIndexMask);
}
constexpr uint32_t entityGeneration(Entity entity) {
  return static_cast<uint32_t>(entity >> 32u);
}
constexpr Entity makeEntity(uint32_t index, uint32_t generation) {
  return (static_cast<Entity>(generation) << 32u) | index;
}

struct EntityId {
  uint32_t index = UINT32_MAX;
  uint32_t generation = 0;
};

struct TypeInfo {
  uint32_t size = 0;
  uint32_t align = 1;
  void (*construct)(void*) = nullptr;
  void (*destruct)(void*) = nullptr;
  void (*copyConstruct)(void*, const void*) = nullptr;
  void (*moveConstruct)(void*, void*) = nullptr;
  const char* name = nullptr;
};

class Registry {
 public:
  uint32_t registerType(TypeInfo info) {
    if (info.size == 0 || info.align == 0 || !info.construct || !info.destruct ||
        !info.moveConstruct)
      throw std::invalid_argument("invalid ECS component type metadata");
    types_.push_back(info);
    return static_cast<uint32_t>(types_.size() - 1);
  }
  const TypeInfo& type(uint32_t id) const {
    if (id >= types_.size()) throw std::out_of_range("ECS component type id");
    return types_[id];
  }
  uint32_t typeCount() const { return static_cast<uint32_t>(types_.size()); }

 private:
  std::vector<TypeInfo> types_;
};

// A compact type-erased column. It uses aligned allocation and explicit C++
// object construction/destruction; it never relocates non-trivial components
// with memcpy.
class ComponentPool {
 public:
  ComponentPool(uint32_t componentSize, uint32_t componentAlign, uint32_t initialCapacity = 16)
      : size_(componentSize), align_(componentAlign) {
    if (size_ == 0 || componentAlign == 0 ||
        (componentAlign & (componentAlign - 1u)) != 0)
      throw std::invalid_argument("invalid ECS component size/alignment");
    align_ = std::max<uint32_t>(componentAlign,
                                static_cast<uint32_t>(alignof(std::max_align_t)));
    const size_t alignment = static_cast<size_t>(align_);
    if (static_cast<size_t>(size_) > std::numeric_limits<size_t>::max() - (alignment - 1u))
      throw std::length_error("ECS component stride overflow");
    stride_ = ((static_cast<size_t>(size_) + alignment - 1u) / alignment) * alignment;
    reserve(initialCapacity);
  }

  ~ComponentPool() { clear(); releaseStorage(); }
  ComponentPool(const ComponentPool&) = delete;
  ComponentPool& operator=(const ComponentPool&) = delete;

  ComponentPool(ComponentPool&& other) noexcept { moveFrom(std::move(other)); }
  ComponentPool& operator=(ComponentPool&& other) noexcept {
    if (this != &other) {
      clear();
      releaseStorage();
      moveFrom(std::move(other));
    }
    return *this;
  }

  void setLifecycle(void (*construct)(void*), void (*destruct)(void*),
                    void (*copyConstruct)(void*, const void*),
                    void (*moveConstruct)(void*, void*)) {
    construct_ = construct;
    destruct_ = destruct;
    copyConstruct_ = copyConstruct;
    moveConstruct_ = moveConstruct;
  }

  void reserve(uint32_t capacity) {
    if (capacity <= capacity_) return;
    if (stride_ == 0 || capacity > std::numeric_limits<size_t>::max() / stride_)
      throw std::length_error("ECS component pool capacity overflow");
    auto* next = static_cast<std::byte*>(::operator new(
        static_cast<size_t>(capacity) * stride_, std::align_val_t(align_)));
    uint32_t constructed = 0;
    try {
      for (; constructed < used_; ++constructed) {
        void* destination = next + static_cast<size_t>(constructed) * stride_;
        void* source = slotPtr(constructed);
        if (copyConstruct_) copyConstruct_(destination, source);
        else moveConstruct_(destination, source);
      }
    } catch (...) {
      for (uint32_t i = 0; i < constructed; ++i)
        destruct_(next + static_cast<size_t>(i) * stride_);
      ::operator delete(next, std::align_val_t(align_));
      throw;
    }
    for (uint32_t i = 0; i < used_; ++i) destruct_(slotPtr(i));
    if (storage_) ::operator delete(storage_, std::align_val_t(align_));
    storage_ = next;
    capacity_ = capacity;
  }

  uint32_t add(Entity entity) {
    if (const auto found = entityToSlot_.find(entity); found != entityToSlot_.end())
      return found->second;
    if (!construct_ || !destruct_)
      throw std::logic_error("ECS component pool lifecycle is not configured");
    ensureCapacity();
    const uint32_t slot = used_;
    auto [mapping, inserted] = entityToSlot_.emplace(entity, slot);
    if (!inserted) throw std::logic_error("duplicate entity in ECS component pool");
    try {
      slotToEntity_.push_back(entity);
      construct_(slotPtr(slot));
    } catch (...) {
      if (slotToEntity_.size() > slot) slotToEntity_.pop_back();
      entityToSlot_.erase(mapping);
      throw;
    }
    ++used_;
    return slot;
  }

  uint32_t addMove(Entity entity, void* source) {
    if (!source || !moveConstruct_)
      throw std::invalid_argument("invalid ECS component move source or lifecycle");
    if (const auto found = entityToSlot_.find(entity); found != entityToSlot_.end())
      throw std::logic_error("duplicate entity in ECS component pool");
    ensureCapacity();
    const uint32_t slot = used_;
    auto [mapping, inserted] = entityToSlot_.emplace(entity, slot);
    if (!inserted) throw std::logic_error("duplicate entity in ECS component pool");
    try {
      slotToEntity_.push_back(entity);
    } catch (...) {
      entityToSlot_.erase(mapping);
      throw;
    }
    // World only registers noexcept relocation constructors, so the component
    // move cannot leave metadata half-committed.
    moveConstruct_(slotPtr(slot), source);
    ++used_;
    return slot;
  }

  uint32_t addCopy(Entity entity, const void* source) {
    if (!source || !copyConstruct_ || !destruct_)
      throw std::invalid_argument("ECS component is not copy constructible");
    if (const auto found = entityToSlot_.find(entity); found != entityToSlot_.end())
      throw std::logic_error("duplicate entity in ECS component pool");
    ensureCapacity();
    const uint32_t slot = used_;
    auto [mapping, inserted] = entityToSlot_.emplace(entity, slot);
    if (!inserted) throw std::logic_error("duplicate entity in ECS component pool");
    try {
      slotToEntity_.push_back(entity);
      copyConstruct_(slotPtr(slot), source);
    } catch (...) {
      if (slotToEntity_.size() > slot) slotToEntity_.pop_back();
      entityToSlot_.erase(mapping);
      throw;
    }
    ++used_;
    return slot;
  }

  void remove(Entity entity) {
    const auto it = entityToSlot_.find(entity);
    if (it == entityToSlot_.end()) return;
    const uint32_t slot = it->second;
    const uint32_t last = used_ - 1;
    if (slot == last) {
      destruct_(slotPtr(last));
    } else {
      destruct_(slotPtr(slot));
      moveConstruct_(slotPtr(slot), slotPtr(last));
      destruct_(slotPtr(last));
      const Entity moved = slotToEntity_[last];
      slotToEntity_[slot] = moved;
      entityToSlot_[moved] = slot;
    }
    slotToEntity_.pop_back();
    entityToSlot_.erase(it);
    --used_;
  }

  void* get(Entity entity) {
    const auto it = entityToSlot_.find(entity);
    return it == entityToSlot_.end() ? nullptr : slotPtr(it->second);
  }
  const void* get(Entity entity) const {
    const auto it = entityToSlot_.find(entity);
    return it == entityToSlot_.end() ? nullptr : slotPtr(it->second);
  }
  bool has(Entity entity) const { return entityToSlot_.count(entity) != 0; }
  uint32_t count() const { return used_; }
  Entity entityAt(uint32_t slot) const {
    if (slot >= used_) throw std::out_of_range("ECS component slot");
    return slotToEntity_[slot];
  }
  std::byte* slotPtr(uint32_t slot) {
    return storage_ + static_cast<size_t>(slot) * stride_;
  }
  const std::byte* slotPtr(uint32_t slot) const {
    return storage_ + static_cast<size_t>(slot) * stride_;
  }

  void clear() {
    if (destruct_) for (uint32_t i = used_; i > 0; --i) destruct_(slotPtr(i - 1));
    entityToSlot_.clear();
    slotToEntity_.clear();
    used_ = 0;
  }

 private:
  void ensureCapacity() {
    if (used_ == capacity_) {
      const uint32_t next = capacity_ == 0 ? 16u :
          (capacity_ > UINT32_MAX / 2u ? UINT32_MAX : capacity_ * 2u);
      if (next <= capacity_) throw std::length_error("ECS component pool exhausted");
      reserve(next);
    }
  }
  void releaseStorage() noexcept {
    if (storage_) ::operator delete(storage_, std::align_val_t(align_));
    storage_ = nullptr;
    capacity_ = 0;
  }
  void moveFrom(ComponentPool&& other) noexcept {
    size_ = other.size_; align_ = other.align_; stride_ = other.stride_;
    capacity_ = other.capacity_; used_ = other.used_;
    storage_ = other.storage_; entityToSlot_ = std::move(other.entityToSlot_);
    slotToEntity_ = std::move(other.slotToEntity_); construct_ = other.construct_;
    destruct_ = other.destruct_; copyConstruct_ = other.copyConstruct_;
    moveConstruct_ = other.moveConstruct_;
    other.storage_ = nullptr; other.stride_ = 0; other.capacity_ = 0; other.used_ = 0;
    other.construct_ = nullptr; other.destruct_ = nullptr;
    other.copyConstruct_ = nullptr; other.moveConstruct_ = nullptr;
  }

  uint32_t size_ = 0;
  uint32_t align_ = alignof(std::max_align_t);
  size_t stride_ = 0;
  uint32_t capacity_ = 0;
  uint32_t used_ = 0;
  std::byte* storage_ = nullptr;
  std::unordered_map<Entity, uint32_t> entityToSlot_;
  std::vector<Entity> slotToEntity_;
  void (*construct_)(void*) = nullptr;
  void (*destruct_)(void*) = nullptr;
  void (*copyConstruct_)(void*, const void*) = nullptr;
  void (*moveConstruct_)(void*, void*) = nullptr;
};

}  // namespace nimbus::ecs
