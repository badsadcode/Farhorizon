#include "engine/core/clock.h"

#include <algorithm>

namespace fh {

int FixedStepClock::advance(double realDeltaSeconds) {
  accumulator_ += std::max(0.0, realDeltaSeconds);
  int steps = 0;
  while (accumulator_ >= step_ && steps < maxSteps_) {
    accumulator_ -= step_;
    ++steps;
  }
  if (accumulator_ >= step_) {
    // Too far behind: drop the backlog rather than trying to catch up forever.
    const double keep = accumulator_ - step_ * static_cast<int>(accumulator_ / step_);
    dropped_ += accumulator_ - keep;
    accumulator_ = keep;
  }
  totalSteps_ += static_cast<std::uint64_t>(steps);
  return steps;
}

}  // namespace fh
