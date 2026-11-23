// Nimbus archetype ECS with generation-safe handles, parallel iteration,
// reflection-based serialization hooks, and dependency system scheduling.
#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "nimbus/jobs.hpp"
#include "pool.hpp"

namespace nimbus::ecs {

class World;

struct ComponentSerializer {
  std::string name;
  std::function<bool(const World&, Entity)> has;
  std::function<std::string(const World&, Entity)> serializeJson;
  std::function<bool(World&, Entity, const std::string&)> deserializeJson;
};

struct Archetype {
  std::vector<uint32_t> componentIds;
  std::vector<ComponentPool> pools;
  std::vector<Entity> entities;
  uint32_t id = 0;
};

class World {
 public:
  explicit World(Registry* registry) : registry_(registry) {
    if (!registry_) throw std::invalid_argument("ECS World requires a Registry");
  }

  Entity create(std::string name = {}) {
    uint32_t index = 0;
    uint32_t generation = 1;
    if (!free_.empty()) {
      index = free_.back();
      free_.pop_back();
      if (generations_[index] == UINT32_MAX)
        throw std::overflow_error("ECS entity generation exhausted");
      generation = ++generations_[index];
    } else {
      if (generations_.size() >= UINT32_MAX)
        throw std::overflow_error("ECS entity index exhausted");
      index = static_cast<uint32_t>(generations_.size());
      generations_.push_back(generation);
      names_.emplace_back();
    }
    const Entity entity = makeEntity(index, generation);
    names_[index] = std::move(name);
    const uint32_t empty = ensureArchetype({});
    Archetype& archetype = archetypes_[empty];
    entityArchetype_[entity] = empty;
    entitySlotInArch_[entity] = static_cast<uint32_t>(archetype.entities.size());
    archetype.entities.push_back(entity);
    return entity;
  }

  void destroy(Entity entity) {
    auto entityIt = entityArchetype_.find(entity);
    if (entityIt == entityArchetype_.end()) return;
    const uint32_t index = entityIndex(entity);
    const uint32_t archetypeId = entityIt->second;
    Archetype& arch = archetypes_[archetypeId];
    const auto slotIt = entitySlotInArch_.find(entity);
    if (slotIt != entitySlotInArch_.end() && slotIt->second < arch.entities.size()) {
      const uint32_t slot = slotIt->second;
      const uint32_t last = static_cast<uint32_t>(arch.entities.size() - 1);
      if (slot != last) {
        const Entity moved = arch.entities[last];
        arch.entities[slot] = moved;
        entitySlotInArch_[moved] = slot;
      }
      arch.entities.pop_back();
    }
    for (ComponentPool& pool : arch.pools) pool.remove(entity);
    entitySlotInArch_.erase(entity);
    entityArchetype_.erase(entityIt);
    names_[index].clear();
    free_.push_back(index);
  }

  bool alive(Entity entity) const {
    return entity != kNullEntity && entityArchetype_.find(entity) != entityArchetype_.end();
  }
  uint32_t generation(Entity entity) const {
    return alive(entity) ? entityGeneration(entity) : 0;
  }
  void setName(Entity entity, std::string name) {
    if (alive(entity)) names_[entityIndex(entity)] = std::move(name);
  }
  const std::string& name(Entity entity) const {
    static const std::string empty;
    return alive(entity) ? names_[entityIndex(entity)] : empty;
  }

  template <typename T>
  T* add(Entity entity) {
    if (!alive(entity)) return nullptr;
    const uint32_t cid = componentId<T>();
    const uint32_t sourceId = entityArchetype_.at(entity);
    const std::vector<uint32_t>& sourceIds = archetypes_[sourceId].componentIds;
    if (std::binary_search(sourceIds.begin(), sourceIds.end(), cid)) return get<T>(entity);
    const uint32_t targetId = ensureArchetype(withComponent(sourceIds, cid));
    moveTo(entity, sourceId, targetId, cid);
    return get<T>(entity);
  }

