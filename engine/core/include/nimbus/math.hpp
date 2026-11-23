// Nimbus core mathematics. Column-major matrices; m[column][row].
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace nimbus::math {

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kEpsilon = 1.0e-6f;

struct Vec2 {
  float x = 0.0f, y = 0.0f;
  constexpr Vec2() = default;
  constexpr Vec2(float xValue, float yValue) : x(xValue), y(yValue) {}
  constexpr float& operator[](size_t i) { return i == 0 ? x : y; }
  constexpr float operator[](size_t i) const { return i == 0 ? x : y; }
};

struct Vec3 {
  float x = 0.0f, y = 0.0f, z = 0.0f;
  constexpr Vec3() = default;
  constexpr Vec3(float xValue, float yValue, float zValue) : x(xValue), y(yValue), z(zValue) {}
  constexpr float& operator[](size_t i) { return i == 0 ? x : (i == 1 ? y : z); }
  constexpr float operator[](size_t i) const { return i == 0 ? x : (i == 1 ? y : z); }
};

struct Vec4 {
  float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
  constexpr Vec4() = default;
  constexpr Vec4(float xValue, float yValue, float zValue, float wValue)
      : x(xValue), y(yValue), z(zValue), w(wValue) {}
  constexpr Vec4(const Vec3& xyz, float wValue) : x(xyz.x), y(xyz.y), z(xyz.z), w(wValue) {}
  constexpr float& operator[](size_t i) {
    return i == 0 ? x : (i == 1 ? y : (i == 2 ? z : w));
  }
  constexpr float operator[](size_t i) const {
    return i == 0 ? x : (i == 1 ? y : (i == 2 ? z : w));
  }
};

struct Quat {
  float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
};

struct Mat4 {
  float columns[4][4]{};
  constexpr float* operator[](size_t column) { return columns[column]; }
  constexpr const float* operator[](size_t column) const { return columns[column]; }
};

struct Aabb {
  Vec3 min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
           std::numeric_limits<float>::max()};
  Vec3 max{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
           -std::numeric_limits<float>::max()};
};

struct Plane {
  Vec3 normal{0.0f, 1.0f, 0.0f};
  float distance = 0.0f;  // dot(normal, point) + distance >= 0 is inside.
};

struct Frustum {
  // left, right, bottom, top, near, far
  Plane planes[6]{};
};

constexpr Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
constexpr Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
constexpr Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
constexpr Vec2 operator/(Vec2 a, float s) { return {a.x / s, a.y / s}; }
constexpr Vec2& operator+=(Vec2& a, Vec2 b) { a.x += b.x; a.y += b.y; return a; }
constexpr Vec2& operator-=(Vec2& a, Vec2 b) { a.x -= b.x; a.y -= b.y; return a; }

