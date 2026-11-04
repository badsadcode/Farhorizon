#pragma once
// First-person capsule controller (design doc 5.1 / 9.4): collide-and-slide against axis-aligned
// boxes with gravity. The capsule is approximated by three spheres stacked along +Y, which is
// robust for floors, walls and small lips. Deterministic and GPU-free; unit-tested.
#include <vector>

#include "engine/math/math.h"
#include "game/physics/collision.h"
#include "game/world/terrain.h"

namespace fh {

struct CharacterParams {
  float radius = 0.35f;
  float height = 1.8f;
  float eyeHeight = 1.65f;
  float walkSpeed = 3.0f;    // m/s
  float runSpeed = 5.5f;
  float groundAccel = 30.0f; // m/s^2
  float airAccel = 4.0f;
  float jumpSpeed = 4.2f;
  float gravity = 9.81f;
};

struct CharacterInput {
  Vec2 move{};          // x strafe right, y forward, each in [-1,1]
  bool run = false;
  bool jump = false;
  float yawDelta = 0;   // radians (positive turns right)
  float pitchDelta = 0; // radians (positive looks up)
};

struct CharacterState {
  Vec3 feet{};          // position of the capsule bottom
  Vec3 velocity{};
  float yaw = 0;        // 0 looks down -Z, positive turns right
  float pitch = 0;
  bool grounded = false;

  Vec3 eye(const CharacterParams& p) const { return feet + Vec3{0, p.eyeHeight, 0}; }
  Quat viewRotation() const;
  Vec3 lookDirection() const { return viewRotation().forward(); }
};

// `terrain` (optional): walkable heightfield; slopes steeper than ~55 degrees block movement.
void stepCharacter(CharacterState& s, const CharacterInput& in, const CharacterParams& p,
                   const std::vector<Aabb>& colliders, float dt, const HeightField* terrain = nullptr,
                   const TriangleCollider* mesh = nullptr);

}  // namespace fh
