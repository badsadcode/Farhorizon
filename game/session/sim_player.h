#pragma once
// A simulated crew member: drives one player of a GameSession the way a person at a keyboard would
// (turn, walk, aim, pull the trigger), with no window and no network. Used by the headless co-op
// tests and soak runs; it only touches that player's input, never the world directly.
#include <vector>

#include "game/session/game_session.h"

namespace fh {

class SimPlayer {
 public:
  SimPlayer(GameSession& session, PlayerId id) : session_(&session), id_(id) {}

  PlayerId id() const { return id_; }
  // Walk to a point in the current place (along the map's nav grid when there is one).
  void walkTo(const Vec3& target, float arriveRadius = 0.6f);
  void stop();
  bool arrived() const { return !walking_; }
  // The last walk ended without getting there (no route, or no progress for a few seconds).
  bool gaveUp() const { return gaveUp_; }
  // Turn to and shoot the nearest living enemy in sight range; stops shooting when none is left.
  void fightNearest(bool on, float range = 25.0f) {
    fighting_ = on;
    range_ = range;
  }
  bool shooting() const { return shooting_; }

  // Call once before each GameSession::fixedUpdate.
  void step();

 private:
  bool planPath();

  GameSession* session_;
  PlayerId id_;
  bool walking_ = false;
  Vec3 target_{};
  float arrive_ = 0.6f;
  std::vector<Vec3> path_;
  bool fighting_ = false;
  bool shooting_ = false;
  float range_ = 25.0f;
  int replanIn_ = 0;
  bool gaveUp_ = false;
  float bestDistance_ = 0;  // closest we have been to the target on this walk
  int sinceProgress_ = 0;   // steps since that last improved
};

}  // namespace fh
