#include "game/fx/light_selector.h"

#include <algorithm>

namespace fh {

float LightSelector::score(const LampCandidate& c) { return c.brightness / (dot(c.position, c.position) + 1.0f); }

const std::vector<ActiveLamp>& LightSelector::update(const std::vector<LampCandidate>& candidates, float dt) {
  if (!started_) slotsInUse_.clear();

  // Rank everything, favouring what is already lit so two similar lamps cannot keep trading places.
  struct Ranked {
    std::uint32_t id;
    float score;
  };
  std::vector<Ranked> ranked;
  ranked.reserve(candidates.size());
  for (const LampCandidate& c : candidates) {
    float s = score(c);
    const bool lit = std::any_of(slotsInUse_.begin(), slotsInUse_.end(), [&](const Slot& u) { return u.id == c.id && u.wanted; });
    if (lit) s *= 1.5f;
    ranked.push_back({c.id, s});
  }
  std::sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) { return a.score != b.score ? a.score > b.score : a.id < b.id; });
  const size_t wantedCount = std::min(ranked.size(), size_t(slots_));

  for (Slot& u : slotsInUse_) {
    u.wanted = false;
    for (size_t i = 0; i < wantedCount; ++i) u.wanted = u.wanted || ranked[i].id == u.id;
  }
  // A lamp leaving keeps its slot while it fades out; the next one starts once a slot is free.
  const float step = started_ ? dt / kFadeSeconds : 1.0f;
  for (Slot& u : slotsInUse_) u.fade = std::clamp(u.fade + (u.wanted ? step : -step), 0.0f, 1.0f);
  std::erase_if(slotsInUse_, [&](const Slot& u) {
    if (u.wanted || u.fade > 0.0f) {
      // A lamp removed from the scene altogether has no fade to finish.
      return std::none_of(candidates.begin(), candidates.end(), [&](const LampCandidate& c) { return c.id == u.id; });
    }
    return true;
  });
  for (size_t i = 0; i < wantedCount && slotsInUse_.size() < size_t(slots_); ++i) {
    const std::uint32_t id = ranked[i].id;
    if (std::none_of(slotsInUse_.begin(), slotsInUse_.end(), [&](const Slot& u) { return u.id == id; }))
      slotsInUse_.push_back({id, started_ ? 0.0f : 1.0f, true});
  }
  started_ = true;

  active_.clear();
  for (const Slot& u : slotsInUse_) {
    const float t = u.fade;
    active_.push_back({u.id, t * t * (3.0f - 2.0f * t)});
  }
  return active_;
}

}  // namespace fh
