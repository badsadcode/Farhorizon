#include "engine/math/math.h"
#include "test_framework.h"

using namespace fh;

TEST(vec3_basic_ops) {
  Vec3 a{1, 2, 3}, b{4, 5, 6};
  CHECK((a + b) == Vec3(5, 7, 9));
  CHECK_NEAR(dot(a, b), 32.0, 1e-6);
  CHECK(cross(Vec3{1, 0, 0}, Vec3{0, 1, 0}) == Vec3(0, 0, 1));
  CHECK_NEAR(length(normalize(Vec3{3, 4, 12})), 1.0, 1e-6);
  CHECK(normalize(Vec3{}) == Vec3{});  // no NaN on zero vector
}

TEST(quat_rotation_matches_matrix) {
  const Quat q = Quat::fromAxisAngle({0, 1, 0}, radians(90));
  CHECK_VEC_NEAR(q.rotate({1, 0, 0}), Vec3(0, 0, -1), 1e-6);
  const Mat4 m = Mat4::rotation(q);
  CHECK_VEC_NEAR(m.transformDir({1, 0, 0}), Vec3(0, 0, -1), 1e-6);
  const Quat q2 = normalize(Quat::fromAxisAngle({1, 2, 3}, 0.7f) * Quat::fromAxisAngle({-2, 0.5f, 1}, 1.3f));
  const Vec3 v{0.3f, -1.2f, 2.0f};
  CHECK_VEC_NEAR(q2.rotate(v), Mat4::rotation(q2).transformDir(v), 1e-5);
  CHECK_VEC_NEAR(q2.conjugate().rotate(q2.rotate(v)), v, 1e-5);
}

TEST(mat4_inverse_roundtrip) {
  const Mat4 m = Mat4::trs({3, -2, 7}, Quat::fromAxisAngle({1, 1, 0}, 0.8f), {2, 2, 2});
  const Mat4 id = m * m.inverse();
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c) CHECK_NEAR(id.at(r, c), r == c ? 1.0 : 0.0, 1e-5);
}

TEST(lookat_maps_target_to_negative_z) {
  const Mat4 v = Mat4::lookAt({10, 5, 3}, {0, 0, 0}, {0, 1, 0});
  const Vec3 t = v.transformPoint({0, 0, 0});
  CHECK_NEAR(t.x, 0, 1e-5);
  CHECK_NEAR(t.y, 0, 1e-5);
  CHECK(t.z < 0);
  CHECK_NEAR(-t.z, length(Vec3{10, 5, 3}), 1e-4);
}

TEST(reversed_z_projection_depth_range) {
  const float n = 0.1f;
  const Mat4 p = Mat4::perspectiveReversedZInfinite(radians(60), 16.0f / 9.0f, n);
  auto depth = [&](float viewZ) {
    const Vec4 c = p * Vec4(0, 0, viewZ, 1);
    return c.z / c.w;
  };
  CHECK_NEAR(depth(-n), 1.0, 1e-6);          // near plane -> 1
  CHECK(depth(-1000.0f) < depth(-10.0f));      // farther -> smaller
  CHECK(depth(-1.0e9f) > 0.0f);                // never reaches 0 (infinite far)
  // Vulkan clip space has +Y down: a point above the camera maps to negative NDC y.
  const Vec4 up = p * Vec4(0, 1, -5, 1);
  CHECK(up.y / up.w < 0);
}

TEST(orthographic_pixel_mapping) {
  const Mat4 o = Mat4::orthographic(0, 1920, 0, 1080);
  CHECK_VEC_NEAR(o.transformPoint({0, 0, 0}), Vec3(-1, -1, 0), 1e-6);
  CHECK_VEC_NEAR(o.transformPoint({1920, 1080, 0}), Vec3(1, 1, 0), 1e-6);
}
