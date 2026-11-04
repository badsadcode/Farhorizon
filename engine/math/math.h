#pragma once
// Small, test-covered linear algebra for FARHORIZON.
// Conventions (see docs/decisions/0002-coordinates-and-precision.md):
//  * Right-handed, +Y up, cameras look down -Z (matches glTF).
//  * Matrices are column-major (m[col][row]) and multiply column vectors, like GLSL.
//  * Projection targets Vulkan clip space (Y down, depth 0..1) with reversed-Z.
#include <cmath>
#include <cstdint>

namespace fh {

inline constexpr float kPi = 3.14159265358979323846f;

template <class T>
struct Vec3T {
  T x{}, y{}, z{};
  constexpr Vec3T() = default;
  constexpr Vec3T(T x_, T y_, T z_) : x(x_), y(y_), z(z_) {}
  template <class U>
  constexpr explicit Vec3T(const Vec3T<U>& o) : x(T(o.x)), y(T(o.y)), z(T(o.z)) {}

  constexpr Vec3T operator+(const Vec3T& o) const { return {x + o.x, y + o.y, z + o.z}; }
  constexpr Vec3T operator-(const Vec3T& o) const { return {x - o.x, y - o.y, z - o.z}; }
  constexpr Vec3T operator-() const { return {-x, -y, -z}; }
  constexpr Vec3T operator*(T s) const { return {x * s, y * s, z * s}; }
  constexpr Vec3T operator/(T s) const { return {x / s, y / s, z / s}; }
  constexpr Vec3T operator*(const Vec3T& o) const { return {x * o.x, y * o.y, z * o.z}; }
  Vec3T& operator+=(const Vec3T& o) { x += o.x; y += o.y; z += o.z; return *this; }
  Vec3T& operator-=(const Vec3T& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
  Vec3T& operator*=(T s) { x *= s; y *= s; z *= s; return *this; }
  constexpr bool operator==(const Vec3T&) const = default;
  T& operator[](int i) { return (&x)[i]; }
  const T& operator[](int i) const { return (&x)[i]; }
};
using Vec3 = Vec3T<float>;
using DVec3 = Vec3T<double>;

template <class T> constexpr Vec3T<T> operator*(T s, const Vec3T<T>& v) { return v * s; }
template <class T> constexpr T dot(const Vec3T<T>& a, const Vec3T<T>& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
template <class T> constexpr Vec3T<T> cross(const Vec3T<T>& a, const Vec3T<T>& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
template <class T> inline T length(const Vec3T<T>& v) { return std::sqrt(dot(v, v)); }
template <class T> inline Vec3T<T> normalize(const Vec3T<T>& v) {
  const T len = length(v);
  return len > T(0) ? v / len : Vec3T<T>{};
}
template <class T> constexpr Vec3T<T> lerp(const Vec3T<T>& a, const Vec3T<T>& b, T t) { return a + (b - a) * t; }

struct Vec2 {
  float x{}, y{};
};

struct Vec4 {
  float x{}, y{}, z{}, w{};
  constexpr Vec4() = default;
  constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
  constexpr Vec4(const Vec3& v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
  constexpr Vec3 xyz() const { return {x, y, z}; }
  float& operator[](int i) { return (&x)[i]; }
  const float& operator[](int i) const { return (&x)[i]; }
};

struct Quat {
  float x = 0, y = 0, z = 0, w = 1;

  static Quat identity() { return {}; }
  static Quat fromAxisAngle(const Vec3& axis, float radians) {
    const Vec3 a = normalize(axis);
    const float s = std::sin(radians * 0.5f);
    return {a.x * s, a.y * s, a.z * s, std::cos(radians * 0.5f)};
  }
  Quat operator*(const Quat& b) const {
    return {w * b.x + x * b.w + y * b.z - z * b.y,
            w * b.y - x * b.z + y * b.w + z * b.x,
            w * b.z + x * b.y - y * b.x + z * b.w,
            w * b.w - x * b.x - y * b.y - z * b.z};
  }
  Quat conjugate() const { return {-x, -y, -z, w}; }
  // Body axes (right-handed, +Y up, -Z forward).
  Vec3 right() const { return rotate({1, 0, 0}); }
  Vec3 up() const { return rotate({0, 1, 0}); }
  Vec3 forward() const { return rotate({0, 0, -1}); }
  Vec3 rotate(const Vec3& v) const {
    const Vec3 u{x, y, z};
    const Vec3 t = cross(u, v) * 2.0f;
    return v + t * w + cross(u, t);
  }
};
inline Quat normalize(const Quat& q) {
  const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  return n > 0 ? Quat{q.x / n, q.y / n, q.z / n, q.w / n} : Quat{};
}

inline float dot(const Quat& a, const Quat& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
// Normalised lerp along the shortest arc; adequate for smoothing small per-frame rotations.
inline Quat nlerp(const Quat& a, Quat b, float t) {
  if (dot(a, b) < 0) b = {-b.x, -b.y, -b.z, -b.w};
  return normalize(Quat{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t});
}
// Integrates a body-frame angular velocity (rad/s) over dt.
inline Quat integrateBodyRate(const Quat& q, const Vec3& omegaBody, float dt) {
  const float rate = length(omegaBody);
  if (rate * dt < 1e-9f) return q;
  return normalize(q * Quat::fromAxisAngle(omegaBody / rate, rate * dt));
}

struct Mat4 {
  Vec4 c[4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};  // columns

  static Mat4 identity() { return {}; }
  float& at(int row, int col) { return c[col][row]; }
  float at(int row, int col) const { return c[col][row]; }
  const float* data() const { return &c[0].x; }

  Mat4 operator*(const Mat4& b) const {
    Mat4 r;
    for (int col = 0; col < 4; ++col)
      for (int row = 0; row < 4; ++row) {
        float s = 0;
        for (int k = 0; k < 4; ++k) s += at(row, k) * b.at(k, col);
        r.at(row, col) = s;
      }
    return r;
  }
  Vec4 operator*(const Vec4& v) const {
    Vec4 r;
    for (int row = 0; row < 4; ++row)
      r[row] = at(row, 0) * v.x + at(row, 1) * v.y + at(row, 2) * v.z + at(row, 3) * v.w;
    return r;
  }
  Vec3 transformPoint(const Vec3& p) const { return (*this * Vec4(p, 1.0f)).xyz(); }
  Vec3 transformDir(const Vec3& d) const { return (*this * Vec4(d, 0.0f)).xyz(); }

  static Mat4 translation(const Vec3& t) {
    Mat4 m;
    m.c[3] = Vec4(t, 1.0f);
    return m;
  }
  static Mat4 scale(const Vec3& s) {
    Mat4 m;
    m.c[0].x = s.x; m.c[1].y = s.y; m.c[2].z = s.z;
    return m;
  }
  static Mat4 rotation(const Quat& q) {
    Mat4 m;
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    m.c[0] = {1 - 2 * (yy + zz), 2 * (xy + wz), 2 * (xz - wy), 0};
    m.c[1] = {2 * (xy - wz), 1 - 2 * (xx + zz), 2 * (yz + wx), 0};
    m.c[2] = {2 * (xz + wy), 2 * (yz - wx), 1 - 2 * (xx + yy), 0};
    return m;
  }
  static Mat4 trs(const Vec3& t, const Quat& r, const Vec3& s) { return translation(t) * rotation(r) * scale(s); }

  // View matrix for a camera at `eye` looking at `target` (right-handed, looks down -Z).
  static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    const Vec3 f = normalize(target - eye);
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);
    Mat4 m;
    m.c[0] = {s.x, u.x, -f.x, 0};
    m.c[1] = {s.y, u.y, -f.y, 0};
    m.c[2] = {s.z, u.z, -f.z, 0};
    m.c[3] = {-dot(s, eye), -dot(u, eye), dot(f, eye), 1};
    return m;
  }

  // Infinite far plane, reversed-Z (near -> depth 1, infinity -> depth 0), Vulkan clip (Y down).
  // Gives excellent depth precision for space scenes; depth test uses GREATER, clear to 0.
  static Mat4 perspectiveReversedZInfinite(float fovYRadians, float aspect, float zNear) {
    const float f = 1.0f / std::tan(fovYRadians * 0.5f);
    Mat4 m;
    m.c[0] = {f / aspect, 0, 0, 0};
    m.c[1] = {0, -f, 0, 0};
    m.c[2] = {0, 0, 0, -1};
    m.c[3] = {0, 0, zNear, 0};
    return m;
  }

  static Mat4 orthographic(float left, float right, float top, float bottom) {
    // Maps pixel coordinates (top-left origin) to Vulkan clip space; z passes through.
    Mat4 m;
    m.c[0] = {2.0f / (right - left), 0, 0, 0};
    m.c[1] = {0, 2.0f / (bottom - top), 0, 0};
    m.c[3] = {-(right + left) / (right - left), -(bottom + top) / (bottom - top), 0, 1};
    return m;
  }

  Mat4 transposed() const {
    Mat4 r;
    for (int i = 0; i < 4; ++i)
      for (int j = 0; j < 4; ++j) r.at(i, j) = at(j, i);
    return r;
  }
  Mat4 inverse() const;  // general 4x4 inverse (math.cpp-free inline below)
};

inline Mat4 Mat4::inverse() const {
  const float* m = data();
  float inv[16];
  inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
  inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
  inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
  inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
  inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
  inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
  inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
  inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
  inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
  inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
  inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
  inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
  inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
  inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
  inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
  inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
  float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
  Mat4 r;
  if (std::fabs(det) < 1e-20f) return r;
  det = 1.0f / det;
  float* o = &r.c[0].x;
  for (int i = 0; i < 16; ++i) o[i] = inv[i] * det;
  return r;
}

// View frustum for culling, extracted from a reversed-Z infinite projection * view matrix.
// Planes: left, right, bottom, top, near (the far plane is at infinity). ax+by+cz+d >= 0 is inside.
struct Frustum {
  Vec4 planes[5];
  static Frustum fromViewProj(const Mat4& m) {
    auto row = [&](int r) { return Vec4(m.at(r, 0), m.at(r, 1), m.at(r, 2), m.at(r, 3)); };
    const Vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    auto add = [](const Vec4& a, const Vec4& b, float s) { return Vec4(a.x + s * b.x, a.y + s * b.y, a.z + s * b.z, a.w + s * b.w); };
    Frustum f;
    f.planes[0] = add(r3, r0, 1);
    f.planes[1] = add(r3, r0, -1);
    f.planes[2] = add(r3, r1, 1);
    f.planes[3] = add(r3, r1, -1);
    f.planes[4] = add(r3, r2, -1);  // reversed-Z: near plane is z_ndc <= 1
    for (auto& p : f.planes) {
      const float n = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
      if (n > 0) p = Vec4(p.x / n, p.y / n, p.z / n, p.w / n);
    }
    return f;
  }
  bool sphereVisible(const Vec3& c, float r) const {
    for (const auto& p : planes)
      if (p.x * c.x + p.y * c.y + p.z * c.z + p.w < -r) return false;
    return true;
  }
};

// Rotation part of a matrix (columns are normalised first, so uniform/non-uniform scale is ignored).
inline Quat quatFromMatrix(const Mat4& m) {
  const Vec3 c0 = normalize(m.c[0].xyz()), c1 = normalize(m.c[1].xyz()), c2 = normalize(m.c[2].xyz());
  const float m00 = c0.x, m11 = c1.y, m22 = c2.z;
  Quat q;
  const float tr = m00 + m11 + m22;
  if (tr > 0) {
    const float s = std::sqrt(tr + 1.0f) * 2.0f;
    q = {(c1.z - c2.y) / s, (c2.x - c0.z) / s, (c0.y - c1.x) / s, 0.25f * s};
  } else if (m00 > m11 && m00 > m22) {
    const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
    q = {0.25f * s, (c1.x + c0.y) / s, (c2.x + c0.z) / s, (c1.z - c2.y) / s};
  } else if (m11 > m22) {
    const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
    q = {(c1.x + c0.y) / s, 0.25f * s, (c2.y + c1.z) / s, (c2.x - c0.z) / s};
  } else {
    const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
    q = {(c2.x + c0.z) / s, (c2.y + c1.z) / s, 0.25f * s, (c0.y - c1.x) / s};
  }
  return normalize(q);
}

inline constexpr float radians(float deg) { return deg * (kPi / 180.0f); }
template <class T> constexpr T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace fh
