#include "nimbus/physics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

#include "nimbus/scene.hpp"

namespace nimbus::physics {
namespace {

float componentAbs(float value) { return std::fabs(value); }
Vec3 absolute(const Vec3& v) {
  return {componentAbs(v.x), componentAbs(v.y), componentAbs(v.z)};
}
float maxComponent(const Vec3& v) { return std::max(v.x, std::max(v.y, v.z)); }

struct ShapeState {
  Collider collider{};
  scene::Transform* transform = nullptr;
  RigidBody* body = nullptr;
  Aabb bounds{};
  Sphere sphere{};
  Capsule capsule{};
};

Aabb boundsFor(const ShapeState& shape) { return shape.bounds; }

bool layersCompatible(const Collider& a, const Collider& b) {
  return ((a.layer & b.mask) != 0u) && ((b.layer & a.mask) != 0u);
}

bool collide(const ShapeState& a, const ShapeState& b, Contact& contact) {
  const auto ta = a.collider.type;
  const auto tb = b.collider.type;
  if (ta == Collider::Type::Sphere && tb == Collider::Type::Sphere) {
    return intersectSphereSphere(a.sphere, b.sphere, contact);
  }
  if (ta == Collider::Type::Box && tb == Collider::Type::Box) {
    return intersectBoxBox(a.bounds, b.bounds, contact);
  }
  if (ta == Collider::Type::Capsule && tb == Collider::Type::Capsule) {
    return intersectCapsuleCapsule(a.capsule, b.capsule, contact);
  }
  if (ta == Collider::Type::Sphere && tb == Collider::Type::Box) {
    if (!intersectSphereBox(a.sphere, b.bounds, contact)) return false;
    contact.normal = contact.normal * -1.0f;
    return true;
  }
  if (ta == Collider::Type::Box && tb == Collider::Type::Sphere) {
    return intersectSphereBox(b.sphere, a.bounds, contact);
  }
  if (ta == Collider::Type::Capsule && tb == Collider::Type::Sphere) {
    return intersectCapsuleSphere(a.capsule, b.sphere, contact);
  }
  if (ta == Collider::Type::Sphere && tb == Collider::Type::Capsule) {
    if (!intersectCapsuleSphere(b.capsule, a.sphere, contact)) return false;
    contact.normal = contact.normal * -1.0f;
    return true;
  }
  if (ta == Collider::Type::Capsule && tb == Collider::Type::Box) {
    return intersectCapsuleBox(a.capsule, b.bounds, contact);
  }
  if (ta == Collider::Type::Box && tb == Collider::Type::Capsule) {
    if (!intersectCapsuleBox(b.capsule, a.bounds, contact)) return false;
    contact.normal = contact.normal * -1.0f;
    return true;
  }
  return intersectBoxBox(a.bounds, b.bounds, contact);
}

float inverseMass(const RigidBody* body) {
  if (!body || body->kinematic || body->mass <= 0.0f || !std::isfinite(body->mass)) return 0.0f;
  return 1.0f / body->mass;
}

ShapeState buildShapeState(const Collider* collider, scene::Transform* transform, RigidBody* body,
                           ecs::World* ecs, ecs::Entity entity) {
  ShapeState state;
  state.collider = *collider;
  state.transform = transform;
  state.body = body;
  const Vec3 center = transform->position + collider->offset;
  const Vec3 scale = absolute(transform->scale);
  if (collider->type == Collider::Type::Sphere) {
    const SphereCollider* sphere = ecs->get<SphereCollider>(entity);
    const float radius = std::max(0.0f, (sphere ? sphere->radius : 0.5f) * maxComponent(scale));
    state.sphere = {center, radius};
    const Vec3 ext{radius, radius, radius};
    state.bounds = {center - ext, center + ext};
  } else if (collider->type == Collider::Type::Capsule) {
    const CapsuleCollider* cap = ecs->get<CapsuleCollider>(entity);
    const float radius = std::max(0.0f, (cap ? cap->radius : 0.25f) * std::max(scale.x, scale.z));
    const float height = std::max(radius * 2.0f, (cap ? cap->height : 1.0f) * scale.y);
    const float halfSeg = std::max(0.0f, height * 0.5f - radius);
    const Vec3 up = math::normalize(math::rotate(transform->rotation, Vec3{0, 1, 0}));
    state.capsule = {center - up * halfSeg, center + up * halfSeg, radius};
    const Vec3 minSeg{std::min(state.capsule.a.x, state.capsule.b.x),
                      std::min(state.capsule.a.y, state.capsule.b.y),
                      std::min(state.capsule.a.z, state.capsule.b.z)};
    const Vec3 maxSeg{std::max(state.capsule.a.x, state.capsule.b.x),
                      std::max(state.capsule.a.y, state.capsule.b.y),
                      std::max(state.capsule.a.z, state.capsule.b.z)};
    const Vec3 ext{radius, radius, radius};
    state.bounds = {minSeg - ext, maxSeg + ext};
  } else {
    Vec3 half{0.5f * scale.x, 0.5f * scale.y, 0.5f * scale.z};
    if (const BoxCollider* box = ecs->get<BoxCollider>(entity))
      half = absolute(box->halfExtents) * scale;
    state.bounds = {center - half, center + half};
  }
  return state;
}

}  // namespace

void Broadphase::update(const std::vector<ecs::Entity>& bodies,
                        const std::function<Aabb(ecs::Entity)>& bounds) {
  entries_.clear();
  entries_.reserve(bodies.size());
  for (ecs::Entity e : bodies) entries_.push_back({e, bounds(e)});
  pairs_.clear();
  for (size_t i = 0; i < entries_.size(); ++i) {
    for (size_t j = i + 1; j < entries_.size(); ++j) {
      const Aabb& a = entries_[i].second;
      const Aabb& b = entries_[j].second;
      if (a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y &&
          a.max.y >= b.min.y && a.min.z <= b.max.z && a.max.z >= b.min.z) {
        pairs_.emplace_back(entries_[i].first, entries_[j].first);
      }
    }
  }
}

bool intersectSphereSphere(const Sphere& a, const Sphere& b, Contact& out) {
  const Vec3 delta = b.center - a.center;
  const float distanceSquared = math::dot(delta, delta);
  const float radiusSum = a.radius + b.radius;
  if (distanceSquared > radiusSum * radiusSum) return false;
  const float distance = std::sqrt(std::max(0.0f, distanceSquared));
  out.normal = distance > 1e-6f ? delta * (1.0f / distance) : Vec3{0, 1, 0};
  out.point = a.center + out.normal * (a.radius - distance * 0.5f);
  out.penetration = radiusSum - distance;
  return true;
}

bool intersectSphereBox(const Sphere& s, const Aabb& b, Contact& out) {
  const Vec3 closest = {
      math::clamp(s.center.x, b.min.x, b.max.x),
      math::clamp(s.center.y, b.min.y, b.max.y),
      math::clamp(s.center.z, b.min.z, b.max.z),
  };
  const Vec3 delta = s.center - closest;
  const float distanceSquared = math::dot(delta, delta);
  if (distanceSquared > s.radius * s.radius) return false;
  const float distance = std::sqrt(std::max(0.0f, distanceSquared));
  out.normal = distance > 1e-6f ? delta * (1.0f / distance) : Vec3{0, 1, 0};
  out.point = closest;
  out.penetration = s.radius - distance;
  return true;
}

bool intersectBoxBox(const Aabb& a, const Aabb& b, Contact& out) {
  if (!(a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y &&
        a.max.y >= b.min.y && a.min.z <= b.max.z && a.max.z >= b.min.z))
    return false;
  const Vec3 overlap = {
      std::min(a.max.x, b.max.x) - std::max(a.min.x, b.min.x),
      std::min(a.max.y, b.max.y) - std::max(a.min.y, b.min.y),
      std::min(a.max.z, b.max.z) - std::max(a.min.z, b.min.z),
  };
  int axis = 0;
  if (overlap.y < overlap.x) axis = 1;
  if (overlap.z < (axis == 0 ? overlap.x : overlap.y)) axis = 2;
  const Vec3 centerA = (a.min + a.max) * 0.5f;
  const Vec3 centerB = (b.min + b.max) * 0.5f;
  Vec3 normal{};
  normal[axis] = centerB[axis] >= centerA[axis] ? 1.0f : -1.0f;
  out.normal = normal;
  out.penetration = overlap[axis];
  out.point = (centerA + centerB) * 0.5f;
  return true;
}

bool intersectCapsuleSphere(const Capsule& c, const Sphere& s, Contact& out) {
  const Vec3 closest = math::closestPointOnSegment(s.center, c.a, c.b);
  return intersectSphereSphere({closest, c.radius}, s, out);
}

bool intersectCapsuleCapsule(const Capsule& a, const Capsule& b, Contact& out) {
  Vec3 c1{}, c2{};
  math::closestPointsBetweenSegments(a.a, a.b, b.a, b.b, c1, c2);
  return intersectSphereSphere({c1, a.radius}, {c2, b.radius}, out);
}

bool intersectCapsuleBox(const Capsule& c, const Aabb& b, Contact& out) {
  // Sample segment closest candidate against the AABB using center + endpoints + iterative clamp.
  const Vec3 seg = c.b - c.a;
  const Vec3 boxCenter = (b.min + b.max) * 0.5f;
  Vec3 segPoint = math::closestPointOnSegment(boxCenter, c.a, c.b);
  for (int iter = 0; iter < 3; ++iter) {
    const Vec3 onBox{math::clamp(segPoint.x, b.min.x, b.max.x),
                     math::clamp(segPoint.y, b.min.y, b.max.y),
                     math::clamp(segPoint.z, b.min.z, b.max.z)};
    segPoint = math::closestPointOnSegment(onBox, c.a, c.b);
  }
  Contact sphereContact;
  if (!intersectSphereBox({segPoint, c.radius}, b, sphereContact)) {
    // Also check endpoints in case box spans outside center projection.
    const Vec3 candidates[2] = {c.a, c.a + seg};
    bool hit = false;
    float bestPen = -1.0f;
    for (const Vec3& p : candidates) {
      Contact trial;
      if (intersectSphereBox({p, c.radius}, b, trial) && trial.penetration > bestPen) {
        bestPen = trial.penetration;
        sphereContact = trial;
        hit = true;
      }
    }
    if (!hit) return false;
  }
  out.point = sphereContact.point;
  out.normal = sphereContact.normal * -1.0f;  // from capsule (a) to box (b)
  out.penetration = sphereContact.penetration;
  return true;
}

bool raycastAabb(const Ray& ray, const Aabb& box, RayHit& out) {
  float tmin = 0.0f;
  float tmax = std::numeric_limits<float>::max();
  Vec3 nearNormal{};
  for (int i = 0; i < 3; ++i) {
    const float origin = ray.origin[i];
    const float dir = ray.direction[i];
    if (std::fabs(dir) < 1e-8f) {
      if (origin < box.min[i] || origin > box.max[i]) return false;
      continue;
    }
    float t1 = (box.min[i] - origin) / dir;
    float t2 = (box.max[i] - origin) / dir;
    float sign = -1.0f;
    if (t1 > t2) { std::swap(t1, t2); sign = 1.0f; }
    if (t1 > tmin) {
      tmin = t1;
      nearNormal = Vec3{};
      nearNormal[i] = sign;
    }
    tmax = std::min(tmax, t2);
    if (tmin > tmax) return false;
  }
  out.hit = true;
  out.distance = tmin;
  out.point = ray.origin + ray.direction * tmin;
  out.normal = math::length(nearNormal) > 0.0f ? nearNormal : math::normalize(ray.direction) * -1.0f;
  return true;
}

bool raycastSphere(const Ray& ray, const Sphere& sphere, RayHit& out) {
  const Vec3 direction = math::normalize(ray.direction);
  if (math::length(direction) < 1e-8f || sphere.radius < 0.0f) return false;
  const Vec3 oc = ray.origin - sphere.center;
  const float b = math::dot(oc, direction);
  const float c = math::dot(oc, oc) - sphere.radius * sphere.radius;
  const float discriminant = b * b - c;
  if (discriminant < 0.0f) return false;
  float t = -b - std::sqrt(discriminant);
  if (t < 0.0f) t = -b + std::sqrt(discriminant);
  if (t < 0.0f) return false;
  out.hit = true;
  out.distance = t;
  out.point = ray.origin + direction * t;
  out.normal = math::normalize(out.point - sphere.center);
  return true;
}

bool raycastCapsule(const Ray& ray, const Capsule& capsule, RayHit& out) {
  const Vec3 direction = math::normalize(ray.direction);
  if (math::length(direction) < 1e-8f) return false;
  // Iterative closest-sphere raycast along the capsule spine.
  const float span = math::distance(capsule.a, capsule.b);
  const uint32_t steps = std::max<uint32_t>(4u, static_cast<uint32_t>(span / std::max(0.05f, capsule.radius)) + 2u);
  RayHit best{};
  best.distance = std::numeric_limits<float>::max();
  for (uint32_t i = 0; i <= steps; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(steps);
    const Vec3 c = math::lerp(capsule.a, capsule.b, t);
    RayHit hit{};
    if (raycastSphere({ray.origin, direction}, {c, capsule.radius}, hit) && hit.distance < best.distance) {
      best = hit;
    }
  }
  if (!best.hit) return false;
  out = best;
  return true;
}

void World::integrate(float deltaSeconds) {
  if (!ecs_ || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) return;
  for (uint32_t archetypeId : ecs_->query<RigidBody>()) {
    const ecs::Archetype& archetype = ecs_->archetype(archetypeId);
    for (ecs::Entity entity : archetype.entities) {
      RigidBody* body = ecs_->get<RigidBody>(entity);
      if (!body || body->sleeping) continue;
      const float invMass = inverseMass(body);
      body->inverseMass = invMass;
      if (!body->kinematic && invMass > 0.0f) {
        Vec3 acceleration = body->force * invMass;
        if (body->useGravity) acceleration += gravity_;
        body->velocity += acceleration * deltaSeconds;
      }
      scene::Transform* transform = scene_ ? scene_->local(entity) : ecs_->get<scene::Transform>(entity);
      if (transform && (body->kinematic || invMass > 0.0f)) {
        transform->position += body->velocity * deltaSeconds;
        if (scene_) scene_->markDirty(entity);
      }
      body->force = Vec3{};
      body->torque = Vec3{};
    }
  }
}

void World::applySpringJoints(float deltaSeconds) {
  if (!ecs_) return;
  for (const SpringJoint& joint : springJoints_) {
    scene::Transform* ta = scene_ ? scene_->local(joint.a) : ecs_->get<scene::Transform>(joint.a);
    scene::Transform* tb = scene_ ? scene_->local(joint.b) : ecs_->get<scene::Transform>(joint.b);
    if (!ta || !tb) continue;
    RigidBody* ba = ecs_->get<RigidBody>(joint.a);
    RigidBody* bb = ecs_->get<RigidBody>(joint.b);
    const Vec3 delta = tb->position - ta->position;
    const float dist = math::length(delta);
    if (dist < 1e-6f) continue;
    const Vec3 dir = delta * (1.0f / dist);
    const float extension = dist - joint.restLength;
    const Vec3 relVel = (bb ? bb->velocity : Vec3{}) - (ba ? ba->velocity : Vec3{});
    const float forceMag = joint.springConstant * extension + joint.damping * math::dot(relVel, dir);
    const Vec3 f = dir * forceMag;
    if (ba && !ba->kinematic && ba->mass > 0.0f) ba->velocity += f * (deltaSeconds / ba->mass);
    if (bb && !bb->kinematic && bb->mass > 0.0f) bb->velocity -= f * (deltaSeconds / bb->mass);
  }
}

void World::resolveDistanceJoints() {
  if (!ecs_) return;
  for (const DistanceJoint& joint : distanceJoints_) {
    scene::Transform* ta = scene_ ? scene_->local(joint.a) : ecs_->get<scene::Transform>(joint.a);
    scene::Transform* tb = scene_ ? scene_->local(joint.b) : ecs_->get<scene::Transform>(joint.b);
    if (!ta || !tb) continue;
    RigidBody* ba = ecs_->get<RigidBody>(joint.a);
    RigidBody* bb = ecs_->get<RigidBody>(joint.b);
    const float invA = inverseMass(ba);
    const float invB = inverseMass(bb);
    const float invSum = invA + invB;
    if (invSum <= 0.0f) continue;
    const Vec3 delta = tb->position - ta->position;
    const float dist = math::length(delta);
    if (dist < 1e-6f) continue;
    const float err = (dist - joint.restLength) * math::saturate(joint.stiffness);
    const Vec3 correction = delta * (err / (dist * invSum));
    if (invA > 0.0f) {
      ta->position += correction * invA;
      if (scene_) scene_->markDirty(joint.a);
    }
    if (invB > 0.0f) {
      tb->position -= correction * invB;
      if (scene_) scene_->markDirty(joint.b);
    }
  }
}

void World::resolveTerrain() {
  if (!terrainEnabled_ || !terrainHeight_ || !ecs_) return;
  for (uint32_t archetypeId : ecs_->query<Collider>()) {
    const ecs::Archetype& archetype = ecs_->archetype(archetypeId);
    for (ecs::Entity entity : archetype.entities) {
      const Collider* collider = ecs_->get<Collider>(entity);
      scene::Transform* transform = scene_ ? scene_->local(entity) : ecs_->get<scene::Transform>(entity);
      RigidBody* body = ecs_->get<RigidBody>(entity);
      if (!collider || !transform || collider->trigger) continue;
      const float inv = inverseMass(body);
      if (inv <= 0.0f) continue;
      const Vec3 pos = transform->position + collider->offset;
      if (pos.x < terrainMinX_ || pos.x > terrainMaxX_ ||
          pos.z < terrainMinZ_ || pos.z > terrainMaxZ_)
        continue;
      const ShapeState shape = buildShapeState(collider, transform, body, ecs_, entity);
      const float bottomOffset = (pos.y - shape.bounds.min.y);
      const float groundY = terrainHeight_(pos.x, pos.z);
      const float entityBottom = pos.y - bottomOffset;
      if (entityBottom < groundY) {
        const float pen = groundY - entityBottom;
        transform->position.y += pen;
        if (scene_) scene_->markDirty(entity);
        const Vec3 normal = terrainNormal_ ? terrainNormal_(pos.x, pos.z) : Vec3{0, 1, 0};
        if (body) {
          const float vn = math::dot(body->velocity, normal);
          if (vn < 0.0f) {
            const float rest = math::saturate(body->restitution);
            body->velocity -= normal * ((1.0f + rest) * vn);
            // Tangential ground damping
            const Vec3 tang = body->velocity - normal * math::dot(body->velocity, normal);
            body->velocity -= tang * math::clamp(body->friction * 0.2f, 0.0f, 1.0f);
          }
        }
        Contact c;
        c.a = entity;
        c.b = ecs::kNullEntity;
        c.point = {pos.x, groundY, pos.z};
        c.normal = normal;
        c.penetration = pen;
        contacts_.push_back(c);
        if (onCollision_) onCollision_(c);
      }
    }
  }
}

void World::resolveContacts(float deltaSeconds) {
  contacts_.clear();
  if (!ecs_) return;
  std::unordered_map<ecs::Entity, ShapeState> shapes;
  std::vector<ecs::Entity> entities;
  for (uint32_t archetypeId : ecs_->query<Collider>()) {
    const ecs::Archetype& archetype = ecs_->archetype(archetypeId);
    for (ecs::Entity entity : archetype.entities) {
      const Collider* collider = ecs_->get<Collider>(entity);
      scene::Transform* transform = scene_ ? scene_->local(entity) : ecs_->get<scene::Transform>(entity);
      if (!collider || !transform) continue;
      RigidBody* body = ecs_->get<RigidBody>(entity);
      shapes.emplace(entity, buildShapeState(collider, transform, body, ecs_, entity));
      entities.push_back(entity);
    }
  }

  broadphase_.update(entities, [&](ecs::Entity entity) { return boundsFor(shapes.at(entity)); });
  contacts_.reserve(broadphase_.pairs().size());
  for (const auto& pair : broadphase_.pairs()) {
    const ShapeState& a = shapes.at(pair.first);
    const ShapeState& b = shapes.at(pair.second);
    if (!layersCompatible(a.collider, b.collider)) continue;
    const float invA = inverseMass(a.body);
    const float invB = inverseMass(b.body);
    const bool isTrigger = a.collider.trigger || b.collider.trigger;
    if (invA + invB <= 0.0f && !isTrigger) continue;
    Contact contact;
    contact.a = pair.first;
    contact.b = pair.second;
    contact.trigger = isTrigger;
    if (collide(a, b, contact)) {
      contacts_.push_back(contact);
      if (isTrigger) {
        if (onTrigger_) onTrigger_(contact);
      } else {
        if (onCollision_) onCollision_(contact);
      }
    }
  }

  const uint32_t velocityIterations = std::max(1u, velocityIterations_);
  for (uint32_t iteration = 0; iteration < velocityIterations; ++iteration) {
    for (const Contact& contact : contacts_) {
      if (contact.trigger || contact.b == ecs::kNullEntity) continue;
      ShapeState& a = shapes.at(contact.a);
      ShapeState& b = shapes.at(contact.b);
      const float invA = inverseMass(a.body);
      const float invB = inverseMass(b.body);
      const float inverseSum = invA + invB;
      if (inverseSum <= 0.0f) continue;

      const Vec3 velocityA = a.body ? a.body->velocity : Vec3{};
      const Vec3 velocityB = b.body ? b.body->velocity : Vec3{};
      const Vec3 relativeVelocity = velocityB - velocityA;
      const float alongNormal = math::dot(relativeVelocity, contact.normal);
      if (alongNormal >= 0.0f) continue;

      const float restitutionA = a.body && std::isfinite(a.body->restitution)
          ? math::saturate(a.body->restitution) : 0.0f;
      const float restitutionB = b.body && std::isfinite(b.body->restitution)
          ? math::saturate(b.body->restitution) : 0.0f;
      const float restitution = std::min(restitutionA, restitutionB);
      const float normalImpulseMagnitude =
          -(1.0f + restitution) * alongNormal / inverseSum;
      const Vec3 normalImpulse = contact.normal * normalImpulseMagnitude;
      if (a.body && invA > 0.0f) a.body->velocity -= normalImpulse * invA;
      if (b.body && invB > 0.0f) b.body->velocity += normalImpulse * invB;

      const Vec3 updatedA = a.body ? a.body->velocity : Vec3{};
      const Vec3 updatedB = b.body ? b.body->velocity : Vec3{};
      const Vec3 tangentVelocity = (updatedB - updatedA) -
          contact.normal * math::dot(updatedB - updatedA, contact.normal);
      const float tangentLength = math::length(tangentVelocity);
      if (tangentLength > 1.0e-6f) {
        const Vec3 tangent = tangentVelocity * (1.0f / tangentLength);
        const float frictionA = a.body && std::isfinite(a.body->friction)
            ? std::max(0.0f, a.body->friction) : 0.5f;
        const float frictionB = b.body && std::isfinite(b.body->friction)
            ? std::max(0.0f, b.body->friction) : 0.5f;
        const float friction = std::sqrt(frictionA * frictionB);
        float tangentImpulseMagnitude = -math::dot(updatedB - updatedA, tangent) / inverseSum;
        const float maxFrictionImpulse = normalImpulseMagnitude * friction;
        tangentImpulseMagnitude = math::clamp(tangentImpulseMagnitude,
                                               -maxFrictionImpulse, maxFrictionImpulse);
        const Vec3 tangentImpulse = tangent * tangentImpulseMagnitude;
        if (a.body && invA > 0.0f) a.body->velocity -= tangentImpulse * invA;
        if (b.body && invB > 0.0f) b.body->velocity += tangentImpulse * invB;
      }
    }
  }

  const uint32_t positionIterations = std::max(1u, positionIterations_);
  for (uint32_t iteration = 0; iteration < positionIterations; ++iteration) {
    for (const Contact& contact : contacts_) {
      if (contact.trigger || contact.b == ecs::kNullEntity) continue;
      ShapeState& a = shapes.at(contact.a);
      ShapeState& b = shapes.at(contact.b);
      const float invA = inverseMass(a.body);
      const float invB = inverseMass(b.body);
      const float inverseSum = invA + invB;
      if (inverseSum <= 0.0f) continue;
      const float correctionMagnitude =
          std::max(contact.penetration - 0.001f, 0.0f) * 0.8f /
          (inverseSum * static_cast<float>(positionIterations));
      const Vec3 correction = contact.normal * correctionMagnitude;
      if (a.transform && invA > 0.0f) {
        a.transform->position -= correction * invA;
        if (scene_) scene_->markDirty(contact.a);
      }
      if (b.transform && invB > 0.0f) {
        b.transform->position += correction * invB;
        if (scene_) scene_->markDirty(contact.b);
      }
    }
    resolveDistanceJoints();
  }
  resolveTerrain();
  (void)deltaSeconds;
}

void World::step(float deltaSeconds) {
  if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) return;
  applySpringJoints(deltaSeconds);
  integrate(deltaSeconds);
  resolveContacts(deltaSeconds);
}

