// Nimbus particles: CPU-simulated particle pool with typed SoA storage, emitter shapes, and collision.
#pragma once

#include <cstdint>
#include <vector>

#include "nimbus/math.hpp"

namespace nimbus::particles {

using math::Vec3;

enum class BlendMode : uint8_t { Additive, Alpha, Opaque };
enum class EmitterShape : uint8_t { Point, Sphere, Box, Cone };

struct Particle {
  Vec3 position{0, 0, 0};
  Vec3 velocity{0, 0, 0};
  Vec3 color{1, 1, 1};
  float size = 1.0f;
  float life = 0.0f;
  float maxLife = 1.0f;
  float rotation = 0.0f;
  float rotationSpeed = 0.0f;
  uint64_t emitterId = 0;
  Vec3 gravity{0, -9.81f, 0};
  Vec3 colorStart{1, 1, 1};
  Vec3 colorEnd{1, 1, 1};
  float sizeStart = 1.0f;
  float sizeEnd = 0.0f;
  bool collideFloor = false;
  float floorY = 0.0f;
  float bounce = 0.4f;
  float damping = 0.0f;
};

struct Emitter {
  Vec3 position{0, 0, 0};
  Vec3 direction{0, 1, 0};
  float spread = 15.0f;
  float rate = 32.0f;
  float speedMin = 1.0f;
  float speedMax = 3.0f;
  float lifeMin = 0.5f;
  float lifeMax = 1.5f;
  float sizeStart = 1.0f;
  float sizeEnd = 0.0f;
  Vec3 gravity{0, -9.81f, 0};
  Vec3 colorStart{1, 1, 1};
  Vec3 colorEnd{1, 1, 1};
  BlendMode blend = BlendMode::Additive;
  EmitterShape shape = EmitterShape::Point;
  float shapeRadius = 0.5f;
  Vec3 shapeExtents{0.5f, 0.5f, 0.5f};
  float rotationSpeedMin = 0.0f;
  float rotationSpeedMax = 0.0f;
  bool collideFloor = false;
  float floorY = 0.0f;
  float bounce = 0.4f;
  float damping = 0.0f;
  bool loop = true;
  bool emitting = true;
  float duration = 0.0f;
  uint32_t maxParticles = 1024;
  // Assigned by System::addEmitter; callers should treat it as read-only.
  uint64_t id = 0;
  float emissionRemainder = 0.0f;
};

class System {
 public:
  explicit System(uint32_t capacity = 4096) : capacity_(capacity) {
    particles_.reserve(capacity);
  }

  uint64_t addEmitter(const Emitter& emitter);
  void clearEmitters() { emitters_.clear(); }
  uint32_t emitterCount() const { return static_cast<uint32_t>(emitters_.size()); }
  std::vector<Emitter>& emitters() { return emitters_; }

  void update(float deltaSeconds);
  void spawn(const Emitter& emitter, float count);

  uint32_t aliveCount() const { return static_cast<uint32_t>(particles_.size()); }
  uint32_t capacity() const { return capacity_; }
  const std::vector<Particle>& particles() const { return particles_; }

  // Flattened SoA upload for the renderer: pos4 (xyz + size), color4 (rgb + lifeRatio).
  void writeInstanceData(float* out, uint32_t maxInstances) const;

 private:
  void spawnForEmitter(const Emitter& emitter, float count, uint64_t emitterId);
  float random01();

  uint32_t capacity_;
  std::vector<Particle> particles_;
  std::vector<Emitter> emitters_;
  uint64_t nextEmitterId_ = 1;
  uint32_t randomState_ = 0x9e3779b9u;
};

}  // namespace nimbus::particles
