#include "engine/math/frames.h"
#include "test_framework.h"

using namespace fh;

static constexpr double kAU = 149597870700.0;

TEST(frame_local_system_roundtrip_at_1au) {
  LocalFrame frame({{kAU, 12000000.0, -3.5e9}});
  const SystemPosition p{{kAU + 12.345, 12000000.0 - 0.5, -3.5e9 + 1000.25}};
  const Vec3 local = frame.toLocal(p);
  CHECK_VEC_NEAR(local, Vec3(12.345f, -0.5f, 1000.25f), 1e-4);
  const SystemPosition back = frame.toSystem(local);
  CHECK_NEAR(back.metres.x, p.metres.x, 1e-3);   // sub-millimetre at 1 AU
  CHECK_NEAR(back.metres.y, p.metres.y, 1e-3);
  CHECK_NEAR(back.metres.z, p.metres.z, 1e-3);
}

TEST(float_alone_fails_at_1au_motivation) {
  // Documents why the floating origin exists: float spacing at 1 AU is ~16 km.
  const float f = static_cast<float>(kAU);
  CHECK(static_cast<double>(f + 1000.0f) == static_cast<double>(f));
}

TEST(rebase_preserves_system_positions) {
  LocalFrame frame({{kAU, 0, 0}}, 4096.0);
  // Three active objects in local space.
  std::vector<Vec3> objects = {{5000, 0, 0}, {4990, 3, -2}, {-20, 0, 1}};
  std::vector<SystemPosition> before;
  for (auto& o : objects) before.push_back(frame.toSystem(o));

  CHECK(frame.needsRebase(objects[0]));
  CHECK(!frame.needsRebase(objects[2]));
  const Vec3 delta = frame.rebase(frame.toSystem(objects[0]));
  for (auto& o : objects) o += delta;
  CHECK(frame.generation() == 1u);
  CHECK_VEC_NEAR(objects[0], Vec3(0, 0, 0), 1e-4);
  for (size_t i = 0; i < objects.size(); ++i) {
    const SystemPosition after = frame.toSystem(objects[i]);
    CHECK_NEAR(after.metres.x, before[i].metres.x, 1e-3);
    CHECK_NEAR(after.metres.y, before[i].metres.y, 1e-3);
    CHECK_NEAR(after.metres.z, before[i].metres.z, 1e-3);
  }
}

TEST(repeated_travel_with_rebasing_has_no_drift) {
  // Fly 2,000 km in 1 m/frame-ish steps with rebasing; final system position must be exact
  // to within float precision of the *local* range, not of the astronomical range.
  LocalFrame frame({{kAU, 0, 0}}, 4096.0);
  Vec3 ship{0, 0, 0};
  const Vec3 step{37.0f, -1.5f, 12.25f};
  const int steps = 50000;
  for (int i = 0; i < steps; ++i) {
    ship += step;
    if (frame.needsRebase(ship)) ship += frame.rebase(frame.toSystem(ship));
  }
  const SystemPosition end = frame.toSystem(ship);
  CHECK_NEAR(end.metres.x, kAU + 37.0 * steps, 1.0);
  CHECK_NEAR(end.metres.y, -1.5 * steps, 1.0);
  CHECK_NEAR(end.metres.z, 12.25 * steps, 1.0);
  CHECK(frame.generation() > 100u);
}
