#include "nimbus/particles.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace nimbus::particles {
namespace {

bool finite(const Vec3& value) {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

}  // namespace

uint64_t System::addEmitter(const Emitter& emitter) {
  Emitter stored = emitter;
  stored.id = nextEmitterId_++;
  if (stored.id == 0) stored.id = nextEmitterId_++;
  stored.emissionRemainder = 0.0f;
  emitters_.push_back(std::move(stored));
  return emitters_.back().id;
}

float System::random01() {
  randomState_ ^= randomState_ << 13u;
  randomState_ ^= randomState_ >> 17u;
  randomState_ ^= randomState_ << 5u;
  return static_cast<float>(randomState_ >> 8u) * (1.0f / 16777216.0f);
}

void System::spawn(const Emitter& emitter, float count) {
  spawnForEmitter(emitter, count, emitter.id);
}

void System::spawnForEmitter(const Emitter& emitter, float count, uint64_t emitterId) {
  if (!std::isfinite(count) || count <= 0.0f || !finite(emitter.position) ||
      !finite(emitter.direction) || !finite(emitter.gravity) ||
      !finite(emitter.colorStart) || !finite(emitter.colorEnd) ||
      !std::isfinite(emitter.speedMin) || !std::isfinite(emitter.speedMax) ||
      !std::isfinite(emitter.lifeMin) || !std::isfinite(emitter.lifeMax) ||
      !std::isfinite(emitter.sizeStart) || !std::isfinite(emitter.sizeEnd)) return;
  if (math::lengthSquared(emitter.direction) < 1.0e-10f) return;
  if (emitterId != 0 && emitter.maxParticles == 0) return;

  const size_t ownCount = emitterId == 0 ? 0 : static_cast<size_t>(std::count_if(
      particles_.begin(), particles_.end(),
      [emitterId](const Particle& particle) { return particle.emitterId == emitterId; }));
  const size_t ownRemaining = emitterId == 0 ? std::numeric_limits<size_t>::max()
      : (ownCount >= emitter.maxParticles ? 0 : emitter.maxParticles - ownCount);
  const size_t globalRemaining = capacity_ > particles_.size()
      ? capacity_ - particles_.size() : 0;
  const size_t allowed = std::min(ownRemaining, globalRemaining);
  const size_t requested = count >= static_cast<float>(std::numeric_limits<size_t>::max())
      ? allowed : static_cast<size_t>(std::ceil(count));
  const size_t spawnCount = std::min(allowed, requested);
  if (spawnCount == 0) return;

  const Vec3 baseDirection = math::normalize(emitter.direction);
  Vec3 side = math::normalize(math::cross(baseDirection, Vec3{0, 1, 0}));
  if (math::lengthSquared(side) < 1.0e-8f)
    side = math::normalize(math::cross(baseDirection, Vec3{1, 0, 0}));
  const Vec3 up = math::normalize(math::cross(side, baseDirection));
  const float spread = math::radians(math::clamp(emitter.spread, 0.0f, 180.0f));
  const float lowSpeed = std::min(emitter.speedMin, emitter.speedMax);
  const float highSpeed = std::max(emitter.speedMin, emitter.speedMax);
  const float lowLife = std::max(1.0e-4f, std::min(emitter.lifeMin, emitter.lifeMax));
  const float highLife = std::max(lowLife, std::max(emitter.lifeMin, emitter.lifeMax));

  for (size_t index = 0; index < spawnCount; ++index) {
    const float speed = lowSpeed + (highSpeed - lowSpeed) * random01();
    const float azimuth = random01() * math::kPi * 2.0f;
    const float elevation = (random01() - 0.5f) * spread;
    const float cosElevation = std::cos(elevation);
    const Vec3 direction = math::normalize(
        baseDirection * cosElevation +
        side * (std::sin(elevation) * std::cos(azimuth)) +
        up * (std::sin(elevation) * std::sin(azimuth)));
    const float life = lowLife + (highLife - lowLife) * random01();

    Vec3 spawnPos = emitter.position;
    if (emitter.shape == EmitterShape::Sphere) {
      const float theta = random01() * math::kPi * 2.0f;
      const float z = random01() * 2.0f - 1.0f;
      const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
      const float rad = std::cbrt(random01()) * std::max(0.0f, emitter.shapeRadius);
      spawnPos += Vec3{r * std::cos(theta), z, r * std::sin(theta)} * rad;
    } else if (emitter.shape == EmitterShape::Box) {
      spawnPos.x += (random01() * 2.0f - 1.0f) * emitter.shapeExtents.x;
      spawnPos.y += (random01() * 2.0f - 1.0f) * emitter.shapeExtents.y;
      spawnPos.z += (random01() * 2.0f - 1.0f) * emitter.shapeExtents.z;
    } else if (emitter.shape == EmitterShape::Cone) {
      const float ringRadius = std::sqrt(random01()) * std::max(0.0f, emitter.shapeRadius);
      spawnPos += side * (std::cos(azimuth) * ringRadius) + up * (std::sin(azimuth) * ringRadius);
    }

    Particle particle;
    particle.position = spawnPos;
    particle.velocity = direction * speed;
    particle.color = emitter.colorStart;
    particle.size = emitter.sizeStart;
    particle.life = life;
    particle.maxLife = life;
    particle.rotation = random01() * math::kPi * 2.0f;
    particle.rotationSpeed = emitter.rotationSpeedMin +
        (emitter.rotationSpeedMax - emitter.rotationSpeedMin) * random01();
    particle.emitterId = emitterId;
    particle.gravity = emitter.gravity;
    particle.colorStart = emitter.colorStart;
    particle.colorEnd = emitter.colorEnd;
    particle.sizeStart = emitter.sizeStart;
    particle.sizeEnd = emitter.sizeEnd;
    particle.collideFloor = emitter.collideFloor;
    particle.floorY = emitter.floorY;
    particle.bounce = math::saturate(emitter.bounce);
    particle.damping = std::max(0.0f, emitter.damping);
    particles_.push_back(particle);
  }
}

void System::update(float deltaSeconds) {
  if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) return;
  for (Emitter& emitter : emitters_) {
    if (!emitter.emitting) continue;
    if (!emitter.loop) {
      if (!std::isfinite(emitter.duration)) {
        emitter.emitting = false;
        continue;
      }
      emitter.duration -= deltaSeconds;
      if (emitter.duration <= 0.0f) {
        emitter.emitting = false;
        continue;
      }
    }
    if (!std::isfinite(emitter.rate) || emitter.rate <= 0.0f) continue;
    const double requested = static_cast<double>(emitter.rate) * deltaSeconds +
                             std::max(0.0f, emitter.emissionRemainder);
    if (!std::isfinite(requested)) {
      emitter.emissionRemainder = 0.0f;
      continue;
    }
    const double whole = std::floor(requested);
    emitter.emissionRemainder = static_cast<float>(requested - whole);
    if (whole > 0.0) {
      const float spawnCount = static_cast<float>(std::min(
          whole, static_cast<double>(std::numeric_limits<uint32_t>::max())));
      spawnForEmitter(emitter, spawnCount, emitter.id);
    }
  }

