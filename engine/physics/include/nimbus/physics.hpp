// Nimbus physics: AABB/OBB/sphere/capsule colliders, broadphase, narrowphase,
// layers/masks, triggers/callbacks, joints, swept-sphere CCD, and terrain heightfields.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "nimbus/ecs.hpp"
#include "nimbus/math.hpp"

namespace nimbus::scene { class Scene; }

namespace nimbus::physics {

using math::Vec3;

struct Aabb {
  Vec3 min{0, 0, 0};
  Vec3 max{0, 0, 0};
};

struct Sphere {
  Vec3 center{0, 0, 0};
  float radius = 0.0f;
};

struct Capsule {
  Vec3 a{0, -0.5f, 0};
  Vec3 b{0, 0.5f, 0};
  float radius = 0.25f;
};

struct Ray {
  Vec3 origin{0, 0, 0};
  Vec3 direction{0, 0, 1};
};

struct RayHit {
  bool hit = false;
  float distance = 0.0f;
  Vec3 point{0, 0, 0};
  Vec3 normal{0, 0, 0};
  ecs::Entity entity = ecs::kNullEntity;
};

struct Contact {
  ecs::Entity a = ecs::kNullEntity;
  ecs::Entity b = ecs::kNullEntity;
  Vec3 point{0, 0, 0};
  Vec3 normal{0, 0, 0};
  float penetration = 0.0f;
  bool trigger = false;
};

struct RigidBody {
  Vec3 velocity{0, 0, 0};
  Vec3 angularVelocity{0, 0, 0};
  Vec3 force{0, 0, 0};
  Vec3 torque{0, 0, 0};
  float mass = 1.0f;
  float inverseMass = 1.0f;
  float restitution = 0.2f;
  float friction = 0.5f;
  bool kinematic = false;
  bool useGravity = true;
  bool sleeping = false;
};

struct BoxCollider {
  Vec3 halfExtents{0.5f, 0.5f, 0.5f};
};

struct SphereCollider {
  float radius = 0.5f;
};

struct CapsuleCollider {
  float radius = 0.25f;
  float height = 1.0f;
};

struct Collider {
  enum class Type : uint8_t { Box, Sphere, Capsule };
  Type type = Type::Box;
  bool trigger = false;
  Vec3 offset{0, 0, 0};
  uint32_t layer = 1u;
  uint32_t mask = 0xFFFFFFFFu;
};

struct DistanceJoint {
  ecs::Entity a = ecs::kNullEntity;
  ecs::Entity b = ecs::kNullEntity;
  float restLength = 1.0f;
  float stiffness = 1.0f;
};

struct SpringJoint {
  ecs::Entity a = ecs::kNullEntity;
  ecs::Entity b = ecs::kNullEntity;
  float restLength = 1.0f;
  float springConstant = 50.0f;
  float damping = 4.0f;
};

class Broadphase {
 public:
  void update(const std::vector<ecs::Entity>& bodies,
              const std::function<Aabb(ecs::Entity)>& bounds);
  const std::vector<std::pair<ecs::Entity, ecs::Entity>>& pairs() const { return pairs_; }
  void clear() { pairs_.clear(); }

 private:
  std::vector<std::pair<ecs::Entity, ecs::Entity>> pairs_;
  std::vector<std::pair<ecs::Entity, Aabb>> entries_;
};

// Narrowphase checks.
bool intersectSphereSphere(const Sphere& a, const Sphere& b, Contact& out);
bool intersectBoxBox(const Aabb& a, const Aabb& b, Contact& out);
bool intersectSphereBox(const Sphere& s, const Aabb& b, Contact& out);
bool intersectCapsuleSphere(const Capsule& c, const Sphere& s, Contact& out);
bool intersectCapsuleCapsule(const Capsule& a, const Capsule& b, Contact& out);
bool intersectCapsuleBox(const Capsule& c, const Aabb& b, Contact& out);
bool raycastAabb(const Ray& ray, const Aabb& box, RayHit& out);
bool raycastSphere(const Ray& ray, const Sphere& sphere, RayHit& out);
bool raycastCapsule(const Ray& ray, const Capsule& capsule, RayHit& out);

// Solver: semi-implicit Euler integration, impulse + joint solver, CCD, and terrain.
class World {
 public:
  using ContactCallback = std::function<void(const Contact&)>;
  using HeightSampler = std::function<float(float x, float z)>;
  using NormalSampler = std::function<Vec3(float x, float z)>;

  explicit World(ecs::World* ecs, scene::Scene* scene = nullptr) : ecs_(ecs), scene_(scene) {}

  void step(float deltaSeconds);
  void setGravity(const Vec3& gravity) { gravity_ = gravity; }
  const Vec3& gravity() const { return gravity_; }

  void setOnCollision(ContactCallback cb) { onCollision_ = std::move(cb); }
  void setOnTrigger(ContactCallback cb) { onTrigger_ = std::move(cb); }

  void addDistanceJoint(const DistanceJoint& joint) { distanceJoints_.push_back(joint); }
  void addSpringJoint(const SpringJoint& joint) { springJoints_.push_back(joint); }
  void clearJoints() {
    distanceJoints_.clear();
    springJoints_.clear();
  }

  void setTerrainSampler(float minX, float maxX, float minZ, float maxZ,
                         HeightSampler heightFn, NormalSampler normalFn = {}) {
    terrainEnabled_ = static_cast<bool>(heightFn);
    terrainMinX_ = minX;
    terrainMaxX_ = maxX;
    terrainMinZ_ = minZ;
    terrainMaxZ_ = maxZ;
    terrainHeight_ = std::move(heightFn);
    terrainNormal_ = std::move(normalFn);
  }
  void clearTerrainSampler() {
    terrainEnabled_ = false;
    terrainHeight_ = nullptr;
    terrainNormal_ = nullptr;
  }

  RayHit raycast(const Ray& ray, float maxDistance = 1000.0f, uint32_t layerMask = 0xFFFFFFFFu);
  RayHit sweepSphere(const Sphere& sphere, const Vec3& direction, float maxDistance,
                     uint32_t layerMask = 0xFFFFFFFFu);

  uint32_t contactCount() const { return static_cast<uint32_t>(contacts_.size()); }
  const std::vector<Contact>& contacts() const { return contacts_; }

  ecs::World* ecs() { return ecs_; }

 private:
  void integrate(float deltaSeconds);
  void applySpringJoints(float deltaSeconds);
  void resolveContacts(float deltaSeconds);
  void resolveDistanceJoints();
  void resolveTerrain();

  ecs::World* ecs_;
  scene::Scene* scene_;
  Vec3 gravity_{0, -9.81f, 0};
  Broadphase broadphase_;
  std::vector<Contact> contacts_;
  std::vector<DistanceJoint> distanceJoints_;
  std::vector<SpringJoint> springJoints_;
  ContactCallback onCollision_;
  ContactCallback onTrigger_;

  bool terrainEnabled_ = false;
  float terrainMinX_ = 0.0f;
  float terrainMaxX_ = 0.0f;
  float terrainMinZ_ = 0.0f;
  float terrainMaxZ_ = 0.0f;
  HeightSampler terrainHeight_;
  NormalSampler terrainNormal_;

  uint32_t velocityIterations_ = 4;
  uint32_t positionIterations_ = 2;
};

}  // namespace nimbus::physics
