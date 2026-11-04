#include <cstring>

#include "engine/debug/overlay.h"
#include "engine/render/starfield.h"
#include "test_framework.h"

using namespace fh;

TEST(starfield_is_deterministic_and_valid) {
  StarfieldParams p;
  p.seed = 7741;
  p.count = 2000;
  const auto a = generateStarfield(p);
  const auto b = generateStarfield(p);
  CHECK(a.size() == 2000u);
  bool identical = true;
  for (size_t i = 0; i < a.size(); ++i)
    if (std::memcmp(&a[i], &b[i], sizeof(StarInstance)) != 0) identical = false;
  CHECK(identical);
  for (const auto& s : a) {
    CHECK_NEAR(length(s.dirSize.xyz()), 1.0, 1e-4);
    CHECK(s.dirSize.w > 0.0f);
    CHECK(s.color.x >= 0.0f && s.color.y >= 0.0f && s.color.z >= 0.0f);
  }
  p.seed = 7742;
  const auto c = generateStarfield(p);
  CHECK(std::memcmp(&a[0], &c[0], sizeof(StarInstance)) != 0);
}

TEST(rng_reference_values) {
  // Pins the RNG output so content seeds stay stable across compilers and refactors.
  Rng r(0);
  CHECK(r.next() == 0xE220A8397B1DCDAFull);
  CHECK(r.next() == 0x6E789E6AA1B965F4ull);
}

TEST(overlay_text_generates_triangles) {
  OverlayBuilder o;
  o.text(10, 10, "FPS 60", {}, 2.0f);
  CHECK(!o.vertices().empty());
  CHECK(o.vertices().size() % 3 == 0);
  CHECK(OverlayBuilder::textWidth("ABC") > OverlayBuilder::textWidth("A"));
}