RayHit World::raycast(const Ray& inputRay, float maxDistance, uint32_t layerMask) {
  RayHit best{};
  best.distance = std::max(0.0f, maxDistance);
  if (!ecs_ || best.distance <= 0.0f) return best;
  const Vec3 direction = math::normalize(inputRay.direction);
  if (math::length(direction) < 1e-8f) return best;
  const Ray ray{inputRay.origin, direction};
  for (uint32_t archetypeId : ecs_->query<Collider>()) {
    const ecs::Archetype& archetype = ecs_->archetype(archetypeId);
    for (ecs::Entity entity : archetype.entities) {
      const Collider* collider = ecs_->get<Collider>(entity);
      scene::Transform* transform = scene_ ? scene_->local(entity) : ecs_->get<scene::Transform>(entity);
      if (!collider || !transform || (collider->layer & layerMask) == 0u) continue;
      const ShapeState shape = buildShapeState(collider, transform, nullptr, ecs_, entity);
      RayHit hit{};
      bool didHit = false;
      if (collider->type == Collider::Type::Sphere) {
        didHit = raycastSphere(ray, shape.sphere, hit);
      } else if (collider->type == Collider::Type::Capsule) {
        didHit = raycastCapsule(ray, shape.capsule, hit);
      } else {
        didHit = raycastAabb(ray, shape.bounds, hit);
      }
      if (didHit && hit.distance <= best.distance) {
        best = hit;
        best.entity = entity;
      }
    }
  }
  return best;
}

