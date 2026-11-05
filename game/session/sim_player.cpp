#include "game/session/sim_player.h"

#include <algorithm>
#include <cmath>

namespace fh {

namespace {
constexpr float kPi = 3.14159265f;

float wrapAngle(float a) {
  while (a > kPi) a -= 2 * kPi;
  while (a < -kPi) a += 2 * kPi;
  return a;
}

// Yaw/pitch that look along `d` (yaw 0 looks down -Z, positive turns right).
float yawOf(const Vec3& d) { return std::atan2(d.x, -d.z); }
}  // namespace

void SimPlayer::walkTo(const Vec3& target, float arriveRadius) {
  target_ = target;
  arrive_ = arriveRadius;
  walking_ = true;
  path_.clear();
  replanIn_ = 0;
  gaveUp_ = false;
  if (const CrewMember* m = session_->player(id_)) progressFrom_ = m->character.feet;
  sinceProgress_ = 0;
}

void SimPlayer::stop() {
  walking_ = false;
  path_.clear();
  if (CrewMember* m = session_->player(id_)) m->input.move = {};
}

bool SimPlayer::planPath() {
  path_.clear();
  CrewMember* m = session_->player(id_);
  if (!m || m->location == Location::Cockpit) return false;
  if (const NavGrid* nav = session_->navAt(m->interior)) return nav->findPath(m->character.feet, target_, 1.8f, path_);
  path_.push_back(target_);  // no grid: straight at it
  return true;
}

void SimPlayer::step() {
  CrewMember* m = session_->player(id_);
  FootPlayer* f = session_->footPlayer(id_);
  if (!m || !f || m->location == Location::Cockpit) return;
  CharacterState& body = m->character;
  const Vec3 eye = body.eye(session_->characterParams());
  float wantYaw = body.yaw, wantPitch = 0;
  bool turning = false;

  // Walking: head for the next corner; replan now and then in case we were pushed off the route.
  m->input.move = {};
  if (walking_) {
    const Vec3 flat{target_.x - body.feet.x, 0, target_.z - body.feet.z};
    if (length(flat) <= arrive_ && std::fabs(target_.y - body.feet.y) < 1.5f) {
      walking_ = false;
      path_.clear();
    } else {
      // Stuck means not moving. A route may lead away from the goal for a while, round a wall or up a stair.
      if (length(body.feet - progressFrom_) > 0.25f) {
        progressFrom_ = body.feet;
        sinceProgress_ = 0;
      }
      if (replanIn_-- <= 0 || path_.empty()) {
        if (!planPath()) sinceProgress_ = 1 << 20;  // nowhere to go
        replanIn_ = 60;
      }
      if (++sinceProgress_ > 240) {  // four seconds without getting closer: a person would give up too
        walking_ = false;
        gaveUp_ = true;
        path_.clear();
        return step();
      }
      while (path_.size() > 1) {
        const Vec3 c = path_.front() - body.feet;
        if (length(Vec3{c.x, 0, c.z}) > 0.3f) break;  // close: cutting a corner early can pin you against it
        path_.erase(path_.begin());
      }
      const Vec3 to = path_.empty() ? flat : path_.front() - body.feet;
      wantYaw = yawOf(Vec3{to.x, 0, to.z});
      turning = true;
      m->input.move = {0, 1};
    }
  }

  // Fighting: the nearest living enemy in range, aimed at its middle. While shooting we strafe
  // relative to the aim instead of turning away from it.
  shooting_ = false;
  if (fighting_) {
    const FootCombat* fc = session_->footCombatAt(m->interior);
    const FootEnemy* best = nullptr;
    float bestD = range_;
    if (fc)
      for (const FootEnemy& e : fc->enemies()) {
        if (e.state == EnemyState::Dead) continue;
        const float d = length(e.body.feet - body.feet);
        if (d < bestD) {
          bestD = d;
          best = &e;
        }
      }
    if (best) {
      const Vec3 aim = best->body.feet + Vec3{0, fc->enemyDef(*best).hitCentreHeight, 0} - eye;
      const float moveYaw = wantYaw;
      wantYaw = yawOf(aim);
      wantPitch = std::asin(std::clamp(aim.y / std::max(length(aim), 1e-3f), -1.0f, 1.0f));
      turning = true;
      shooting_ = std::fabs(wrapAngle(wantYaw - body.yaw)) < 0.08f;
      if (walking_) {  // keep walking the same way while facing the target
        const float rel = wrapAngle(moveYaw - wantYaw);
        m->input.move = {std::sin(rel), std::cos(rel)};
      }
    }
  }
  f->trigger = shooting_;
  if (shooting_ && f->magazines.size() > size_t(f->weapon) && f->magazines[size_t(f->weapon)] == 0) f->reloadRequest = true;
  if (turning) {
    m->input.yawDelta = wrapAngle(wantYaw - body.yaw);
    m->input.pitchDelta = wantPitch - body.pitch;
  }
}

}  // namespace fh