  for (size_t index = 0; index < particles_.size();) {
    Particle& particle = particles_[index];
    particle.life -= deltaSeconds;
    if (particle.life <= 0.0f || !std::isfinite(particle.life)) {
      particles_[index] = std::move(particles_.back());
      particles_.pop_back();
      continue;
    }
    particle.velocity += particle.gravity * deltaSeconds;
    if (particle.damping > 0.0f) {
      const float factor = std::max(0.0f, 1.0f - particle.damping * deltaSeconds);
      particle.velocity *= factor;
    }
    particle.position += particle.velocity * deltaSeconds;
    if (particle.collideFloor && particle.position.y < particle.floorY) {
      particle.position.y = particle.floorY;
      if (particle.velocity.y < 0.0f) {
        particle.velocity.y = -particle.velocity.y * particle.bounce;
        particle.velocity.x *= 0.85f;
        particle.velocity.z *= 0.85f;
      }
    }
    particle.rotation += particle.rotationSpeed * deltaSeconds;
    const float t = math::saturate(1.0f - particle.life / particle.maxLife);
    particle.size = math::lerp(particle.sizeStart, particle.sizeEnd, t);
    particle.color = math::lerp(particle.colorStart, particle.colorEnd, t);
    ++index;
  }
}

void System::writeInstanceData(float* out, uint32_t maxInstances) const {
  if (!out) return;
  const uint32_t count = std::min(maxInstances, static_cast<uint32_t>(particles_.size()));
  for (uint32_t i = 0; i < count; ++i) {
    const Particle& particle = particles_[i];
    out[i * 8 + 0] = particle.position.x;
    out[i * 8 + 1] = particle.position.y;
    out[i * 8 + 2] = particle.position.z;
    out[i * 8 + 3] = particle.size;
    out[i * 8 + 4] = particle.color.x;
    out[i * 8 + 5] = particle.color.y;
    out[i * 8 + 6] = particle.color.z;
    out[i * 8 + 7] = particle.life / particle.maxLife;
  }
}

}  // namespace nimbus::particles
