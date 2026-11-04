#pragma once
// Coordinate frames for astronomical scale (design doc 4.3, ADR 0002).
//
//  SystemPosition : double precision, metres, star-system frame. Persistent/authoritative.
//  LocalFrame     : a floating origin (double) near the active play area. Everything that is
//                   rendered or simulated at close range uses float positions relative to it.
//
// Rebase rule: when the focus point drifts more than `rebaseDistance` from the origin, the
// origin jumps to the focus point and every active local transform is shifted by the same delta.
#include "engine/math/math.h"

namespace fh {

struct SystemPosition {
  DVec3 metres;
  constexpr bool operator==(const SystemPosition&) const = default;
};

class LocalFrame {
 public:
  explicit LocalFrame(SystemPosition origin = {}, double rebaseDistance = 4096.0)
      : origin_(origin), rebaseDistance_(rebaseDistance) {}

  const SystemPosition& origin() const { return origin_; }
  double rebaseDistance() const { return rebaseDistance_; }
  std::uint32_t generation() const { return generation_; }  // increments on every rebase

  Vec3 toLocal(const SystemPosition& p) const { return Vec3(p.metres - origin_.metres); }
  SystemPosition toSystem(const Vec3& local) const { return {origin_.metres + DVec3(local)}; }

  bool needsRebase(const Vec3& localFocus) const {
    return static_cast<double>(length(localFocus)) > rebaseDistance_;
  }

  // Moves the origin to `newOrigin`. Returns the float delta that must be ADDED to every
  // active local position so it keeps the same system position.
  Vec3 rebase(const SystemPosition& newOrigin) {
    const DVec3 delta = origin_.metres - newOrigin.metres;
    origin_ = newOrigin;
    ++generation_;
    return Vec3(delta);
  }

 private:
  SystemPosition origin_;
  double rebaseDistance_;
  std::uint32_t generation_ = 0;
};

}  // namespace fh
