#include "game/foot/foot_view.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "game/session/game_session.h"

namespace fh {

namespace {

const Vec3 kGunRest{0.2f, -0.215f, -0.43f};
const Vec3 kMuzzleOnGun{0, 0.085f, -0.33f};

Quat aboutX(float a) { return Quat::fromAxisAngle({1, 0, 0}, a); }
Quat aboutY(float a) { return Quat::fromAxisAngle({0, 1, 0}, a); }
Quat aboutZ(float a) { return Quat::fromAxisAngle({0, 0, 1}, a); }

// A little repeatable scatter: effects only, but no reason to be different each run.
float scatter(std::uint32_t& seed) {
  seed = seed * 1664525u + 1013904223u;
  return float((seed >> 16) & 0x7fff) / 16383.5f - 1.0f;
}

}  // namespace

void FootView::reset() {
  particles_.clear();
  kick_ = flash_ = reloadTurn_ = wallPull_ = hurtFlash_ = hitMark_ = blackout_ = 0;
}

void FootView::burst(const Vec3& at, const Vec3& away, const Vec3& color, int count, float speed, ParticleFrame frame) {
  for (int i = 0; i < count; ++i) {
    Particle p;
    p.position = at;
    const Vec3 jitter{scatter(sparkSeed_), scatter(sparkSeed_), scatter(sparkSeed_)};
    p.velocity = (away * 0.8f + jitter * 0.7f) * (speed * (0.4f + 0.6f * std::fabs(scatter(sparkSeed_))));
    p.color = color;
    p.life = 0.16f + 0.2f * std::fabs(scatter(sparkSeed_));
    p.sizeStart = frame == ParticleFrame::Puff ? 0.05f : 0.012f;
    p.sizeEnd = frame == ParticleFrame::Puff ? 0.14f : 0.003f;
    p.stretch = frame == ParticleFrame::Spark ? 0.05f : 0.0f;
    p.frame = frame;
    particles_.add(p);
  }
}

void FootView::update(float dt, const std::vector<FootEvent>& events, const GameSession& session) {
  particles_.update(dt);
  const float falloff = dt > 0 ? std::exp(-dt * 13.0f) : 1.0f;
  kick_ *= falloff;
  flash_ = std::max(0.0f, flash_ - dt / 0.055f);
  hurtFlash_ = std::max(0.0f, hurtFlash_ - dt / 0.5f);
  hitMark_ = std::max(0.0f, hitMark_ - dt / 0.16f);
  blackout_ = std::max(0.0f, blackout_ - dt / 1.8f);

  const CharacterState& body = session.character();
  const float speed = length(Vec3{body.velocity.x, 0, body.velocity.z});
  bobPhase_ += speed * dt * 2.4f;
  bobAmount_ += (std::min(speed / 5.5f, 1.0f) - bobAmount_) * std::min(1.0f, dt * 8.0f);

  if (!session.footPlayers().empty()) {
    const FootPlayer& me = session.footPlayers().front();
    const float reloadTime = session.foot().gun().reloadSeconds;
    reloadTurn_ = me.reloading() ? 1.0f - me.reloadLeft / reloadTime : 0.0f;
    float wall = 1.0f;
    const InteriorDef* def = session.activeInterior();
    if (def && def->meshCollider && !def->meshCollider->raycast(me.eye, me.look, 0.7f, wall)) wall = 0.7f;
    wallPull_ += (std::max(0.0f, 0.7f - wall) - wallPull_) * std::min(1.0f, dt * 18.0f);
  }

  for (const FootEvent& e : events) {
    Vec3 back = e.from - e.position;
    if (const float n = length(back); n > 1e-3f) back = back / n;
    switch (e.type) {
      case FootEvent::Type::Shot: {
        kick_ = 1.0f;
        flash_ = 1.0f;
        const Vec3 start = e.from + Quat(session.character().viewRotation()).rotate(muzzleOffset());
        const Vec3 line = e.position - start;
        const float dist = length(line);
        if (dist > 0.5f) {
          Particle tracer;
          tracer.position = start;
          tracer.velocity = line / dist * 110.0f;
          tracer.color = {7.0f, 4.6f, 1.8f};
          tracer.life = dist / 110.0f;
          tracer.sizeStart = tracer.sizeEnd = 0.009f;
          tracer.stretch = 0.45f;
          tracer.fadeIn = 0.0f;
          tracer.frame = ParticleFrame::Streak;
          particles_.add(tracer);
        }
        break;
      }
      case FootEvent::Type::HitWall:
        burst(e.position + back * 0.02f, back, {7.0f, 4.5f, 1.6f}, 7, 4.5f, ParticleFrame::Spark);
        burst(e.position + back * 0.03f, back, {0.25f, 0.23f, 0.2f}, 2, 0.5f, ParticleFrame::Puff);
        break;
      case FootEvent::Type::HitEnemy:
        hitMark_ = 1.0f;
        burst(e.position, back, {0.55f, 0.12f, 0.03f}, 7, 2.8f, ParticleFrame::Puff);
        burst(e.position, back, {5.0f, 2.2f, 0.7f}, 4, 3.5f, ParticleFrame::Spark);
        break;
      case FootEvent::Type::EnemyDied:
        burst(e.position, {0, 1, 0}, {0.45f, 0.1f, 0.03f}, 16, 2.4f, ParticleFrame::Puff);
        break;
      case FootEvent::Type::PlayerHurt:
        hurtFlash_ = 1.0f;
        break;
      case FootEvent::Type::PlayerDown:
        blackout_ = 1.0f;
        break;
      default:
        break;
    }
  }
}

Vec3 FootView::muzzleOffset() const { return kGunRest + kMuzzleOnGun; }

void FootView::draw(const GameSession& session, const FootMeshes& meshes, const Vec3& viewPosition, const Quat& viewRotation,
                    std::vector<MeshDrawItem>& items, std::vector<GlowSprite>& glows) const {
  // ---- Crawlers ----
  for (const FootEnemy& e : session.foot().enemies()) {
    const bool dead = e.state == CrawlerState::Dead;
    const float fall = dead ? std::clamp(e.deadTime / 0.35f, 0.0f, 1.0f) : 0.0f;
    const float rear = e.state == CrawlerState::WindingUp ? 0.38f : 0.0f;
    const float bob = dead ? 0.0f : std::sin(e.stride * 2.0f) * 0.012f;
    const Vec3 at = e.body.feet + Vec3{0, bob - fall * 0.26f, 0};
    const Mat4 base = Mat4::translation(at - viewPosition) * Mat4::rotation(aboutY(-e.body.yaw) * aboutX(rear + fall * 0.08f));
    items.push_back({dead ? meshes.crawlerDead : meshes.crawlerBody, base});
    for (int row = 0; row < 3; ++row) {
      for (int side = -1; side <= 1; side += 2) {
        const float phase = e.stride + float(row) * 2.09f + (side > 0 ? kPi : 0.0f);
        const float swing = dead ? 0.0f : std::sin(phase) * 0.34f;
        const float lift = dead ? fall * 1.1f : std::max(0.0f, std::cos(phase)) * 0.26f;
        const float splay = (float(row) - 1.0f) * 0.42f;
        const Vec3 hip{float(side) * 0.23f, 0.42f, (float(row) - 1.0f) * 0.27f};
        const Quat turn = side > 0 ? aboutY(splay + swing) : aboutY(kPi - splay - swing);
        items.push_back({meshes.crawlerLeg, base * Mat4::translation(hip) * Mat4::rotation(turn * aboutZ(lift))});
      }
    }
  }

  // ---- Loot still lying around ----
  if (const InteriorDef* def = session.activeInterior()) {
    for (int i = 0; i < int(def->markers.size()); ++i) {
      const InteriorMarker& m = def->markers[size_t(i)];
      if (m.type != "loot" || session.markerCleared(i)) continue;
      const bool medical = m.kind == "medical";
      items.push_back({medical ? meshes.lootMedical : meshes.lootSalvage,
                       Mat4::translation(m.position - viewPosition) * Mat4::rotation(aboutY(radians(m.yawDeg)))});
      const Vec3 tint = medical ? Vec3{0.25f, 1.6f, 0.65f} : Vec3{1.6f, 0.9f, 0.2f};
      glows.push_back({Vec4(m.position + Vec3{0, 0.4f, 0} - viewPosition, 0.09f), Vec4(tint, 0)});
    }
  }

  // ---- The gun in your hands ----
  if (session.footArmed()) {
    const float dip = std::sin(reloadTurn_ * kPi);
    Vec3 offset = kGunRest;
    offset.x += std::cos(bobPhase_) * 0.006f * bobAmount_;
    offset.y += std::fabs(std::sin(bobPhase_)) * 0.009f * bobAmount_ - dip * 0.09f;
    offset.z += kick_ * 0.045f + wallPull_ * 0.34f;
    const Quat tilt = aboutX(kick_ * 0.1f - dip * 0.75f - wallPull_ * 1.3f) * aboutZ(dip * 0.35f);
    items.push_back({meshes.gun, Mat4::translation(viewRotation.rotate(offset)) * Mat4::rotation(viewRotation * tilt), false});
    if (flash_ > 0)
      glows.push_back({Vec4(viewRotation.rotate(offset + kMuzzleOnGun), 0.1f * (0.6f + 0.4f * flash_)),
                       Vec4(Vec3{7.0f, 4.6f, 1.9f} * flash_, 0)});
  }
}

void FootView::addLights(const GameSession& session, const Quat& viewRotation, std::vector<PointLight>& lights) const {
  // A weak lamp on the suit: derelicts are dark, and you need to see what the red eyes belong to.
  if (suitLamp_ && session.footArmed())
    lights.insert(lights.begin(), PointLight{viewRotation.rotate({-0.18f, 0.16f, 0.1f}), 16.0f, Vec3{1.5f, 1.45f, 1.3f}});
  if (flash_ <= 0) return;
  // Ahead of the barrel, so the gun does not throw a huge shadow of itself. First in the list, so a
  // full scene loses its last lamp for an instant rather than the flash.
  const Vec3 at = viewRotation.rotate(muzzleOffset() + Vec3{0, 0, -0.3f});
  lights.insert(lights.begin(), PointLight{at, 10.0f, Vec3{4.5f, 3.0f, 1.3f} * flash_});
}

void FootView::buildSprites(const Vec3& viewPosition, std::vector<ParticleSprite>& out) const {
  particles_.buildSprites(viewPosition, {}, out);
}

}  // namespace fh
