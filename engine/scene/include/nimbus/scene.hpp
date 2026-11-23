// Nimbus scene: hierarchy, transforms, component serialization, prefabs, multi-scene.
#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "nimbus/ecs.hpp"
#include "nimbus/math.hpp"

namespace nimbus::scene {

using math::Quat;
using math::Vec3;

struct Transform {
  Vec3 position{0, 0, 0};
  Quat rotation{0, 0, 0, 1};
  Vec3 scale{1, 1, 1};
};

struct PrefabInstance {
  std::string prefabId;
  std::vector<std::string> overrides;
};

struct Node {
  ecs::Entity entity = ecs::kNullEntity;
  ecs::Entity parent = ecs::kNullEntity;
  std::vector<ecs::Entity> children;
  Transform local{};
  math::Mat4 worldMatrix = math::mat4Identity();
  bool dirty = true;
};

// Scene is a hierarchy over ECS entities. Transform writes mark the subtree
// dirty; `updateTransforms` recomputes world matrices.
class Scene {
 public:
  explicit Scene(ecs::World* world, std::string sceneName = "main")
      : world_(world), name_(std::move(sceneName)) {}

  const std::string& name() const { return name_; }
  void setName(std::string name) { name_ = std::move(name); }

  ecs::Entity createNode(const std::string& name, ecs::Entity parent = ecs::kNullEntity) {
    if (!world_ || (parent != ecs::kNullEntity && nodes_.find(parent) == nodes_.end()))
      return ecs::kNullEntity;
    const ecs::Entity e = world_->create(name);
    world_->add<Transform>(e, Transform{});
    Node node;
    node.entity = e;
    node.parent = parent;
    nodes_[e] = node;
    if (parent != ecs::kNullEntity) {
      auto it = nodes_.find(parent);
      if (it != nodes_.end()) it->second.children.push_back(e);
    } else {
      roots_.push_back(e);
    }
    return e;
  }