  template <typename T>
  T* add(Entity entity, const T& value) {
    if (!alive(entity)) return nullptr;
    const uint32_t cid = componentId<T>();
    const uint32_t sourceId = entityArchetype_.at(entity);
    const std::vector<uint32_t>& sourceIds = archetypes_[sourceId].componentIds;
    if (std::binary_search(sourceIds.begin(), sourceIds.end(), cid)) {
      T* existing = get<T>(entity);
      if constexpr (std::is_copy_assignable_v<T>) {
        if (existing) *existing = value;
        return existing;
      } else {
        return nullptr;
      }
    }
    if constexpr (!std::is_copy_constructible_v<T>) {
      return nullptr;
    } else {
      const uint32_t targetId = ensureArchetype(withComponent(sourceIds, cid));
      moveTo(entity, sourceId, targetId, cid, &value, false);
      return get<T>(entity);
    }
  }

  template <typename T>
    requires (!std::is_lvalue_reference_v<T>)
  std::remove_reference_t<T>* add(Entity entity, T&& value) {
    using Component = std::remove_cvref_t<T>;
    if (!alive(entity)) return nullptr;
    const uint32_t cid = componentId<Component>();
    const uint32_t sourceId = entityArchetype_.at(entity);
    const auto& sourceIds = archetypes_[sourceId].componentIds;
    if (std::binary_search(sourceIds.begin(), sourceIds.end(), cid)) {
      Component* existing = get<Component>(entity);
      if constexpr (std::is_move_assignable_v<Component>) {
        if (existing) *existing = std::move(value);
        return existing;
      } else {
        return nullptr;
      }
    }
    const uint32_t targetId = ensureArchetype(withComponent(sourceIds, cid));
    moveTo(entity, sourceId, targetId, cid, &value, true);
    return get<Component>(entity);
  }

  template <typename T>
  void remove(Entity entity) {
    if (!alive(entity)) return;
    const uint32_t cid = componentId<T>();
    const uint32_t sourceId = entityArchetype_.at(entity);
    const auto& sourceIds = archetypes_[sourceId].componentIds;
    if (!std::binary_search(sourceIds.begin(), sourceIds.end(), cid)) return;
    const uint32_t targetId = ensureArchetype(withoutComponent(sourceIds, cid));
    moveTo(entity, sourceId, targetId, kNoComponent);
  }

  template <typename T>
  T* get(Entity entity) {
    if (!alive(entity)) return nullptr;
    const uint32_t cid = componentId<T>();
    const uint32_t archetypeId = entityArchetype_.at(entity);
    const uint32_t column = indexOf(archetypeId, cid);
    if (column == kNoComponent) return nullptr;
    return static_cast<T*>(archetypes_[archetypeId].pools[column].get(entity));
  }

  template <typename T>
  const T* get(Entity entity) const {
    if (!alive(entity)) return nullptr;
    const uint32_t cid = const_cast<World*>(this)->componentId<T>();
    const uint32_t archetypeId = entityArchetype_.at(entity);
    const uint32_t column = indexOf(archetypeId, cid);
    if (column == kNoComponent) return nullptr;
    return static_cast<const T*>(archetypes_[archetypeId].pools[column].get(entity));
  }

  template <typename T>
  bool has(Entity entity) const { return get<T>(entity) != nullptr; }

  template <typename... Ts>
  const std::vector<uint32_t>& query() {
    static_assert(sizeof...(Ts) > 0, "ECS query requires at least one component type");
    const uint64_t key = queryKey<Ts...>();
    const auto cached = queryCache_.find(key);
    if (cached != queryCache_.end()) return cached->second;
    std::vector<uint32_t> ids{componentId<Ts>()...};
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    std::vector<uint32_t> matched;
    for (const Archetype& arch : archetypes_)
      if (containsAll(arch.componentIds, ids)) matched.push_back(arch.id);
    return queryCache_.emplace(key, std::move(matched)).first->second;
  }

  template <typename... Ts, typename Fn>
  void forEach(Fn&& fn) {
    const std::vector<uint32_t> archs = query<Ts...>();
    for (uint32_t archId : archs) {
      const std::vector<Entity> entities = archetypes_[archId].entities;
      for (Entity entity : entities) {
        if (!alive(entity)) continue;
        fn(entity, *get<Ts>(entity)...);
      }
    }
  }

