#include "engine/core/clock.h"
#include "test_framework.h"

using namespace fh;

TEST(fixed_step_accumulates) {
  FixedStepClock c(1.0 / 60.0, 5);
  CHECK(c.advance(1.0 / 120.0) == 0);
  CHECK(c.advance(1.0 / 120.0) == 1);
  CHECK(c.advance(3.0 / 60.0 + 1e-9) == 3);
  CHECK(c.totalSteps() == 4u);
  CHECK(c.alpha() >= 0.0 && c.alpha() < 1.0);
}

TEST(fixed_step_caps_catch_up) {
  FixedStepClock c(1.0 / 60.0, 5);
  CHECK(c.advance(2.0) == 5);  // a 2 s hitch simulates only 5 steps
  CHECK(c.droppedSeconds() > 1.8);
  CHECK(c.advance(1.0 / 60.0 + 1e-9) == 1);  // and recovers immediately
  CHECK(c.advance(-1.0) == 0);               // negative deltas ignored
}
