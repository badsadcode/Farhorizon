#include "game/space/landing.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "engine/core/log.h"

namespace fh {

namespace {
float smooth(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}
// Orientation whose nose points along `fwd` with `up` as close as possible to `upHint`.
Quat lookRotation(const Vec3& fwd, const Vec3& upHint) {
  const Vec3 f = normalize(fwd);
  Vec3 r = cross(f, upHint);
  if (length(r) < 1e-4f) r = cross(f, Vec3{1, 0, 0});
  r = normalize(r);
  const Vec3 u = cross(r, f);
  Mat4 m;
  m.c[0] = Vec4(r, 0);
  m.c[1] = Vec4(u, 0);
  m.c[2] = Vec4(-f, 0);
  return quatFromMatrix(m);
}
}  // namespace

const char* landStateName(LandState s) {
  switch (s) {
    case LandState::None: return "FLIGHT";
    case LandState::Descent: return "DESCENT";
    case LandState::SurfaceApproach: return "FINAL APPROACH";
    case LandState::Landed: return "LANDED";
    case LandState::SurfaceAscent: return "LIFTOFF";
    case LandState::Ascent: return "ASCENT";
  }
  return "?";
}

void Landing::say(const std::string& m, const SpaceWorld& w) {
  message_ = m;
  messageTime_ = w.simulationTime();
  log::info("landing", "{}", m);
}

std::optional<LandingSiteRef> Landing::nearestSite(const SpaceWorld& world) const {
  const DVec3 me = world.frame().toSystem(world.player().state.position).metres;
  if (const auto& t = world.target(); t && t->kind == TargetRef::Kind::Site) {
    if (length(world.planets().at(t->index).siteOrbit(t->sub) - me) <= kRequestRange)
      return LandingSiteRef{t->index, t->sub};
  }
  std::optional<LandingSiteRef> best;
  double bestD = kRequestRange;
  for (std::uint32_t p = 0; p < world.planets().size(); ++p)
    for (std::uint32_t s = 0; s < world.planets()[p].def.sites.size(); ++s) {
      const double d = length(world.planets()[p].siteOrbit(s) - me);
      if (d <= bestD) {
        bestD = d;
        best = LandingSiteRef{p, s};
      }
    }
  return best;
}

bool Landing::request(SpaceWorld& world) {
  if (busy()) {
    say("Landing sequence already in progress", world);
    return false;
  }
  auto s = nearestSite(world);
  if (!s) {
    say("No landing site in range. Travel to a site's orbit first (J).", world);
    return false;
  }
  site_ = s;
  state_ = LandState::Descent;
  stateTime_ = 0;
  descentStart_ = world.frame().toSystem(world.player().state.position).metres;
  descentStartRot_ = world.player().state.orientation;
  world.player().kinematic = true;
  world.player().collisionsEnabled = false;
  const auto& site = world.planets()[s->planet].def.sites[s->site];
  say(std::format("{} Traffic: descent corridor clear. Hold on, it gets bumpy.", site.name), world);
  return true;
}

void Landing::park(SpaceWorld& world) {
  PlayerShip& p = world.player();
  const Planet& pl = world.planets().at(site_->planet);
  p.state.position = world.frame().toLocal(SystemPosition{pl.siteOrbit(site_->site)});
  p.state.velocity = {};
  p.state.angularVelocity = {};
  p.previous = p.state;
  p.kinematic = true;
  p.collisionsEnabled = false;
}

bool Landing::landAt(SpaceWorld& world, LandingSiteRef ref) {
  if (ref.planet >= world.planets().size() || ref.site >= world.planets()[ref.planet].def.sites.size()) return false;
  site_ = ref;
  state_ = LandState::Landed;
  stateTime_ = kFadeSeconds;  // no fade-in
  park(world);
  return true;
}

bool Landing::launch(SpaceWorld& world) {
  if (state_ != LandState::Landed) {
    say("Not landed", world);
    return false;
  }
  state_ = LandState::SurfaceAscent;
  stateTime_ = 0;
  say("Liftoff. Climbing through the cloud deck.", world);
  return true;
}

void Landing::preStep(SpaceWorld& world, float dt) {
  if (state_ == LandState::None) return;
  stateTime_ += dt;
  PlayerShip& p = world.player();
  const Planet& pl = world.planets().at(site_->planet);
  const Vec3 up = Planet::siteDirection(pl.def.sites[site_->site]);

  switch (state_) {
    case LandState::Descent: {
      // Corridor: from the start point to 25 km above the site, nose down along the path.
      p.kinematic = true;
      p.collisionsEnabled = false;
      const DVec3 end = pl.siteSurface(site_->site) + DVec3(up) * 25000.0;
      const float t = smooth(float(stateTime_ / kDescentSeconds));
      const DVec3 pos = descentStart_ + (end - descentStart_) * double(t);
      const Vec3 prev = p.state.position;
      p.state.position = world.frame().toLocal(SystemPosition{pos});
      p.state.velocity = (p.state.position - prev) / std::max(dt, 1e-4f);
      const Vec3 dir = normalize(Vec3(end - descentStart_));
      p.state.orientation = nlerp(descentStartRot_, lookRotation(dir, up), std::min(1.0f, t * 3.0f));
      if (stateTime_ >= kDescentSeconds) {
        state_ = LandState::SurfaceApproach;
        stateTime_ = 0;
        park(world);
        say(std::format("Through the clouds. {} pad in sight.", pl.def.sites[site_->site].name), world);
      }
      break;
    }
    case LandState::SurfaceApproach:
      park(world);
      if (stateTime_ >= kApproachSeconds) {
        state_ = LandState::Landed;
        stateTime_ = 0;
        say(std::format("Touchdown. Welcome to {}.", pl.def.sites[site_->site].name), world);
      }
      break;
    case LandState::Landed: park(world); break;
    case LandState::SurfaceAscent:
      park(world);
      if (stateTime_ >= kSurfaceAscentSeconds) {
        state_ = LandState::Ascent;
        stateTime_ = 0;
        // Leave the orbit marker climbing radially, nose up.
        p.state.orientation = lookRotation(up, p.state.orientation.forward());
        p.state.velocity = up * 250.0f;
        p.previous = p.state;
        say("Orbit regained.", world);
      }
      break;
    case LandState::Ascent:
      p.kinematic = true;
      p.collisionsEnabled = false;
      p.state.velocity = up * 250.0f;
      if (stateTime_ >= kAscentSeconds) {
        state_ = LandState::None;
        p.kinematic = false;
        p.collisionsEnabled = true;
        say("Normal flight.", world);
      }
      break;
    case LandState::None: break;
  }
}

Transform Landing::surfaceOffset() const {
  // Approach: from 220 m up and 160 m back to the pad, rotating level; liftoff: straight up.
  if (state_ == LandState::SurfaceApproach) {
    const float t = smooth(float(stateTime_ / kApproachSeconds));
    const float h = 220.0f * (1.0f - t) * (1.0f - t) + 0.0f;
    const float back = 160.0f * (1.0f - t);
    const float pitch = radians(-12.0f) * (1.0f - t);
    return {{0, h, back}, Quat::fromAxisAngle({1, 0, 0}, pitch)};
  }
  if (state_ == LandState::SurfaceAscent) {
    const float t = float(stateTime_ / kSurfaceAscentSeconds);
    const float h = 400.0f * t * t;
    return {{0, h, -40.0f * t * t}, Quat::fromAxisAngle({1, 0, 0}, radians(18.0f) * smooth(t))};
  }
  return {};
}

float Landing::fade() const {
  const float f = float(kFadeSeconds);
  const float t = float(stateTime_);
  switch (state_) {
    case LandState::Descent: return std::clamp((t - float(kDescentSeconds) + f) / f, 0.0f, 1.0f);
    case LandState::SurfaceApproach: return std::clamp(1.0f - t / f, 0.0f, 1.0f);
    case LandState::SurfaceAscent: return std::clamp((t - float(kSurfaceAscentSeconds) + f) / f, 0.0f, 1.0f);
    case LandState::Ascent: return std::clamp(1.0f - t / f, 0.0f, 1.0f);
    default: return 0.0f;
  }
}

float Landing::heat() const {
  if (state_ != LandState::Descent) return 0.0f;
  const float t = float(stateTime_ / kDescentSeconds);
  return smooth((t - 0.25f) / 0.5f);
}

}  // namespace fh
