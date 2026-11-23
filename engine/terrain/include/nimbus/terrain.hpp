// Nimbus terrain: heightfield chunks with distance LOD, sculpting brushes,
// 4-channel splatmap blending, and deterministic vegetation scatter.
#pragma once

#include <cstdint>
#include <vector>

#include "nimbus/math.hpp"
#include "nimbus/render.hpp"

namespace nimbus::terrain {

using math::Vec2;
using math::Vec3;
using math::Vec4;

enum class BrushType : uint8_t { Raise, Lower, Flatten, Smooth, Noise };

struct VegetationInstance {
  Vec3 position{0, 0, 0};
  Vec3 scale{1, 1, 1};
  float yaw = 0.0f;
  uint32_t variant = 0;
};

struct Heightfield {
  uint32_t width = 0;
  uint32_t depth = 0;
  float scale = 1.0f;  // world units per sample
  std::vector<float> heights;

  float at(uint32_t x, uint32_t z) const {
    if (x >= width || z >= depth) return 0.0f;
    return heights[static_cast<size_t>(z) * width + x];
  }
  void set(uint32_t x, uint32_t z, float value) {
    if (x < width && z < depth) heights[static_cast<size_t>(z) * width + x] = value;
  }
  float sample(float x, float z) const;
  Vec3 normal(float x, float z) const;
};

// Deterministic value-noise so terrain is reproducible from a seed.
class Noise {
 public:
  explicit Noise(uint32_t seed = 1337) : seed_(seed) {}
  float operator()(float x, float z) const;
  float fbm(float x, float z, uint32_t octaves, float lacunarity = 2.0f,
            float gain = 0.5f) const;

 private:
  uint32_t seed_;
  float hash(int32_t x, int32_t z) const;
};

struct Chunk {
  uint32_t x = 0;
  uint32_t z = 0;
  uint32_t lod = 0;
  render::Mesh mesh;
  bool dirty = true;
};

class Terrain {
 public:
  Terrain(uint32_t width, uint32_t depth, float scale, uint32_t chunkSize = 32)
      : field_{width, depth, scale, std::vector<float>(static_cast<size_t>(width) * depth, 0.0f)},
        chunkSize_(chunkSize),
        splatWeights_(static_cast<size_t>(width) * depth, Vec4{1, 0, 0, 0}) {}

  Heightfield& field() { return field_; }
  const Heightfield& field() const { return field_; }

  void generate(const Noise& noise, uint32_t octaves = 4, float amplitude = 8.0f);

  // Sculpting & smoothing brushes in world coordinates; marks affected chunks dirty.
  void applyBrush(float worldX, float worldZ, float radius, float strength, BrushType type,
                  float targetHeight = 0.0f);

  // 4-channel normalized splat weights (grass, rock, sand, snow).
  void autoGenerateSplatmap(float snowHeight = 5.0f, float slopeRockThreshold = 0.75f);
  void paintSplat(float worldX, float worldZ, float radius, uint32_t channel, float strength);
  Vec4 splatAt(uint32_t x, uint32_t z) const;

  // Deterministic vegetation scatter across terrain slopes & elevation band.
  std::vector<VegetationInstance> scatterProps(uint32_t seed, float minHeight, float maxSlopeDot,
                                               uint32_t count) const;

  // Returns the chunk mesh for a chunk coordinate at the requested LOD,
  // rebuilding it when the LOD changed or terrain was sculpted.
  render::Mesh& chunkMesh(uint32_t chunkX, uint32_t chunkZ, uint32_t lod);

  uint32_t chunkCountX() const { return (field_.width + chunkSize_ - 1) / chunkSize_; }
  uint32_t chunkCountZ() const { return (field_.depth + chunkSize_ - 1) / chunkSize_; }
  uint32_t chunkSize() const { return chunkSize_; }
  float worldWidth() const { return static_cast<float>(field_.width > 0 ? field_.width - 1 : 0) * field_.scale; }
  float worldDepth() const { return static_cast<float>(field_.depth > 0 ? field_.depth - 1 : 0) * field_.scale; }

  uint32_t lodForDistance(float distance) const;

 private:
  void markAllDirty();

  Heightfield field_;
  uint32_t chunkSize_;
  std::vector<Chunk> chunks_;
  std::vector<Vec4> splatWeights_;
};

}  // namespace nimbus::terrain
