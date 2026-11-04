#pragma once
// Fixed-timestep simulation clock (design doc 9.2): simulation steps at a fixed rate,
// rendering interpolates with alpha(), catch-up is capped to avoid a spiral of death.
#include <cstdint>

namespace fh {

class FixedStepClock {
 public:
  explicit FixedStepClock(double stepSeconds = 1.0 / 60.0, int maxStepsPerFrame = 5)
      : step_(stepSeconds), maxSteps_(maxStepsPerFrame) {}

  // Adds real elapsed time; returns how many fixed steps to simulate this frame.
  // Time beyond maxStepsPerFrame steps is dropped (counted in droppedSeconds()).
  int advance(double realDeltaSeconds);

  double step() const { return step_; }
  // Interpolation factor in [0,1) between the previous and current simulation state.
  double alpha() const { return accumulator_ / step_; }
  std::uint64_t totalSteps() const { return totalSteps_; }
  double simulationTime() const { return static_cast<double>(totalSteps_) * step_; }
  double droppedSeconds() const { return dropped_; }

 private:
  double step_;
  int maxSteps_;
  double accumulator_ = 0.0;
  double dropped_ = 0.0;
  std::uint64_t totalSteps_ = 0;
};

}  // namespace fh
