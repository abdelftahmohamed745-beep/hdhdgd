// Nimbus AI: behavior trees, steering, perception, navmesh pathfinding, GOAP.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "nimbus/ecs.hpp"
#include "nimbus/math.hpp"

namespace nimbus::ai {

using math::Vec3;

enum class Status : uint8_t { Invalid, Running, Success, Failure };

// ---- Behavior tree -------------------------------------------------------

class Node {
 public:
  virtual ~Node() = default;
  virtual Status tick(ecs::Entity entity, float deltaSeconds) = 0;
  virtual void reset() {}
};

using NodePtr = std::unique_ptr<Node>;

class Sequence : public Node {
 public:
  void add(NodePtr child) { children_.push_back(std::move(child)); }
  Status tick(ecs::Entity entity, float deltaSeconds) override;
  void reset() override;

 private:
  std::vector<NodePtr> children_;
  uint32_t cursor_ = 0;
};

class Selector : public Node {
 public:
  void add(NodePtr child) { children_.push_back(std::move(child)); }
  Status tick(ecs::Entity entity, float deltaSeconds) override;
  void reset() override;

 private:
  std::vector<NodePtr> children_;
  uint32_t cursor_ = 0;
};

// Decorator that inverts the child's success/failure.
class Inverter : public Node {
 public:
  void setChild(NodePtr child) { child_ = std::move(child); }
  Status tick(ecs::Entity entity, float deltaSeconds) override;
  void reset() override {
    if (child_) child_->reset();
  }

 private:
  NodePtr child_;
};

// Decorator enforcing a minimum cooldown interval between completions.
class Cooldown : public Node {
 public:
  explicit Cooldown(float durationSeconds = 1.0f) : duration_(durationSeconds) {}
  void setDuration(float durationSeconds) { duration_ = durationSeconds; }
  float duration() const { return duration_; }
  float remaining() const { return remaining_; }
  void setChild(NodePtr child) { child_ = std::move(child); }
  Status tick(ecs::Entity entity, float deltaSeconds) override;
  void reset() override;

 private:
  float duration_ = 1.0f;
  float remaining_ = 0.0f;
  NodePtr child_;
};

// Decorator repeating child execution `repeatCount` times (0 = infinite until failure).
class Repeater : public Node {
 public:
  explicit Repeater(uint32_t repeatCount = 0) : repeatCount_(repeatCount) {}
  void setCount(uint32_t repeatCount) { repeatCount_ = repeatCount; }
  uint32_t count() const { return repeatCount_; }
  void setChild(NodePtr child) { child_ = std::move(child); }
  Status tick(ecs::Entity entity, float deltaSeconds) override;
  void reset() override;

 private:
  uint32_t repeatCount_ = 0;
  uint32_t completed_ = 0;
  NodePtr child_;
};

// Condition leaf or guard decorator evaluating a predicate before running optional child.
class Condition : public Node {
 public:
  using Predicate = std::function<bool(ecs::Entity)>;
  explicit Condition(Predicate predicate, NodePtr child = nullptr)
      : predicate_(std::move(predicate)), child_(std::move(child)) {}
  void setChild(NodePtr child) { child_ = std::move(child); }
  Status tick(ecs::Entity entity, float deltaSeconds) override;
  void reset() override {
    if (child_) child_->reset();
  }

 private:
  Predicate predicate_;
  NodePtr child_;
};

// Leaf that calls back into gameplay code (Lua or C++).
class Action : public Node {
 public:
  using Fn = std::function<Status(ecs::Entity, float)>;
  explicit Action(Fn fn) : fn_(std::move(fn)) {}
  Status tick(ecs::Entity entity, float deltaSeconds) override {
    return fn_ ? fn_(entity, deltaSeconds) : Status::Failure;
  }

 private:
  Fn fn_;
};

// ECS component that owns and ticks one behavior tree on the engine thread.
struct Agent {
  NodePtr root;
  Status lastStatus = Status::Invalid;
  bool enabled = true;