RayHit World::sweepSphere(const Sphere& sphere, const Vec3& direction, float maxDistance,
                          uint32_t layerMask) {
  RayHit best{};
  best.distance = std::max(0.0f, maxDistance);
  if (!ecs_ || best.distance <= 0.0f || sphere.radius < 0.0f) return best;
  const Vec3 dir = math::normalize(direction);
  if (math::length(dir) < 1e-8f) return best;
  const Ray ray{sphere.center, dir};
  for (uint32_t archetypeId : ecs_->query<Collider>()) {
    const ecs::Archetype& archetype = ecs_->archetype(archetypeId);
    for (ecs::Entity entity : archetype.entities) {
      const Collider* collider = ecs_->get<Collider>(entity);
      scene::Transform* transform = scene_ ? scene_->local(entity) : ecs_->get<scene::Transform>(entity);
      if (!collider || !transform || (collider->layer & layerMask) == 0u) continue;
      const ShapeState shape = buildShapeState(collider, transform, nullptr, ecs_, entity);
      RayHit hit{};
      bool didHit = false;
      if (collider->type == Collider::Type::Sphere) {
        didHit = raycastSphere(ray, {shape.sphere.center, shape.sphere.radius + sphere.radius}, hit);
      } else if (collider->type == Collider::Type::Capsule) {
        didHit = raycastCapsule(ray, {shape.capsule.a, shape.capsule.b,
                                      shape.capsule.radius + sphere.radius}, hit);
      } else {
        const Vec3 pad{sphere.radius, sphere.radius, sphere.radius};
        didHit = raycastAabb(ray, {shape.bounds.min - pad, shape.bounds.max + pad}, hit);
      }
      if (didHit && hit.distance <= best.distance) {
        best = hit;
        best.entity = entity;
      }
    }
  }
  return best;
}

}  // namespace nimbus::physics