  void setParent(ecs::Entity e, ecs::Entity parent) {
    auto it = nodes_.find(e);
    if (it == nodes_.end() || e == parent ||
        (parent != ecs::kNullEntity && nodes_.find(parent) == nodes_.end())) return;
    for (ecs::Entity ancestor = parent; ancestor != ecs::kNullEntity;) {
      if (ancestor == e) return;
      const auto ancestorIt = nodes_.find(ancestor);
      if (ancestorIt == nodes_.end()) break;
      ancestor = ancestorIt->second.parent;
    }
    if (it->second.parent == parent) return;
    if (it->second.parent != ecs::kNullEntity) {
      auto pit = nodes_.find(it->second.parent);
      if (pit != nodes_.end()) {
        auto& siblings = pit->second.children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), e), siblings.end());
      }
    } else {
      roots_.erase(std::remove(roots_.begin(), roots_.end(), e), roots_.end());
    }
    it->second.parent = parent;
    if (parent != ecs::kNullEntity) {
      auto pit = nodes_.find(parent);
      if (pit != nodes_.end() &&
          std::find(pit->second.children.begin(), pit->second.children.end(), e) ==
              pit->second.children.end()) pit->second.children.push_back(e);
    } else {
      roots_.push_back(e);
    }
    markDirty(e);
  }

  Transform* local(ecs::Entity e) {
    auto it = nodes_.find(e);
    return it == nodes_.end() ? nullptr : &it->second.local;
  }

  void setLocal(ecs::Entity e, const Transform& t) {
    auto it = nodes_.find(e);
    if (it == nodes_.end()) return;
    it->second.local = t;
    markDirty(e);
  }

  const math::Mat4& world(ecs::Entity e) {
    updateTransforms();
    auto it = nodes_.find(e);
    return it == nodes_.end() ? identity_ : it->second.worldMatrix;
  }

  void markDirty(ecs::Entity e) {
    auto it = nodes_.find(e);
    if (it == nodes_.end()) return;
    if (Transform* component = world_->get<Transform>(e)) *component = it->second.local;
    it->second.dirty = true;
    for (ecs::Entity child : it->second.children) markDirty(child);
  }

  void updateTransforms() {
    for (ecs::Entity root : roots_) updateNode(root, math::mat4Identity());
  }

  void destroy(ecs::Entity e) {
    auto it = nodes_.find(e);
    if (it == nodes_.end()) return;
    std::vector<ecs::Entity> children = it->second.children;
    for (ecs::Entity child : children) destroy(child);
    if (it->second.parent != ecs::kNullEntity) {
      auto pit = nodes_.find(it->second.parent);
      if (pit != nodes_.end()) {
        auto& siblings = pit->second.children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), e), siblings.end());
      }
    } else {
      roots_.erase(std::remove(roots_.begin(), roots_.end(), e), roots_.end());
    }
    nodes_.erase(it);
    world_->destroy(e);
  }

  uint32_t nodeCount() const { return static_cast<uint32_t>(nodes_.size()); }
  const std::vector<ecs::Entity>& roots() const { return roots_; }
  const Node* node(ecs::Entity e) const {
    auto it = nodes_.find(e);
    return it == nodes_.end() ? nullptr : &it->second;
  }
  Node* node(ecs::Entity e) {
    auto it = nodes_.find(e);
    return it == nodes_.end() ? nullptr : &it->second;
  }

  ecs::Entity findByName(const std::string& name) const {
    if (!world_) return ecs::kNullEntity;
    for (const auto& [entity, node] : nodes_) {
      (void)node;
      if (world_->name(entity) == name) return entity;
    }
    return ecs::kNullEntity;
  }

  ecs::World* world() { return world_; }

  // Prefab registry & instantiation with override tracking.
  bool registerPrefab(const std::string& id, const std::string& serializedJson);
  bool savePrefabFromNode(ecs::Entity rootEntity, const std::string& prefabId);
  ecs::Entity instantiatePrefab(const std::string& prefabId, ecs::Entity parent = ecs::kNullEntity);
  void markPrefabOverride(ecs::Entity entity, const std::string& componentName);
  bool revertPrefabInstance(ecs::Entity entity);
  const std::unordered_map<std::string, std::string>& prefabs() const { return prefabs_; }

  std::string serialize() const;
  std::string serializeSubtree(ecs::Entity rootEntity) const;
  bool deserialize(const std::string& json);
  bool deserializeAppend(const std::string& json, ecs::Entity parent = ecs::kNullEntity,
                         std::vector<ecs::Entity>* createdRoots = nullptr);

 private:
  void updateNode(ecs::Entity e, const math::Mat4& parentWorld) {
    auto it = nodes_.find(e);
    if (it == nodes_.end()) return;
    Node& node = it->second;
    const math::Mat4 localMatrix =
        math::mat4FromTrs(node.local.position, node.local.rotation, node.local.scale);
    node.worldMatrix = math::mat4Multiply(parentWorld, localMatrix);
    if (Transform* component = world_->get<Transform>(e)) *component = node.local;
    node.dirty = false;
    for (ecs::Entity child : node.children) updateNode(child, node.worldMatrix);
  }

  ecs::World* world_;
  std::string name_ = "main";
  std::unordered_map<ecs::Entity, Node> nodes_;
  std::vector<ecs::Entity> roots_;
  std::unordered_map<std::string, std::string> prefabs_;
  math::Mat4 identity_ = math::mat4Identity();
};

// Multi-scene manager sharing or isolating scenes by name.
class SceneManager {
 public:
  Scene* createScene(const std::string& name, ecs::World* world) {
    if (name.empty() || !world) return nullptr;
    auto& ptr = scenes_[name];
    ptr = std::make_unique<Scene>(world, name);
    if (active_.empty()) active_ = name;
    return ptr.get();
  }
  Scene* get(const std::string& name) {
    auto it = scenes_.find(name);
    return it == scenes_.end() ? nullptr : it->second.get();
  }
  bool setActive(const std::string& name) {
    if (scenes_.find(name) == scenes_.end()) return false;
    active_ = name;
    return true;
  }
  Scene* active() { return get(active_); }
  const std::string& activeName() const { return active_; }
  void unload(const std::string& name) {
    auto it = scenes_.find(name);
    if (it == scenes_.end()) return;
    const auto roots = it->second->roots();
    for (ecs::Entity r : roots) it->second->destroy(r);
    scenes_.erase(it);
    if (active_ == name) active_ = scenes_.empty() ? std::string{} : scenes_.begin()->first;
  }
  std::vector<std::string> sceneNames() const {
    std::vector<std::string> out;
    out.reserve(scenes_.size());
    for (const auto& [name, ptr] : scenes_) { (void)ptr; out.push_back(name); }
    return out;
  }

 private:
  std::unordered_map<std::string, std::unique_ptr<Scene>> scenes_;
  std::string active_;
};

}  // namespace nimbus::scene