  template <typename... Ts, typename Fn>
  void parallelForEach(jobs::JobSystem* jobSystem, Fn&& fn) {
    const std::vector<uint32_t> archs = query<Ts...>();
    for (uint32_t archId : archs) {
      Archetype& arch = archetypes_[archId];
      const uint32_t count = static_cast<uint32_t>(arch.entities.size());
      if (count == 0) continue;
      if (!jobSystem || jobSystem->workerCount() <= 1 || count < 64) {
        for (uint32_t i = 0; i < count; ++i) {
          const Entity e = arch.entities[i];
          fn(e, *get<Ts>(e)...);
        }
      } else {
        jobSystem->parallelFor(count, 32, [&](uint32_t begin, uint32_t end) {
          for (uint32_t i = begin; i < end; ++i) {
            const Entity e = arch.entities[i];
            fn(e, *get<Ts>(e)...);
          }
        });
      }
    }
  }

  // Component serialization registry for Scene / Prefab / Editor inspection.
  void registerSerializer(ComponentSerializer serializer) {
    if (serializer.name.empty()) return;
    for (ComponentSerializer& existing : serializers_) {
      if (existing.name == serializer.name) {
        existing = std::move(serializer);
        return;
      }
    }
    serializers_.push_back(std::move(serializer));
  }

  const std::vector<ComponentSerializer>& serializers() const { return serializers_; }

  std::vector<std::string> componentNames(Entity entity) const {
    std::vector<std::string> names;
    if (!alive(entity)) return names;
    for (const ComponentSerializer& ser : serializers_) {
      if (ser.has && ser.has(*this, entity)) names.push_back(ser.name);
    }
    return names;
  }

  void invalidateQueryCache() { queryCache_.clear(); }
  uint32_t entityArchetype(Entity entity) const {
    const auto it = entityArchetype_.find(entity);
    return it == entityArchetype_.end() ? kNoComponent : it->second;
  }
  uint32_t archetypeCount() const { return static_cast<uint32_t>(archetypes_.size()); }
  const Archetype& archetype(uint32_t id) const { return archetypes_.at(id); }
  Archetype& archetype(uint32_t id) { return archetypes_.at(id); }
  uint32_t entityCount() const {
    uint32_t total = 0;
    for (const Archetype& arch : archetypes_) total += static_cast<uint32_t>(arch.entities.size());
    return total;
  }
  uint64_t estimatedMemoryBytes() const {
    uint64_t bytes = sizeof(World) + generations_.size() * sizeof(uint32_t) +
                     free_.size() * sizeof(uint32_t);
    for (const Archetype& arch : archetypes_) {
      bytes += arch.entities.capacity() * sizeof(Entity);
      for (const ComponentPool& pool : arch.pools)
        bytes += static_cast<uint64_t>(pool.count()) * registry_->type(arch.componentIds[0]).size + 64;
    }
    return bytes;
  }
  Registry* registry() { return registry_; }
  const Registry* registry() const { return registry_; }

 private:
  static constexpr uint32_t kNoComponent = UINT32_MAX;

  template <typename T>
  uint32_t componentId() {
    const std::type_index type = std::type_index(typeid(T));
    const auto found = componentIds_.find(type);
    if (found != componentIds_.end()) return found->second;
    static_assert(std::is_default_constructible_v<T>, "ECS components must be default constructible");
    static_assert(std::is_destructible_v<T>, "ECS components must be destructible");
    static_assert(std::is_nothrow_destructible_v<T>,
                  "ECS components must have noexcept destructors");
    static_assert(std::is_nothrow_move_constructible_v<T> ||
                      std::is_nothrow_copy_constructible_v<T>,
                  "ECS components must support noexcept move or copy relocation");
    TypeInfo info{};
    info.size = static_cast<uint32_t>(sizeof(T));
    info.align = static_cast<uint32_t>(alignof(T));
    info.construct = [](void* memory) { new (memory) T(); };
    info.destruct = [](void* memory) { static_cast<T*>(memory)->~T(); };
    if constexpr (std::is_copy_constructible_v<T>)
      info.copyConstruct = [](void* destination, const void* source) {
        new (destination) T(*static_cast<const T*>(source));
      };
    info.moveConstruct = [](void* destination, void* source) {
      if constexpr (std::is_nothrow_move_constructible_v<T>)
        new (destination) T(std::move(*static_cast<T*>(source)));
      else
        new (destination) T(*static_cast<const T*>(source));
    };
    info.name = type.name();
    const uint32_t id = registry_->registerType(info);
    componentIds_.emplace(type, id);
    return id;
  }