constexpr Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr Vec3 operator-(Vec3 v) { return {-v.x, -v.y, -v.z}; }
constexpr Vec3 operator*(Vec3 v, float s) { return {v.x * s, v.y * s, v.z * s}; }
constexpr Vec3 operator*(float s, Vec3 v) { return v * s; }
constexpr Vec3 operator/(Vec3 v, float s) { return {v.x / s, v.y / s, v.z / s}; }
constexpr Vec3 operator*(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
constexpr Vec3& operator+=(Vec3& a, Vec3 b) { a = a + b; return a; }
constexpr Vec3& operator-=(Vec3& a, Vec3 b) { a = a - b; return a; }
constexpr Vec3& operator*=(Vec3& a, float s) { a = a * s; return a; }
constexpr Vec3& operator/=(Vec3& a, float s) { a = a / s; return a; }

constexpr Vec4 operator+(Vec4 a, Vec4 b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
constexpr Vec4 operator-(Vec4 a, Vec4 b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
constexpr Vec4 operator*(Vec4 v, float s) { return {v.x * s, v.y * s, v.z * s, v.w * s}; }

constexpr float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
constexpr float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr float dot(Vec4 a, Vec4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
constexpr Vec3 cross(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
constexpr float lengthSquared(Vec2 v) { return dot(v, v); }
constexpr float lengthSquared(Vec3 v) { return dot(v, v); }
constexpr float lengthSquared(Vec4 v) { return dot(v, v); }
inline float length(Vec2 v) { return std::sqrt(lengthSquared(v)); }
inline float length(Vec3 v) { return std::sqrt(lengthSquared(v)); }
inline float length(Vec4 v) { return std::sqrt(lengthSquared(v)); }
inline Vec2 normalize(Vec2 v) { const float n = length(v); return n > kEpsilon ? v / n : Vec2{}; }
inline Vec3 normalize(Vec3 v) { const float n = length(v); return n > kEpsilon ? v / n : Vec3{}; }
inline Vec4 normalize(Vec4 v) { const float n = length(v); return n > kEpsilon ? v * (1.0f / n) : Vec4{}; }
inline float distance(Vec3 a, Vec3 b) { return length(a - b); }
inline float radians(float degreesValue) { return degreesValue * (kPi / 180.0f); }
inline float degrees(float radiansValue) { return radiansValue * (180.0f / kPi); }
inline float clamp(float value, float low, float high) { return std::clamp(value, low, high); }
inline float saturate(float value) { return clamp(value, 0.0f, 1.0f); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline Vec2 lerp(Vec2 a, Vec2 b, float t) { return a + (b - a) * t; }
inline Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
inline Vec4 lerp(Vec4 a, Vec4 b, float t) { return a + (b - a) * t; }

inline Quat quatNormalize(Quat q) {
  const float n = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
  if (n <= kEpsilon) return {};
  return {q.x/n, q.y/n, q.z/n, q.w/n};
}
inline Quat quatConjugate(Quat q) { return {-q.x, -q.y, -q.z, q.w}; }
inline Quat quatMultiply(Quat a, Quat b) {
  return {a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
          a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
          a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
          a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z};
}
inline Quat quatFromAxisAngle(Vec3 axis, float angle) {
  axis = normalize(axis);
  const float half = angle * 0.5f;
  const float s = std::sin(half);
  return quatNormalize({axis.x*s, axis.y*s, axis.z*s, std::cos(half)});
}
inline Vec3 rotate(Quat q, Vec3 v) {
  q = quatNormalize(q);
  const Vec3 u{q.x, q.y, q.z};
  return v + 2.0f * cross(u, cross(u, v) + q.w * v);
}
inline Quat quatSlerp(Quat a, Quat b, float t) {
  a = quatNormalize(a); b = quatNormalize(b);
  float d = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
  if (d < 0.0f) { b = {-b.x, -b.y, -b.z, -b.w}; d = -d; }
  if (d > 0.9995f) return quatNormalize({lerp(a.x,b.x,t), lerp(a.y,b.y,t),
                                         lerp(a.z,b.z,t), lerp(a.w,b.w,t)});
  const float theta = std::acos(clamp(d, -1.0f, 1.0f));
  const float sinTheta = std::sin(theta);
  const float wa = std::sin((1.0f - t) * theta) / sinTheta;
  const float wb = std::sin(t * theta) / sinTheta;
  return quatNormalize({a.x*wa+b.x*wb, a.y*wa+b.y*wb, a.z*wa+b.z*wb, a.w*wa+b.w*wb});
}

inline Mat4 mat4Identity() {
  Mat4 out{};
  for (size_t i = 0; i < 4; ++i) out[i][i] = 1.0f;
  return out;
}
inline Mat4 mat4Multiply(const Mat4& a, const Mat4& b) {
  Mat4 out{};
  for (size_t column = 0; column < 4; ++column)
    for (size_t row = 0; row < 4; ++row)
      for (size_t k = 0; k < 4; ++k)
        out[column][row] += a[k][row] * b[column][k];
  return out;
}
inline Mat4 mat4FromTranslation(Vec3 t) {
  Mat4 out = mat4Identity(); out[3][0] = t.x; out[3][1] = t.y; out[3][2] = t.z; return out;
}
inline Mat4 mat4FromQuat(Quat q) {
  q = quatNormalize(q);
  const float xx=q.x*q.x, yy=q.y*q.y, zz=q.z*q.z;
  const float xy=q.x*q.y, xz=q.x*q.z, yz=q.y*q.z;
  const float wx=q.w*q.x, wy=q.w*q.y, wz=q.w*q.z;
  Mat4 out = mat4Identity();
  out[0][0] = 1.0f - 2.0f*(yy+zz);
  out[0][1] = 2.0f*(xy+wz);
  out[0][2] = 2.0f*(xz-wy);
  out[1][0] = 2.0f*(xy-wz);
  out[1][1] = 1.0f - 2.0f*(xx+zz);
  out[1][2] = 2.0f*(yz+wx);
  out[2][0] = 2.0f*(xz+wy);
  out[2][1] = 2.0f*(yz-wx);
  out[2][2] = 1.0f - 2.0f*(xx+yy);
  return out;
}
inline Mat4 mat4FromTrs(Vec3 position, Quat rotation, Vec3 scale) {
  Mat4 out = mat4FromQuat(rotation);
  for (size_t row = 0; row < 3; ++row) {
    out[0][row] *= scale.x;
    out[1][row] *= scale.y;
    out[2][row] *= scale.z;
  }
  out[3][0] = position.x; out[3][1] = position.y; out[3][2] = position.z;
  return out;
}
inline Vec4 mat4Transform(const Mat4& m, Vec4 v) {
  return {m[0][0]*v.x + m[1][0]*v.y + m[2][0]*v.z + m[3][0]*v.w,
          m[0][1]*v.x + m[1][1]*v.y + m[2][1]*v.z + m[3][1]*v.w,
          m[0][2]*v.x + m[1][2]*v.y + m[2][2]*v.z + m[3][2]*v.w,
          m[0][3]*v.x + m[1][3]*v.y + m[2][3]*v.z + m[3][3]*v.w};
}
inline Mat4 mat4Perspective(float fovRadians, float aspect, float nearPlane, float farPlane) {
  Mat4 out{};
  if (!(aspect > 0.0f) || !(nearPlane > 0.0f) || !(farPlane > nearPlane)) return out;
  const float f = 1.0f / std::tan(fovRadians * 0.5f);
  out[0][0] = f / aspect;
  out[1][1] = f;
  out[2][2] = -(farPlane + nearPlane) / (farPlane - nearPlane);
  out[2][3] = -1.0f;
  out[3][2] = -(2.0f * farPlane * nearPlane) / (farPlane - nearPlane);
  return out;
}
inline Mat4 mat4LookAt(Vec3 eye, Vec3 target, Vec3 up) {
  const Vec3 forward = normalize(target - eye);
  const Vec3 side = normalize(cross(forward, up));
  const Vec3 correctedUp = cross(side, forward);
  Mat4 out = mat4Identity();
  out[0][0] = side.x; out[0][1] = correctedUp.x; out[0][2] = -forward.x;
  out[1][0] = side.y; out[1][1] = correctedUp.y; out[1][2] = -forward.y;
  out[2][0] = side.z; out[2][1] = correctedUp.z; out[2][2] = -forward.z;
  out[3][0] = -dot(side, eye);
  out[3][1] = -dot(correctedUp, eye);
  out[3][2] = dot(forward, eye);
  return out;
}
inline bool mat4TryInverse(const Mat4& in, Mat4& out) {
  double a[4][8]{};
  for (size_t r = 0; r < 4; ++r) {
    for (size_t c = 0; c < 4; ++c) a[r][c] = in[c][r];
    a[r][r + 4] = 1.0;
  }
  for (size_t c = 0; c < 4; ++c) {
    size_t pivot = c;
    for (size_t r = c + 1; r < 4; ++r)
      if (std::fabs(a[r][c]) > std::fabs(a[pivot][c])) pivot = r;
    if (std::fabs(a[pivot][c]) <= 1.0e-12) return false;
    if (pivot != c) for (size_t k = 0; k < 8; ++k) std::swap(a[pivot][k], a[c][k]);
    const double divisor = a[c][c];
    for (double& value : a[c]) value /= divisor;
    for (size_t r = 0; r < 4; ++r) if (r != c) {
      const double factor = a[r][c];
      for (size_t k = 0; k < 8; ++k) a[r][k] -= factor * a[c][k];
    }
  }
  out = {};
  for (size_t r = 0; r < 4; ++r)
    for (size_t c = 0; c < 4; ++c) out[c][r] = static_cast<float>(a[r][c + 4]);
  return true;
}
inline Mat4 mat4Inverse(const Mat4& matrix) {
  Mat4 result{};
  return mat4TryInverse(matrix, result) ? result : mat4Identity();
}
inline Mat4 mat4Ortho(float left, float right, float bottom, float top, float nearPlane,
                      float farPlane) {
  Mat4 result = mat4Identity();
  const float rl = right - left;
  const float tb = top - bottom;
  const float fn = farPlane - nearPlane;
  if (std::fabs(rl) <= kEpsilon || std::fabs(tb) <= kEpsilon || std::fabs(fn) <= kEpsilon)
    return result;
  result[0][0] = 2.0f / rl;
  result[1][1] = 2.0f / tb;
  result[2][2] = -2.0f / fn;
  result[3][0] = -(right + left) / rl;
  result[3][1] = -(top + bottom) / tb;
  result[3][2] = -(farPlane + nearPlane) / fn;
  return result;
}
inline void aabbExpand(Aabb& box, Vec3 point) {
  box.min.x = std::min(box.min.x, point.x); box.min.y = std::min(box.min.y, point.y);
  box.min.z = std::min(box.min.z, point.z);
  box.max.x = std::max(box.max.x, point.x); box.max.y = std::max(box.max.y, point.y);
  box.max.z = std::max(box.max.z, point.z);
}
inline bool aabbValid(const Aabb& box) {
  return box.min.x <= box.max.x && box.min.y <= box.max.y && box.min.z <= box.max.z;
}
inline bool aabbContains(const Aabb& box, Vec3 p) {
  return aabbValid(box) && p.x >= box.min.x && p.x <= box.max.x &&
         p.y >= box.min.y && p.y <= box.max.y && p.z >= box.min.z && p.z <= box.max.z;
}
inline Plane normalizePlane(Plane p) {
  const float magnitude = length(p.normal);
  if (magnitude <= kEpsilon) return {};
  p.normal = p.normal / magnitude; p.distance /= magnitude; return p;
}
inline Frustum frustumFromViewProj(const Mat4& m) {
  // Extract matrix rows from column-major storage, OpenGL clip-space depth.
  const auto row = [&m](size_t r) { return Vec4{m[0][r], m[1][r], m[2][r], m[3][r]}; };
  const Vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
  const auto plane = [](Vec4 a, Vec4 b, float sign) {
    return normalizePlane(Plane{{a.x + sign*b.x, a.y + sign*b.y, a.z + sign*b.z}, a.w + sign*b.w});
  };
  Frustum out{};
  out.planes[0] = plane(r3, r0, 1.0f);
  out.planes[1] = plane(r3, r0, -1.0f);
  out.planes[2] = plane(r3, r1, 1.0f);
  out.planes[3] = plane(r3, r1, -1.0f);
  out.planes[4] = plane(r3, r2, 1.0f);
  out.planes[5] = plane(r3, r2, -1.0f);
  return out;
}
inline bool frustumContainsPoint(const Frustum& frustum, Vec3 point) {
  for (const Plane& p : frustum.planes)
    if (dot(p.normal, point) + p.distance < 0.0f) return false;
  return true;
}

struct Sphere {
  Vec3 center{0.0f, 0.0f, 0.0f};
  float radius = 0.0f;
};

struct Ray {
  Vec3 origin{0.0f, 0.0f, 0.0f};
  Vec3 direction{0.0f, 0.0f, -1.0f};
};

struct Capsule {
  Vec3 start{0.0f, 0.0f, 0.0f};
  Vec3 end{0.0f, 1.0f, 0.0f};
  float radius = 0.25f;
};

struct Obb {
  Vec3 center{0.0f, 0.0f, 0.0f};
  Vec3 halfExtents{0.5f, 0.5f, 0.5f};
  Quat orientation{0.0f, 0.0f, 0.0f, 1.0f};
};

inline bool frustumContainsSphere(const Frustum& frustum, const Sphere& sphere) {
  for (const Plane& p : frustum.planes)
    if (dot(p.normal, sphere.center) + p.distance < -sphere.radius) return false;
  return true;
}

inline bool frustumIntersectsAabb(const Frustum& frustum, const Aabb& box) {
  if (!aabbValid(box)) return false;
  for (const Plane& p : frustum.planes) {
    const Vec3 positive{
        p.normal.x >= 0.0f ? box.max.x : box.min.x,
        p.normal.y >= 0.0f ? box.max.y : box.min.y,
        p.normal.z >= 0.0f ? box.max.z : box.min.z,
    };
    if (dot(p.normal, positive) + p.distance < 0.0f) return false;
  }
  return true;
}

inline Aabb aabbMerge(const Aabb& a, const Aabb& b) {
  if (!aabbValid(a)) return b;
  if (!aabbValid(b)) return a;
  return {{std::min(a.min.x, b.min.x), std::min(a.min.y, b.min.y), std::min(a.min.z, b.min.z)},
          {std::max(a.max.x, b.max.x), std::max(a.max.y, b.max.y), std::max(a.max.z, b.max.z)}};
}

inline Aabb aabbTransform(const Aabb& localBox, const Mat4& world) {
  if (!aabbValid(localBox)) return localBox;
  Aabb out;
  for (int i = 0; i < 8; ++i) {
    const Vec4 corner{
        (i & 1) ? localBox.max.x : localBox.min.x,
        (i & 2) ? localBox.max.y : localBox.min.y,
        (i & 4) ? localBox.max.z : localBox.min.z,
        1.0f,
    };
    const Vec4 transformed = mat4Transform(world, corner);
    aabbExpand(out, {transformed.x, transformed.y, transformed.z});
  }
  return out;
}

// Euler angles in radians (X=pitch, Y=yaw, Z=roll, intrinsic YXZ order).
inline Quat quatFromEuler(Vec3 radiansEuler) {
  const Quat qy = quatFromAxisAngle({0.0f, 1.0f, 0.0f}, radiansEuler.y);
  const Quat qx = quatFromAxisAngle({1.0f, 0.0f, 0.0f}, radiansEuler.x);
  const Quat qz = quatFromAxisAngle({0.0f, 0.0f, 1.0f}, radiansEuler.z);
  return quatNormalize(quatMultiply(qy, quatMultiply(qx, qz)));
}

inline Vec3 eulerFromQuat(Quat q) {
  q = quatNormalize(q);
  const float sinp = 2.0f * (q.w * q.x - q.y * q.z);
  const float pitch = std::fabs(sinp) >= 1.0f ? std::copysign(kPi * 0.5f, sinp) : std::asin(sinp);
  const float yaw = std::atan2(2.0f * (q.w * q.y + q.x * q.z),
                               1.0f - 2.0f * (q.x * q.x + q.y * q.y));
  const float roll = std::atan2(2.0f * (q.w * q.z + q.x * q.y),
                                1.0f - 2.0f * (q.x * q.x + q.z * q.z));
  return {pitch, yaw, roll};
}

inline Quat quatLookRotation(Vec3 forward, Vec3 up = {0.0f, 1.0f, 0.0f}) {
  const Vec3 f = normalize(forward);
  if (lengthSquared(f) <= kEpsilon) return {};
  Vec3 r = normalize(cross(up, f));
  if (lengthSquared(r) <= kEpsilon) r = {1.0f, 0.0f, 0.0f};
  const Vec3 u = cross(f, r);
  const float m00 = r.x, m01 = u.x, m02 = f.x;
  const float m10 = r.y, m11 = u.y, m12 = f.y;
  const float m20 = r.z, m21 = u.z, m22 = f.z;
  const float trace = m00 + m11 + m22;
  Quat q{};
  if (trace > 0.0f) {
    const float s = std::sqrt(trace + 1.0f) * 2.0f;
    q.w = 0.25f * s;
    q.x = (m21 - m12) / s;
    q.y = (m02 - m20) / s;
    q.z = (m10 - m01) / s;
  } else if (m00 > m11 && m00 > m22) {
    const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
    q.w = (m21 - m12) / s;
    q.x = 0.25f * s;
    q.y = (m01 + m10) / s;
    q.z = (m02 + m20) / s;
  } else if (m11 > m22) {
    const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
    q.w = (m02 - m20) / s;
    q.x = (m01 + m10) / s;
    q.y = 0.25f * s;
    q.z = (m12 + m21) / s;
  } else {
    const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
    q.w = (m10 - m01) / s;
    q.x = (m02 + m20) / s;
    q.y = (m12 + m21) / s;
    q.z = 0.25f * s;
  }
  return quatNormalize(q);
}

inline bool mat4Decompose(const Mat4& m, Vec3& outPosition, Quat& outRotation, Vec3& outScale) {
  outPosition = {m[3][0], m[3][1], m[3][2]};
  Vec3 col0{m[0][0], m[0][1], m[0][2]};
  Vec3 col1{m[1][0], m[1][1], m[1][2]};
  Vec3 col2{m[2][0], m[2][1], m[2][2]};
  outScale = {length(col0), length(col1), length(col2)};
  if (outScale.x <= kEpsilon || outScale.y <= kEpsilon || outScale.z <= kEpsilon) return false;
  col0 /= outScale.x;
  col1 /= outScale.y;
  col2 /= outScale.z;
  const float trace = col0.x + col1.y + col2.z;
  if (trace > 0.0f) {
    const float s = std::sqrt(trace + 1.0f) * 2.0f;
    outRotation = quatNormalize({(col1.z - col2.y) / s, (col2.x - col0.z) / s,
                                 (col0.y - col1.x) / s, 0.25f * s});
  } else if (col0.x > col1.y && col0.x > col2.z) {
    const float s = std::sqrt(1.0f + col0.x - col1.y - col2.z) * 2.0f;
    outRotation = quatNormalize({0.25f * s, (col1.x + col0.y) / s,
                                 (col2.x + col0.z) / s, (col1.z - col2.y) / s});
  } else if (col1.y > col2.z) {
    const float s = std::sqrt(1.0f + col1.y - col0.x - col2.z) * 2.0f;
    outRotation = quatNormalize({(col1.x + col0.y) / s, 0.25f * s,
                                 (col2.y + col1.z) / s, (col2.x - col0.z) / s});
  } else {
    const float s = std::sqrt(1.0f + col2.z - col0.x - col1.y) * 2.0f;
    outRotation = quatNormalize({(col2.x + col0.z) / s, (col2.y + col1.z) / s,
                                 0.25f * s, (col0.y - col1.x) / s});
  }
  return true;
}

inline bool rayIntersectsPlane(const Ray& ray, const Plane& plane, float& outDistance, Vec3* outPoint = nullptr) {
  const float denom = dot(plane.normal, ray.direction);
  if (std::fabs(denom) <= kEpsilon) return false;
  const float t = -(dot(plane.normal, ray.origin) + plane.distance) / denom;
  if (t < 0.0f) return false;
  outDistance = t;
  if (outPoint) *outPoint = ray.origin + ray.direction * t;
  return true;
}

inline Vec3 closestPointOnSegment(Vec3 p, Vec3 a, Vec3 b) {
  const Vec3 ab = b - a;
  const float denom = dot(ab, ab);
  if (denom <= kEpsilon) return a;
  const float t = clamp(dot(p - a, ab) / denom, 0.0f, 1.0f);
  return a + ab * t;
}

inline void closestPointsBetweenSegments(Vec3 p1, Vec3 q1, Vec3 p2, Vec3 q2, Vec3& c1, Vec3& c2) {
  const Vec3 d1 = q1 - p1;
  const Vec3 d2 = q2 - p2;
  const Vec3 r = p1 - p2;
  const float a = dot(d1, d1);
  const float e = dot(d2, d2);
  const float f = dot(d2, r);
  float s = 0.0f, t = 0.0f;
  if (a <= kEpsilon && e <= kEpsilon) {
    c1 = p1; c2 = p2; return;
  }
  if (a <= kEpsilon) {
    s = 0.0f; t = clamp(f / e, 0.0f, 1.0f);
  } else {
    const float c = dot(d1, r);
    if (e <= kEpsilon) {
      t = 0.0f; s = clamp(-c / a, 0.0f, 1.0f);
    } else {
      const float b = dot(d1, d2);
      const float denom = a * e - b * b;
      s = denom != 0.0f ? clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
      t = (b * s + f) / e;
      if (t < 0.0f) {
        t = 0.0f; s = clamp(-c / a, 0.0f, 1.0f);
      } else if (t > 1.0f) {
        t = 1.0f; s = clamp((b - c) / a, 0.0f, 1.0f);
      }
    }
  }
  c1 = p1 + d1 * s;
  c2 = p2 + d2 * t;
}

}  // namespace nimbus::math
