// Procedural mesh factory & mesh bounds computation: cube, plane, sphere, quad.
#include "nimbus/render.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace nimbus::render {

void Mesh::computeBounds() {
  if (vertices.size() < 8) {
    localBounds = {};
    hasBounds = false;
    return;
  }
  Vec3 bmin{std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()};
  Vec3 bmax{-std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max()};
  const size_t count = vertices.size() / 8u;
  for (size_t i = 0; i < count; ++i) {
    const float x = vertices[i * 8 + 0];
    const float y = vertices[i * 8 + 1];
    const float z = vertices[i * 8 + 2];
    bmin.x = std::min(bmin.x, x);
    bmin.y = std::min(bmin.y, y);
    bmin.z = std::min(bmin.z, z);
    bmax.x = std::max(bmax.x, x);
    bmax.y = std::max(bmax.y, y);
    bmax.z = std::max(bmax.z, z);
  }
  localBounds = {bmin, bmax};
  hasBounds = true;
}

Mesh createCube(float size) {
  Mesh mesh;
  const float h = size * 0.5f;
  const float faces[6][4][3] = {
      {{-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}},
      {{h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}},
      {{-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}},
      {{-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}},
      {{h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h}},
      {{-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}},
  };
  const float normals[6][3] = {{0, 0, 1}, {0, 0, -1}, {0, 1, 0},
                               {0, -1, 0}, {1, 0, 0}, {-1, 0, 0}};
  for (int f = 0; f < 6; ++f) {
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size() / 8);
    for (int v = 0; v < 4; ++v) {
      mesh.vertices.push_back(faces[f][v][0]);
      mesh.vertices.push_back(faces[f][v][1]);
      mesh.vertices.push_back(faces[f][v][2]);
      mesh.vertices.push_back(normals[f][0]);
      mesh.vertices.push_back(normals[f][1]);
      mesh.vertices.push_back(normals[f][2]);
      mesh.vertices.push_back(v == 1 || v == 2 ? 1.0f : 0.0f);
      mesh.vertices.push_back(v >= 2 ? 1.0f : 0.0f);
    }
    mesh.indices.push_back(base);
    mesh.indices.push_back(base + 1);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 3);
  }
  mesh.computeBounds();
  return mesh;
}

Mesh createPlane(float size, uint32_t subdivisions) {
  Mesh mesh;
  subdivisions = std::max(1u, subdivisions);
  const float step = size / static_cast<float>(subdivisions);
  const float half = size * 0.5f;
  for (uint32_t z = 0; z <= subdivisions; ++z) {
    for (uint32_t x = 0; x <= subdivisions; ++x) {
      mesh.vertices.push_back(-half + x * step);
      mesh.vertices.push_back(0.0f);
      mesh.vertices.push_back(-half + z * step);
      mesh.vertices.push_back(0.0f);
      mesh.vertices.push_back(1.0f);
      mesh.vertices.push_back(0.0f);
      mesh.vertices.push_back(static_cast<float>(x) / subdivisions);
      mesh.vertices.push_back(static_cast<float>(z) / subdivisions);
    }
  }
  const uint32_t stride = subdivisions + 1;
  for (uint32_t z = 0; z < subdivisions; ++z) {
    for (uint32_t x = 0; x < subdivisions; ++x) {
      const uint32_t a = z * stride + x;
      const uint32_t b = a + 1;
      const uint32_t c = a + stride;
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
  return mesh;
}

Mesh createSphere(float radius, uint32_t segments) {
  Mesh mesh;
  segments = std::max(4u, segments);
  for (uint32_t y = 0; y <= segments; ++y) {
    const float v = static_cast<float>(y) / segments;
    const float phi = v * math::kPi;
    for (uint32_t x = 0; x <= segments; ++x) {
      const float u = static_cast<float>(x) / segments;
      const float theta = u * math::kPi * 2.0f;
      const float nx = std::sin(phi) * std::cos(theta);
      const float ny = std::cos(phi);
      const float nz = std::sin(phi) * std::sin(theta);
      mesh.vertices.push_back(nx * radius);
      mesh.vertices.push_back(ny * radius);
      mesh.vertices.push_back(nz * radius);
      mesh.vertices.push_back(nx);
      mesh.vertices.push_back(ny);
      mesh.vertices.push_back(nz);
      mesh.vertices.push_back(u);
      mesh.vertices.push_back(v);
    }
  }
  const uint32_t stride = segments + 1;
  for (uint32_t y = 0; y < segments; ++y) {
    for (uint32_t x = 0; x < segments; ++x) {
      const uint32_t a = y * stride + x;
      const uint32_t b = a + 1;
      const uint32_t c = a + stride;
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
  return mesh;
}

Mesh createQuad(float width, float height) {
  Mesh mesh;
  const float hw = width * 0.5f;
  const float hh = height * 0.5f;
  const float v[4][8] = {
      {-hw, -hh, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
      {hw, -hh, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f},
      {hw, hh, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f},
      {-hw, hh, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
  };
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 8; ++j) mesh.vertices.push_back(v[i][j]);
  }
  mesh.indices = {0, 1, 2, 0, 2, 3};
  mesh.computeBounds();
  return mesh;
}

}  // namespace nimbus::render
