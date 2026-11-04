#include "game/player/character_controller.h"
#include "test_framework.h"

using namespace fh;

namespace {
constexpr float kDt = 1.0f / 60.0f;
std::vector<Aabb> room() {
  // 10 x 10 m room, floor top at y=0, walls 3 m high.
  return {{{-5, -1, -5}, {5, 0, 5}}, {{5, 0, -5}, {6, 3, 5}}, {{-6, 0, -5}, {-5, 3, 5}},
          {{-5, 0, 5}, {5, 3, 6}}, {{-5, 0, -6}, {5, 3, -5}}};
}
void run(CharacterState& s, const CharacterInput& in, const std::vector<Aabb>& w, float seconds) {
  CharacterParams p;
  for (int i = 0; i < int(seconds / kDt); ++i) stepCharacter(s, in, p, w, kDt);
}
}  // namespace

TEST(character_lands_and_stands_on_floor) {
  CharacterState s;
  s.feet = {0, 2, 0};
  run(s, {}, room(), 2.0f);
  CHECK(s.grounded);
  CHECK_NEAR(s.feet.y, 0.0, 0.02);
  CHECK_NEAR(length(s.velocity), 0.0, 0.05);
}

TEST(character_walks_and_is_stopped_by_walls) {
  CharacterState s;
  s.feet = {0, 0.01f, 0};
  CharacterInput in;
  in.move = {0, 1};  // forward = -Z
  run(s, in, room(), 6.0f);
  CHECK(s.feet.z < -4.0f);
  CHECK(s.feet.z >= -5.0f + 0.35f - 0.02f);  // radius keeps it off the wall
  CHECK(s.grounded);
}

TEST(character_slides_along_walls) {
  CharacterState s;
  s.feet = {0, 0.01f, -4.5f};
  s.yaw = radians(45.0f);  // diagonal into the -Z wall, heading +X
  CharacterInput in;
  in.move = {0, 1};
  run(s, in, room(), 1.0f);
  CHECK(s.feet.x > 1.0f);  // kept moving sideways
  CHECK(s.feet.z >= -5.0f + 0.35f - 0.02f);
}

TEST(character_does_not_tunnel_through_thin_walls) {
  std::vector<Aabb> w = room();
  w.push_back({{-5, 0, -0.05f}, {5, 3, 0.05f}});  // 10 cm partition across the room
  CharacterState s;
  s.feet = {0, 0.01f, 2};
  CharacterInput in;
  in.move = {0, 1};
  in.run = true;
  run(s, in, w, 3.0f);
  CHECK(s.feet.z > 0.0f);  // still on the near side
}

TEST(character_jumps_and_falls_back) {
  CharacterState s;
  s.feet = {0, 0.01f, 0};
  run(s, {}, room(), 0.2f);
  CharacterInput jump;
  jump.jump = true;
  CharacterParams p;
  stepCharacter(s, jump, p, room(), kDt);
  CHECK(s.velocity.y > 3.0f);
  run(s, {}, room(), 0.2f);
  CHECK(s.feet.y > 0.3f);
  run(s, {}, room(), 2.0f);
  CHECK(s.grounded);
}