  template <typename... Ts>
  uint64_t queryKey() {
    std::vector<uint32_t> ids{componentId<Ts>()...};
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    uint64_t key = 1469598103934665603ull;
    for (uint32_t id : ids) { key ^= id; key *= 1099511628211ull; }
    return key;
  }

  static bool containsAll(const std::vector<uint32_t>& haystack,
                          const std::vector<uint32_t>& needle) {
    for (uint32_t id : needle)
      if (!std::binary_search(haystack.begin(), haystack.end(), id)) return false;
    return true;
  }
  static std::vector<uint32_t> withComponent(const std::vector<uint32_t>& ids, uint32_t added) {
    std::vector<uint32_t> result = ids;
    result.insert(std::lower_bound(result.begin(), result.end(), added), added);
    return result;
  }
  static std::vector<uint32_t> withoutComponent(const std::vector<uint32_t>& ids, uint32_t removed) {
    std::vector<uint32_t> result;
    result.reserve(ids.size());
    for (uint32_t id : ids) if (id != removed) result.push_back(id);
    return result;
  }
  uint32_t indexOf(uint32_t archetypeId, uint32_t componentId) const {
    const auto& ids = archetypes_.at(archetypeId).componentIds;
    const auto it = std::lower_bound(ids.begin(), ids.end(), componentId);
    return it == ids.end() || *it != componentId ? kNoComponent
                                                  : static_cast<uint32_t>(it - ids.begin());
  }

  uint32_t ensureArchetype(const std::vector<uint32_t>& ids) {
    for (const Archetype& arch : archetypes_)
      if (arch.componentIds == ids) return arch.id;
    Archetype arch;
    arch.componentIds = ids;
    arch.id = static_cast<uint32_t>(archetypes_.size());
    arch.pools.reserve(ids.size());
    for (uint32_t component : ids) {
      const TypeInfo& info = registry_->type(component);
      arch.pools.emplace_back(info.size, info.align);
      arch.pools.back().setLifecycle(info.construct, info.destruct, info.copyConstruct,
                                     info.moveConstruct);
    }
    archetypes_.push_back(std::move(arch));
    invalidateQueryCache();
    return archetypes_.back().id;
  }

  void removeEntityFromArchetype(Archetype& archetype, Entity entity) {
    const auto slotIt = entitySlotInArch_.find(entity);
    if (slotIt == entitySlotInArch_.end()) return;
    const uint32_t slot = slotIt->second;
    const uint32_t last = static_cast<uint32_t>(archetype.entities.size() - 1);
    if (slot != last) {
      const Entity moved = archetype.entities[last];
      archetype.entities[slot] = moved;
      entitySlotInArch_[moved] = slot;
    }
    archetype.entities.pop_back();
    entitySlotInArch_.erase(entity);
  }

