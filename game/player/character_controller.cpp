#include "game/player/character_controller.h"

#include <algorithm>
#include <cmath>

namespace fh {

Quat CharacterState::viewRotation() const {
  return normalize(Quat::fromAxisAngle({0, 1, 0}, -yaw) * Quat::fromAxisAngle({1, 0, 0}, pitch));
}

namespace {
// Pushes the capsule out of every collider. Returns true if it is standing on something.
bool resolve(CharacterState& s, const CharacterParams& p, const std::vector<Aabb>& colliders,
             const TriangleCollider* mesh) {
  bool grounded = false;
  static thread_local std::vector<std::uint32_t> nearby;
  const float offsets[3] = {p.radius, p.height * 0.5f, p.height - p.radius};
  for (int iter = 0; iter < 4; ++iter) {
    bool any = false;
    for (const Aabb& box : colliders) {
      for (float off : offsets) {
        Contact c;
        const Vec3 centre = s.feet + Vec3{0, off, 0};
        if (!sphereVsAabb(centre, p.radius, box, c)) continue;
        any = true;
        s.feet += c.normal * c.depth;
        const float vn = dot(s.velocity, c.normal);
        if (vn < 0) s.velocity -= c.normal * vn;  // slide: remove the component into the surface
        if (c.normal.y > 0.65f && off == offsets[0]) grounded = true;
      }
    }
    if (mesh && !mesh->empty()) {
      for (float off : offsets) {
        mesh->nearby(s.feet + Vec3{0, off, 0}, p.radius, nearby);
        for (std::uint32_t tri : nearby) {
          Contact c;
          if (!mesh->sphereVsTriangle(tri, s.feet + Vec3{0, off, 0}, p.radius, c)) continue;
          any = true;
          s.feet += c.normal * c.depth;
          const float vn = dot(s.velocity, c.normal);
          if (vn < 0) s.velocity -= c.normal * vn;
          if (c.normal.y > 0.65f && off == offsets[0]) grounded = true;
        }
      }
    }
    if (!any) break;
  }
  return grounded;
}
}  // namespace

void stepCharacter(CharacterState& s, const CharacterInput& in, const CharacterParams& p,
                   const std::vector<Aabb>& colliders, float dt, const HeightField* terrain,
                   const TriangleCollider* mesh) {
  if (dt <= 0) return;
  s.yaw += in.yawDelta;
  s.pitch = clamp(s.pitch + in.pitchDelta, radians(-85.0f), radians(85.0f));

  // Desired horizontal velocity from input, relative to yaw.
  const Vec3 fwd{std::sin(s.yaw), 0, -std::cos(s.yaw)};
  const Vec3 right{std::cos(s.yaw), 0, std::sin(s.yaw)};
  Vec3 wish = fwd * in.move.y + right * in.move.x;
  const float wl = length(wish);
  if (wl > 1.0f) wish = wish / wl;
  const Vec3 target = wish * (in.run ? p.runSpeed : p.walkSpeed);
  const float accel = (s.grounded ? p.groundAccel : p.airAccel) * dt;
  Vec3 horiz{s.velocity.x, 0, s.velocity.z};
  const Vec3 dv = target - horiz;
  const float dvl = length(dv);
  horiz += dvl > accel ? dv * (accel / dvl) : dv;
  s.velocity.x = horiz.x;
  s.velocity.z = horiz.z;

  if (in.jump && s.grounded) s.velocity.y = p.jumpSpeed;
  s.velocity.y -= p.gravity * dt;

  // Sub-step so no step moves more than half a radius (no tunnelling through thin walls).
  const float travel = length(s.velocity) * dt;
  const int steps = std::clamp(int(std::ceil(travel / (p.radius * 0.5f))), 1, 32);
  const float h = dt / float(steps);
  bool grounded = false;
  for (int i = 0; i < steps; ++i) {
    const Vec3 before = s.feet;
    s.feet += s.velocity * h;
    grounded = resolve(s, p, colliders, mesh) || grounded;
    if (terrain && terrain->valid()) {
      const float ground = terrain->heightAt(s.feet.x, s.feet.z);
      if (s.feet.y < ground + 0.02f) {
        const Vec3 n = terrain->normalAt(s.feet.x, s.feet.z);
        if (n.y >= 0.57f || ground - before.y < 0.002f) {  // walkable slope: stand on it
          s.feet.y = std::max(s.feet.y, ground);
          if (s.velocity.y < 0) s.velocity.y = 0;
          grounded = true;
        } else {  // cliff: undo the horizontal move and kill velocity into the slope
          s.feet.x = before.x;
          s.feet.z = before.z;
          s.feet.y = std::max(before.y, terrain->heightAt(before.x, before.z));
          const Vec3 into = normalize(Vec3{n.x, 0, n.z});
          const float vn = dot(s.velocity, into);
          if (vn < 0) s.velocity -= into * vn;
        }
      }
    }
  }
  s.grounded = grounded;
  if (s.grounded && s.velocity.y < 0) s.velocity.y = 0;
}

}  // namespace fh
