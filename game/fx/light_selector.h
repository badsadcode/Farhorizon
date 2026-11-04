#pragma once
// Chooses which lamps of a local scene light the frame. There are few slots, so choosing by distance
// alone lets tiny nearby lights push out big lamps, and any change of choice is a visible pop. This
// chooses by how much a lamp contributes at the viewer, resists swapping, and fades changes.
// GPU-free and unit-tested.
#include <cstdint>
#include <vector>

#include "engine/math/math.h"

namespace fh {

struct LampCandidate {
  std::uint32_t id = 0;   // stable from frame to frame (its index in the scene)
  Vec3 position{};        // relative to the viewer
  float brightness = 1;   // strongest colour channel at full strength, ignoring any flicker
};

struct ActiveLamp {
  std::uint32_t id = 0;
  float strength = 1;     // 0..1, multiply the lamp's colour by this
};

class LightSelector {
 public:
  static constexpr float kFadeSeconds = 0.35f;

  explicit LightSelector(int slots = 16) : slots_(slots) {}
  void reset() { active_.clear(); started_ = false; }
  // The first call after reset() has no fade: a scene starts fully lit.
  const std::vector<ActiveLamp>& update(const std::vector<LampCandidate>& candidates, float dt);
  // Score: light reaching the viewer's surroundings, with the same softening the shader uses.
  static float score(const LampCandidate& c);

 private:
  struct Slot {
    std::uint32_t id;
    float fade;
    bool wanted;
  };
  int slots_;
  bool started_ = false;
  std::vector<Slot> slotsInUse_;
  std::vector<ActiveLamp> active_;
};

}  // namespace fh
