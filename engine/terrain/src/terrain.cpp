#include "nimbus/terrain.hpp"

#include <algorithm>
#include <cmath>

namespace nimbus::terrain {

float Noise::hash(int32_t x, int32_t z) const {
  uint32_t h = seed_;
  h ^= static_cast<uint32_t>(x) * 374761393u;
  h ^= static_cast<uint32_t>(z) * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return static_cast<float>(h & 0xFFFFFF) / static_cast<float>(0xFFFFFF) * 2.0f - 1.0f;
}

float Noise::operator()(float x, float z) const {
  const int32_t xi = static_cast<int32_t>(std::floor(x));
  const int32_t zi = static_cast<int32_t>(std::floor(z));
  const float fx = x - static_cast<float>(xi);
  const float fz = z - static_cast<float>(zi);
  const float u = fx * fx * (3.0f - 2.0f * fx);
  const float v = fz * fz * (3.0f - 2.0f * fz);
  const float a = hash(xi, zi);
  const float b = hash(xi + 1, zi);
  const float c = hash(xi, zi + 1);
  const float d = hash(xi + 1, zi + 1);
  return math::lerp(math::lerp(a, b, u), math::lerp(c, d, u), v);
}

float Noise::fbm(float x, float z, uint32_t octaves, float lacunarity, float gain) const {
  float sum = 0.0f;
  float amplitude = 1.0f;
  float frequency = 1.0f;
  float norm = 0.0f;
  for (uint32_t i = 0; i < octaves; ++i) {
    sum += (*this)(x * frequency, z * frequency) * amplitude;
    norm += amplitude;
    amplitude *= gain;
    frequency *= lacunarity;
  }
  return norm > 0.0f ? sum / norm : 0.0f;
}

float Heightfield::sample(float x, float z) const {
  if (width == 0 || depth == 0 || heights.empty()) return 0.0f;
  const float safeScale = scale > 1e-6f ? scale : 1.0f;
  const float fx = math::clamp(x / safeScale, 0.0f, static_cast<float>(width - 1));
  const float fz = math::clamp(z / safeScale, 0.0f, static_cast<float>(depth - 1));
  const uint32_t x0 = static_cast<uint32_t>(std::floor(fx));
  const uint32_t z0 = static_cast<uint32_t>(std::floor(fz));
  const uint32_t x1 = std::min(x0 + 1u, width - 1u);
  const uint32_t z1 = std::min(z0 + 1u, depth - 1u);
  const float tx = fx - static_cast<float>(x0);
  const float tz = fz - static_cast<float>(z0);
  const float h00 = at(x0, z0);
  const float h10 = at(x1, z0);
  const float h01 = at(x0, z1);
  const float h11 = at(x1, z1);
  return math::lerp(math::lerp(h00, h10, tx), math::lerp(h01, h11, tx), tz);
}

Vec3 Heightfield::normal(float x, float z) const {
  const float hL = sample(x - scale, z);
  const float hR = sample(x + scale, z);
  const float hD = sample(x, z - scale);
  const float hU = sample(x, z + scale);
  return math::normalize(Vec3{hL - hR, 2.0f * std::max(0.01f, scale), hD - hU});
}

void Terrain::markAllDirty() {
  for (Chunk& chunk : chunks_) chunk.dirty = true;
}

void Terrain::generate(const Noise& noise, uint32_t octaves, float amplitude) {
  for (uint32_t z = 0; z < field_.depth; ++z) {
    for (uint32_t x = 0; x < field_.width; ++x) {
      const float n = noise.fbm(static_cast<float>(x) * 0.05f, static_cast<float>(z) * 0.05f,
                                octaves);
      field_.set(x, z, n * amplitude);
    }
  }
  markAllDirty();
  autoGenerateSplatmap(amplitude * 0.6f, 0.78f);
}

void Terrain::applyBrush(float worldX, float worldZ, float radius, float strength, BrushType type,
                         float targetHeight) {
  if (field_.width == 0 || field_.depth == 0 || radius <= 0.0f) return;
  const float safeScale = field_.scale > 1e-6f ? field_.scale : 1.0f;
  const int32_t minX = std::max<int32_t>(0, static_cast<int32_t>(std::floor((worldX - radius) / safeScale)));
  const int32_t maxX = std::min<int32_t>(static_cast<int32_t>(field_.width) - 1,
                                         static_cast<int32_t>(std::ceil((worldX + radius) / safeScale)));
  const int32_t minZ = std::max<int32_t>(0, static_cast<int32_t>(std::floor((worldZ - radius) / safeScale)));
  const int32_t maxZ = std::min<int32_t>(static_cast<int32_t>(field_.depth) - 1,
                                         static_cast<int32_t>(std::ceil((worldZ + radius) / safeScale)));

  std::vector<float> smoothCopy;
  if (type == BrushType::Smooth) smoothCopy = field_.heights;
  const Noise brushNoise(4242u);

  bool modified = false;
  for (int32_t z = minZ; z <= maxZ; ++z) {
    for (int32_t x = minX; x <= maxX; ++x) {
      const float wx = static_cast<float>(x) * safeScale;
      const float wz = static_cast<float>(z) * safeScale;
      const float dx = wx - worldX;
      const float dz = wz - worldZ;
      const float dist = std::sqrt(dx * dx + dz * dz);
      if (dist > radius) continue;
      const float falloff = 0.5f * (1.0f + std::cos(math::kPi * (dist / radius)));
      const float w = falloff * strength;
      float h = field_.at(static_cast<uint32_t>(x), static_cast<uint32_t>(z));
      switch (type) {
        case BrushType::Raise: h += w; break;
        case BrushType::Lower: h -= w; break;
        case BrushType::Flatten: h = math::lerp(h, targetHeight, math::saturate(w)); break;
        case BrushType::Smooth: {
          float sum = 0.0f;
          int count = 0;
          for (int dz2 = -1; dz2 <= 1; ++dz2) {
            for (int dx2 = -1; dx2 <= 1; ++dx2) {
              const int32_t nx = math::clamp(x + dx2, 0, static_cast<int32_t>(field_.width) - 1);
              const int32_t nz = math::clamp(z + dz2, 0, static_cast<int32_t>(field_.depth) - 1);
              sum += smoothCopy[static_cast<size_t>(nz) * field_.width + static_cast<size_t>(nx)];
              ++count;
            }
          }
          h = math::lerp(h, sum / static_cast<float>(count), math::saturate(w));
          break;
        }
        case BrushType::Noise:
          h += brushNoise.fbm(wx * 0.25f, wz * 0.25f, 3) * w;
          break;
      }
      field_.set(static_cast<uint32_t>(x), static_cast<uint32_t>(z), h);
      modified = true;
    }
  }
  if (modified) markAllDirty();
}

void Terrain::autoGenerateSplatmap(float snowHeight, float slopeRockThreshold) {
  splatWeights_.resize(static_cast<size_t>(field_.width) * field_.depth);
  for (uint32_t z = 0; z < field_.depth; ++z) {
    for (uint32_t x = 0; x < field_.width; ++x) {
      const float wx = static_cast<float>(x) * field_.scale;
      const float wz = static_cast<float>(z) * field_.scale;
      const float h = field_.at(x, z);
      const Vec3 n = field_.normal(wx, wz);
      const float slope = math::saturate(n.y);
      Vec4 w{0, 0, 0, 0};  // x:grass, y:rock, z:sand, w:snow
      if (slope < slopeRockThreshold) {
        w.y = 1.0f - slope / std::max(0.01f, slopeRockThreshold);
        w.x = 1.0f - w.y;
      } else if (h >= snowHeight) {
        w.w = math::saturate((h - snowHeight) * 0.5f + 0.5f);
        w.x = 1.0f - w.w;
      } else if (h < 0.35f) {
        w.z = math::saturate(1.0f - std::max(0.0f, h) / 0.35f);
        w.x = 1.0f - w.z;
      } else {
        w.x = 1.0f;
      }
      splatWeights_[static_cast<size_t>(z) * field_.width + x] = w;
    }
  }
}

void Terrain::paintSplat(float worldX, float worldZ, float radius, uint32_t channel, float strength) {
  if (channel > 3 || field_.width == 0 || field_.depth == 0 || radius <= 0.0f) return;
  const float safeScale = field_.scale > 1e-6f ? field_.scale : 1.0f;
  const int32_t minX = std::max<int32_t>(0, static_cast<int32_t>(std::floor((worldX - radius) / safeScale)));
  const int32_t maxX = std::min<int32_t>(static_cast<int32_t>(field_.width) - 1,
                                         static_cast<int32_t>(std::ceil((worldX + radius) / safeScale)));
  const int32_t minZ = std::max<int32_t>(0, static_cast<int32_t>(std::floor((worldZ - radius) / safeScale)));
  const int32_t maxZ = std::min<int32_t>(static_cast<int32_t>(field_.depth) - 1,
                                         static_cast<int32_t>(std::ceil((worldZ + radius) / safeScale)));
  for (int32_t z = minZ; z <= maxZ; ++z) {
    for (int32_t x = minX; x <= maxX; ++x) {
      const float dx = static_cast<float>(x) * safeScale - worldX;
      const float dz = static_cast<float>(z) * safeScale - worldZ;
      const float dist = std::sqrt(dx * dx + dz * dz);
      if (dist > radius) continue;
      const float w = math::saturate((1.0f - dist / radius) * strength);
      Vec4& slot = splatWeights_[static_cast<size_t>(z) * field_.width + static_cast<size_t>(x)];
      float arr[4] = {slot.x, slot.y, slot.z, slot.w};
      arr[channel] = math::saturate(arr[channel] + w);
      const float sum = arr[0] + arr[1] + arr[2] + arr[3];
      if (sum > 1e-5f) {
        slot = {arr[0] / sum, arr[1] / sum, arr[2] / sum, arr[3] / sum};
      }
    }
  }
}

Vec4 Terrain::splatAt(uint32_t x, uint32_t z) const {
  if (x >= field_.width || z >= field_.depth) return {1, 0, 0, 0};
  return splatWeights_[static_cast<size_t>(z) * field_.width + x];
}

std::vector<VegetationInstance> Terrain::scatterProps(uint32_t seed, float minHeight,
                                                      float maxSlopeDot, uint32_t count) const {
  std::vector<VegetationInstance> out;
  if (field_.width < 2 || field_.depth < 2 || count == 0) return out;
  uint32_t rng = seed == 0 ? 1u : seed;
  auto next01 = [&]() {
    rng ^= rng << 13u;
    rng ^= rng >> 17u;
    rng ^= rng << 5u;
    return static_cast<float>(rng >> 8u) * (1.0f / 16777216.0f);
  };
  const float maxW = worldWidth();
  const float maxD = worldDepth();
  out.reserve(count);
  const uint32_t maxAttempts = count * 4u;
  for (uint32_t attempt = 0; attempt < maxAttempts && out.size() < count; ++attempt) {
    const float wx = next01() * maxW;
    const float wz = next01() * maxD;
    const float wy = field_.sample(wx, wz);
    if (wy < minHeight) continue;
    const Vec3 n = field_.normal(wx, wz);
    if (n.y < maxSlopeDot) continue;
    const float s = 0.75f + next01() * 0.65f;
    VegetationInstance inst;
    inst.position = {wx, wy, wz};
    inst.scale = {s, s, s};
    inst.yaw = next01() * math::kPi * 2.0f;
    inst.variant = rng % 3u;
    out.push_back(inst);
  }
  return out;
}

uint32_t Terrain::lodForDistance(float distance) const {
  const float chunkWorldSize = static_cast<float>(chunkSize_) * field_.scale;
  if (chunkWorldSize <= 0.0f) return 0;
  const float normalized = distance / chunkWorldSize;
  if (normalized < 2.0f) return 0;
  if (normalized < 4.0f) return 1;
  if (normalized < 8.0f) return 2;
  return 3;
}

render::Mesh& Terrain::chunkMesh(uint32_t chunkX, uint32_t chunkZ, uint32_t lod) {
  const uint32_t stride = 1u << lod;
  const size_t index = static_cast<size_t>(chunkZ) * chunkCountX() + chunkX;
  if (index >= chunks_.size()) chunks_.resize(index + 1);
  Chunk& chunk = chunks_[index];
  if (!chunk.dirty && chunk.lod == lod && chunk.x == chunkX && chunk.z == chunkZ)
    return chunk.mesh;
  chunk = Chunk{chunkX, chunkZ, lod, render::Mesh{}, true};

  render::Mesh mesh;
  const uint32_t x0 = chunkX * chunkSize_;
  const uint32_t z0 = chunkZ * chunkSize_;
  const uint32_t x1 = std::min(x0 + chunkSize_, field_.width - 1);
  const uint32_t z1 = std::min(z0 + chunkSize_, field_.depth - 1);
  for (uint32_t z = z0; z <= z1; z += stride) {
    for (uint32_t x = x0; x <= x1; x += stride) {
      const float wx = static_cast<float>(x) * field_.scale;
      const float wz = static_cast<float>(z) * field_.scale;
      const float wy = field_.at(x, z);
      const Vec3 n = field_.normal(wx, wz);
      mesh.vertices.push_back(wx);
      mesh.vertices.push_back(wy);
      mesh.vertices.push_back(wz);
      mesh.vertices.push_back(n.x);
      mesh.vertices.push_back(n.y);
      mesh.vertices.push_back(n.z);
      mesh.vertices.push_back(static_cast<float>(x) / field_.width);
      mesh.vertices.push_back(static_cast<float>(z) / field_.depth);
    }
  }
  const uint32_t columns = (x1 - x0) / stride + 1;
  for (uint32_t z = z0; z < z1; z += stride) {
    for (uint32_t x = x0; x < x1; x += stride) {
      const uint32_t a = ((z - z0) / stride) * columns + ((x - x0) / stride);
      const uint32_t b = a + 1;
      const uint32_t c = a + columns;
      const uint32_t d = c + 1;
      mesh.indices.push_back(a);
      mesh.indices.push_back(c);
      mesh.indices.push_back(b);
      mesh.indices.push_back(b);
      mesh.indices.push_back(c);
      mesh.indices.push_back(d);
    }
  }
  mesh.computeBounds();
  chunk.mesh = std::move(mesh);
  chunk.dirty = false;
  return chunk.mesh;
}

}  // namespace nimbus::terrain