  void moveTo(Entity entity, uint32_t fromId, uint32_t toId, uint32_t addedComponent,
              const void* addedValue = nullptr, bool moveAddedValue = false) {
    if (fromId == toId) return;
    Archetype& from = archetypes_[fromId];
    Archetype& to = archetypes_[toId];

    if (addedComponent != kNoComponent) {
      const uint32_t column = indexOf(toId, addedComponent);
      if (column != kNoComponent && !to.pools[column].has(entity)) {
        if (!addedValue) to.pools[column].add(entity);
        else if (moveAddedValue)
          to.pools[column].addMove(entity, const_cast<void*>(addedValue));
        else
          to.pools[column].addCopy(entity, addedValue);
      }
    }
    for (uint32_t component : from.componentIds) {
      const uint32_t sourceColumn = indexOf(fromId, component);
      const uint32_t targetColumn = indexOf(toId, component);
      if (targetColumn != kNoComponent) {
        void* source = from.pools[sourceColumn].get(entity);
        if (source) to.pools[targetColumn].addMove(entity, source);
      }
      from.pools[sourceColumn].remove(entity);
    }
    removeEntityFromArchetype(from, entity);
    to.entities.push_back(entity);
    entitySlotInArch_[entity] = static_cast<uint32_t>(to.entities.size() - 1);
    entityArchetype_[entity] = toId;
  }

  Registry* registry_;
  std::deque<Archetype> archetypes_;
  std::vector<uint32_t> generations_;
  std::vector<uint32_t> free_;
  std::vector<std::string> names_;
  std::unordered_map<Entity, uint32_t> entityArchetype_;
  std::unordered_map<Entity, uint32_t> entitySlotInArch_;
  std::unordered_map<uint64_t, std::vector<uint32_t>> queryCache_;
  std::unordered_map<std::type_index, uint32_t> componentIds_;
  std::vector<ComponentSerializer> serializers_;
};

// Dependency-ordered ECS system scheduler.
class Scheduler {
 public:
  using SystemFn = std::function<void(World&, float)>;

  struct SystemEntry {
    std::string name;
    std::vector<std::string> after;
    SystemFn fn;
    bool enabled = true;
  };

  bool addSystem(const std::string& name, SystemFn fn, std::vector<std::string> after = {}) {
    if (name.empty() || !fn) return false;
    for (const SystemEntry& entry : systems_) {
      if (entry.name == name) return false;
    }
    systems_.push_back({name, std::move(after), std::move(fn), true});
    dirty_ = true;
    return true;
  }

  void setEnabled(const std::string& name, bool enabled) {
    for (SystemEntry& entry : systems_) {
      if (entry.name == name) entry.enabled = enabled;
    }
  }

  bool compile() {
    order_.clear();
    std::unordered_map<std::string, uint32_t> indegree;
    std::unordered_map<std::string, std::vector<std::string>> dependents;
    std::unordered_map<std::string, size_t> indexByName;
    for (size_t i = 0; i < systems_.size(); ++i) {
      indexByName[systems_[i].name] = i;
      indegree[systems_[i].name] = 0;
    }
    for (const SystemEntry& entry : systems_) {
      for (const std::string& dep : entry.after) {
        if (indexByName.find(dep) != indexByName.end()) {
          dependents[dep].push_back(entry.name);
          indegree[entry.name]++;
        }
      }
    }
    std::vector<std::string> ready;
    for (const SystemEntry& entry : systems_) {
      if (indegree[entry.name] == 0) ready.push_back(entry.name);
    }
    while (!ready.empty()) {
      const std::string current = ready.front();
      ready.erase(ready.begin());
      order_.push_back(indexByName[current]);
      for (const std::string& child : dependents[current]) {
        if (--indegree[child] == 0) ready.push_back(child);
      }
    }
    if (order_.size() != systems_.size()) {
      order_.clear();
      return false;
    }
    dirty_ = false;
    return true;
  }

  bool run(World& world, float deltaSeconds) {
    if (dirty_ && !compile()) return false;
    for (size_t idx : order_) {
      if (systems_[idx].enabled && systems_[idx].fn) systems_[idx].fn(world, deltaSeconds);
    }
    return true;
  }

  uint32_t systemCount() const { return static_cast<uint32_t>(systems_.size()); }
  std::vector<std::string> executionOrder() {
    if (dirty_) compile();
    std::vector<std::string> names;
    names.reserve(order_.size());
    for (size_t idx : order_) names.push_back(systems_[idx].name);
    return names;
  }

 private:
  std::vector<SystemEntry> systems_;
  std::vector<size_t> order_;
  bool dirty_ = false;
};

}  // namespace nimbus::ecs