  void update(ecs::Entity entity, float deltaSeconds) {
    if (enabled && root) lastStatus = root->tick(entity, deltaSeconds);
  }
  void reset() {
    if (root) root->reset();
    lastStatus = Status::Invalid;
  }
};

// ---- Steering behaviors --------------------------------------------------

struct SteeringAgent {
  Vec3 position{0, 0, 0};
  Vec3 velocity{0, 0, 0};
  float maxSpeed = 6.0f;
  float maxForce = 18.0f;
  float radius = 0.5f;
  float wanderAngle = 0.0f;
};

namespace steering {

Vec3 seek(const SteeringAgent& agent, const Vec3& target);
Vec3 flee(const SteeringAgent& agent, const Vec3& threat);
Vec3 arrive(const SteeringAgent& agent, const Vec3& target, float slowRadius);
Vec3 wander(SteeringAgent& agent, float circleDist, float circleRadius, float jitter,
            float deltaSeconds);
Vec3 obstacleAvoid(const SteeringAgent& agent, const std::vector<math::Sphere>& obstacles,
                   float lookAhead);
void integrate(SteeringAgent& agent, const Vec3& steeringForce, float deltaSeconds);

}  // namespace steering

// ---- Blackboard ----------------------------------------------------------

class Blackboard {
 public:
  void setFloat(const std::string& key, float value) { floats_[key] = value; }
  void setInt(const std::string& key, int32_t value) { ints_[key] = value; }
  void setBool(const std::string& key, bool value) { bools_[key] = value; }
  void setVector(const std::string& key, const Vec3& value) { vectors_[key] = value; }
  float getFloat(const std::string& key, float fallback = 0.0f) const {
    auto it = floats_.find(key);
    return it == floats_.end() ? fallback : it->second;
  }
  int32_t getInt(const std::string& key, int32_t fallback = 0) const {
    auto it = ints_.find(key);
    return it == ints_.end() ? fallback : it->second;
  }
  bool getBool(const std::string& key, bool fallback = false) const {
    auto it = bools_.find(key);
    return it == bools_.end() ? fallback : it->second;
  }
  Vec3 getVector(const std::string& key, const Vec3& fallback = {0, 0, 0}) const {
    auto it = vectors_.find(key);
    return it == vectors_.end() ? fallback : it->second;
  }
  void remove(const std::string& key) {
    floats_.erase(key);
    ints_.erase(key);
    bools_.erase(key);
    vectors_.erase(key);
  }

 private:
  std::unordered_map<std::string, float> floats_;
  std::unordered_map<std::string, int32_t> ints_;
  std::unordered_map<std::string, bool> bools_;
  std::unordered_map<std::string, Vec3> vectors_;
};

// ---- Perception ----------------------------------------------------------

struct Perception {
  Vec3 position{0, 0, 0};
  float sightRange = 20.0f;
  float sightAngle = 120.0f;
  Vec3 forward{0, 0, -1};
  std::vector<ecs::Entity> visible;
};

bool canSee(const Perception& observer, const Vec3& target);

// ---- Navmesh / pathfinding ----------------------------------------------

struct NavTriangle {
  Vec3 a{0, 0, 0};
  Vec3 b{0, 0, 0};
  Vec3 c{0, 0, 0};
  int32_t neighbours[3] = {-1, -1, -1};
};

class NavMesh {
 public:
  void clear() { triangles_.clear(); }
  void addTriangle(const NavTriangle& triangle) { triangles_.push_back(triangle); }
  uint32_t triangleCount() const { return static_cast<uint32_t>(triangles_.size()); }
  const std::vector<NavTriangle>& triangles() const { return triangles_; }

  // Builds a walkable navigation mesh over a (width + 1) x (depth + 1) heightfield
  // grid (or flat plane when `heights` is empty), filtering out triangles
  // steeper than maxSlopeCos and automatically linking shared edges.
  bool buildFromGrid(uint32_t width, uint32_t depth, float cellSize,
                     const std::vector<float>& heights, float maxSlopeCos = 0.7f,
                     const Vec3& origin = {0, 0, 0});

  // A* over triangle adjacency followed by 2D XZ funnel string-pulling.
  std::vector<Vec3> findPath(const Vec3& start, const Vec3& goal) const;
  Vec3 closestPointOnMesh(const Vec3& point) const;

 private:
  std::vector<NavTriangle> triangles_;
  int32_t findContainingTriangle(const Vec3& point) const;
};

// ---- GOAP ----------------------------------------------------------------

struct WorldState {
  std::unordered_map<std::string, bool> values;
  bool matches(const WorldState& conditions) const;
  void apply(const WorldState& effects);
};

struct GoapAction {
  std::string name;
  WorldState preconditions;
  WorldState effects;
  float cost = 1.0f;
};

// A* over world states. Returns action names in execution order.
std::vector<std::string> planGoap(const WorldState& start, const WorldState& goal,
                                  const std::vector<GoapAction>& actions);

}  // namespace nimbus::ai
