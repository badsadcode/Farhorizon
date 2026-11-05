#include "game/foot/foot_view.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "game/session/game_session.h"

namespace fh {

namespace {

const Vec3 kGunRest{0.2f, -0.215f, -0.43f};
const Vec3 kGlobColor{0.9f, 1.6f, 0.15f};

Quat aboutX(float a) { return Quat::fromAxisAngle({1, 0, 0}, a); }
Quat aboutY(float a) { return Quat::fromAxisAngle({0, 1, 0}, a); }
Quat aboutZ(float a) { return Quat::fromAxisAngle({0, 0, 1}, a); }

// A little repeatable scatter: effects only, but no reason to be different each run.
float scatter(std::uint32_t& seed) {
  seed = seed * 1664525u + 1013904223u;
  return float((seed >> 16) & 0x7fff) / 16383.5f - 1.0f;
}

// How each body plan stands on its legs (model space, metres, before visualScale).
struct LegRig {
  int rows = 3;
  float hipX = 0.23f, hipY = 0.42f, rowSpacing = 0.27f, splay = 0.42f, swing = 0.34f, lift = 0.26f;
};
LegRig rigFor(const std::string& visual) {
  if (visual == "spitter") return {2, 0.17f, 0.6f, 0.34f, 0.5f, 0.3f, 0.22f};
  if (visual == "brute") return {2, 0.42f, 0.78f, 0.6f, 0.35f, 0.24f, 0.16f};
  return {};
}

}  // namespace

void FootView::reset() {
  particles_.clear();
  kick_ = flash_ = reloadTurn_ = switchDip_ = wallPull_ = hurtFlash_ = hitMark_ = blackout_ = 0;
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

Vec3 FootView::muzzleOffset(const GameSession& session) const {
  const FootCombat& f = session.foot();
  return kGunRest + f.weapon(session.footPlayers().front()).muzzle;
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

  const FootCombat& f = session.foot();
  const Quat view = session.character().viewRotation();
  if (!session.footPlayers().empty()) {
    const FootPlayer& me = session.footPlayers().front();
    const FootWeaponDef& w = f.weapon(me);
    reloadTurn_ = me.reloading() ? 1.0f - me.reloadLeft / std::max(0.01f, w.reloadSeconds) : 0.0f;
    switchDip_ = me.switching() ? std::sin(me.switchLeft / 0.4f * kPi) : 0.0f;
    float wall = 1.0f;
    const InteriorDef* def = session.activeInterior();
    if (def && def->meshCollider && !def->meshCollider->raycast(me.eye, me.look, 0.7f, wall)) wall = 0.7f;
    wallPull_ += (std::max(0.0f, 0.7f - wall) - wallPull_) * std::min(1.0f, dt * 18.0f);
    // The arc beam: a jittering bolt, sparks where it bites, and a gun that shivers.
    if (me.beamActive && dt > 0) {
      beamFlicker_ += dt * 37.0f;
      kick_ = std::max(kick_, 0.12f + 0.08f * std::sin(beamFlicker_ * 3.1f));
      flash_ = std::max(flash_, 0.55f + 0.45f * std::fabs(std::sin(beamFlicker_)));
      flashColor_ = w.tracerColor;
      Vec3 back = me.eye - me.beamEnd;
      if (const float n = length(back); n > 1e-3f) back = back / n;
      burst(me.beamEnd + back * 0.03f, back, w.tracerColor * 0.9f, 2, 3.5f, ParticleFrame::Spark);
    }
  }
  // Glob trails.
  for (const FootProjectile& pr : f.projectiles())
    if (dt > 0 && scatter(sparkSeed_) > 0.2f) burst(pr.position, pr.velocity * -0.02f, kGlobColor * 0.35f, 1, 0.3f, ParticleFrame::Puff);

  for (const FootEvent& e : events) {
    Vec3 back = e.from - e.position;
    if (const float n = length(back); n > 1e-3f) back = back / n;
    const bool mine = e.player == 0;
    switch (e.type) {
      case FootEvent::Type::Shot: {
        const FootWeaponDef& w = f.content().weapons[size_t(std::max(0, e.weapon))];
        if (mine) {
          kick_ = w.mode == FootFireMode::Pellets ? 1.6f : 1.0f;
          flash_ = 1.0f;
          flashColor_ = w.tracerColor;
        }
        const Vec3 start = e.from + view.rotate(kGunRest + w.muzzle);
        const Vec3 line = e.position - start;
        const float dist = length(line);
        if (dist > 0.5f) {
          Particle tracer;
          tracer.position = start;
          tracer.velocity = line / dist * 110.0f;
          tracer.color = w.tracerColor;
          tracer.life = dist / 110.0f;
          tracer.sizeStart = tracer.sizeEnd = w.mode == FootFireMode::Pellets ? 0.006f : 0.009f;
          tracer.stretch = 0.45f;
          tracer.fadeIn = 0.0f;
          tracer.frame = ParticleFrame::Streak;
          particles_.add(tracer);
        }
        break;
      }
      case FootEvent::Type::HitWall:
        burst(e.position + back * 0.02f, back, {7.0f, 4.5f, 1.6f}, 5, 4.5f, ParticleFrame::Spark);
        burst(e.position + back * 0.03f, back, {0.25f, 0.23f, 0.2f}, 1, 0.5f, ParticleFrame::Puff);
        break;
      case FootEvent::Type::HitEnemy:
        hitMark_ = 1.0f;
        burst(e.position, back, {0.55f, 0.12f, 0.03f}, 5, 2.8f, ParticleFrame::Puff);
        burst(e.position, back, {5.0f, 2.2f, 0.7f}, 3, 3.5f, ParticleFrame::Spark);
        break;
      case FootEvent::Type::EnemyDied:
        burst(e.position, {0, 1, 0}, {0.45f, 0.1f, 0.03f}, 16, 2.4f, ParticleFrame::Puff);
        break;
      case FootEvent::Type::ProjectileHit:
        burst(e.position, back, kGlobColor * 0.6f, 10, 2.2f, ParticleFrame::Puff);
        burst(e.position, back, kGlobColor * 2.0f, 6, 3.0f, ParticleFrame::Spark);
        break;
      case FootEvent::Type::EnemyCharged:
        burst(e.position + Vec3{0, 0.1f, 0}, {0, 0.4f, 0}, {0.3f, 0.27f, 0.22f}, 12, 2.0f, ParticleFrame::Puff);
        break;
      case FootEvent::Type::EnemyStunned:
        burst(e.position + Vec3{0, 1.0f, 0}, {0, 1, 0}, {6.0f, 3.0f, 0.8f}, 10, 3.0f, ParticleFrame::Spark);
        break;
      case FootEvent::Type::PlayerHurt:
        if (mine) hurtFlash_ = 1.0f;
        break;
      case FootEvent::Type::PlayerDown:
        if (mine) blackout_ = 1.0f;
        break;
      default:
        break;
    }
  }
}

void FootView::draw(const GameSession& session, const FootMeshes& meshes, const Vec3& viewPosition, const Quat& viewRotation,
                    std::vector<MeshDrawItem>& items, std::vector<GlowSprite>& glows) const {
  const FootCombat& f = session.foot();
  // ---- Enemies ----
  for (const FootEnemy& e : f.enemies()) {
    const FootEnemyDef& d = f.enemyDef(e);
    const auto found = meshes.enemies.find(d.visual);
    if (found == meshes.enemies.end()) continue;
    const FootEnemyMeshes& m = found->second;
    const LegRig rig = rigFor(d.visual);
    const float k = d.visualScale;
    const bool dead = e.state == EnemyState::Dead;
    const float fall = dead ? std::clamp(e.deadTime / 0.35f, 0.0f, 1.0f) : 0.0f;
    float pitch = 0;  // body language per state
    if (e.state == EnemyState::WindingUp) pitch = 0.38f;
    if (e.state == EnemyState::Aiming) pitch = 0.22f;
    if (e.state == EnemyState::ChargeWindup) pitch = -0.18f;  // head down
    if (e.state == EnemyState::Charging) pitch = -0.1f;
    float sway = 0;
    if (e.state == EnemyState::Recovering) sway = std::sin(e.timer * 9.0f) * 0.12f;  // dazed
    const float bob = dead ? 0.0f : std::sin(e.stride * 2.0f) * 0.012f * k;
    const Vec3 at = e.body.feet + Vec3{0, bob - fall * rig.hipY * 0.6f * k, 0};
    const Mat4 base = Mat4::translation(at - viewPosition) *
                      Mat4::rotation(aboutY(-e.body.yaw) * aboutX(pitch + fall * 0.08f) * aboutZ(sway)) *
                      Mat4::scale({k, k, k});
    items.push_back({dead ? m.dead : m.body, base});
    for (int row = 0; row < rig.rows; ++row) {
      for (int side = -1; side <= 1; side += 2) {
        const float phase = e.stride + float(row) * (6.2831853f / float(rig.rows + 1)) + (side > 0 ? kPi : 0.0f);
        const float swing = dead ? 0.0f : std::sin(phase) * rig.swing;
        const float lift = dead ? fall * 1.1f : std::max(0.0f, std::cos(phase)) * rig.lift;
        const float centre = float(rig.rows - 1) * 0.5f;
        const float splay = (float(row) - centre) * rig.splay;
        const Vec3 hip{float(side) * rig.hipX, rig.hipY, (float(row) - centre) * rig.rowSpacing};
        const Quat turn = side > 0 ? aboutY(splay + swing) : aboutY(kPi - splay - swing);
        items.push_back({m.leg, base * Mat4::translation(hip) * Mat4::rotation(turn * aboutZ(lift))});
      }
    }
  }

  // ---- Globs in flight ----
  for (const FootProjectile& pr : f.projectiles()) {
    glows.push_back({Vec4(pr.position - viewPosition, pr.radius * 1.6f), Vec4(kGlobColor, 0)});
    glows.push_back({Vec4(pr.position - viewPosition, pr.radius * 0.7f), Vec4(Vec3{2.0f, 2.6f, 1.2f}, 0)});
  }

  // ---- Loot still lying around, and what the dead dropped ----
  auto lootMesh = [&](const std::string& name) {
    const auto it = meshes.loot.find(name);
    return it == meshes.loot.end() ? 0u : it->second;
  };
  auto tintOf = [](const std::string& mesh) {
    if (mesh == "medical") return Vec3{0.25f, 1.6f, 0.65f};
    if (mesh == "cache") return Vec3{0.35f, 0.8f, 1.8f};
    return Vec3{1.6f, 0.9f, 0.2f};
  };
  if (const InteriorDef* def = session.activeInterior()) {
    for (int i = 0; i < int(def->markers.size()); ++i) {
      const InteriorMarker& m = def->markers[size_t(i)];
      if (m.type != "loot" || session.markerCleared(i)) continue;
      const LootTable* table = f.content().lootTable(m.kind.empty() ? "salvage" : m.kind);
      const std::string mesh = table ? table->mesh : "salvage";
      if (const std::uint32_t h = lootMesh(mesh))
        items.push_back({h, Mat4::translation(m.position - viewPosition) * Mat4::rotation(aboutY(radians(m.yawDeg)))});
      glows.push_back({Vec4(m.position + Vec3{0, 0.4f, 0} - viewPosition, 0.09f), Vec4(tintOf(mesh), 0)});
    }
  }
  const double t = session.world().simulationTime();
  for (const auto& d : session.footDrops()) {
    const float spin = float(t) * 1.2f + float(d.key.size());
    if (const std::uint32_t h = lootMesh("ammo"))
      items.push_back({h, Mat4::translation(d.position - viewPosition) * Mat4::rotation(aboutY(spin))});
    const float pulse = 0.7f + 0.3f * std::sin(float(t) * 5.0f + float(d.key.size()));
    glows.push_back({Vec4(d.position + Vec3{0, 0.35f, 0} - viewPosition, 0.08f), Vec4(Vec3{1.6f, 0.9f, 0.2f} * pulse, 0)});
  }

  // ---- The gun in your hands ----
  if (session.footArmed() && !session.footPlayers().empty()) {
    const FootPlayer& me = session.footPlayers().front();
    const FootWeaponDef& w = f.weapon(me);
    const std::uint32_t gun = me.weapon >= 0 && me.weapon < int(meshes.guns.size()) ? meshes.guns[size_t(me.weapon)] : 0;
    const float dip = std::max(std::sin(reloadTurn_ * kPi), switchDip_);
    Vec3 offset = kGunRest;
    offset.x += std::cos(bobPhase_) * 0.006f * bobAmount_;
    offset.y += std::fabs(std::sin(bobPhase_)) * 0.009f * bobAmount_ - dip * 0.09f - switchDip_ * 0.12f;
    offset.z += kick_ * 0.045f + wallPull_ * 0.34f;
    const Quat tilt = aboutX(kick_ * 0.1f - dip * 0.75f - wallPull_ * 1.3f) * aboutZ(dip * 0.35f);
    if (gun) items.push_back({gun, Mat4::translation(viewRotation.rotate(offset)) * Mat4::rotation(viewRotation * tilt), false});
    const Vec3 muzzle = viewRotation.rotate(offset + w.muzzle);
    if (flash_ > 0) glows.push_back({Vec4(muzzle, 0.1f * (0.6f + 0.4f * flash_)), Vec4(flashColor_ * flash_, 0)});
    if (me.beamActive) {
      // The arc: a chain of glows from the muzzle to where it bites, kinked by noise.
      const Vec3 end = me.beamEnd - viewPosition;
      const Vec3 line = end - muzzle;
      const float len = length(line);
      const int n = std::clamp(int(len / 0.08f), 6, 120);
      const Vec3 side = normalize(cross(line, Vec3{0, 1, 0}) + Vec3{1e-4f, 0, 0});
      const Vec3 up = normalize(cross(side, line));
      for (int i = 0; i <= n; ++i) {
        const float s = float(i) / float(n);
        const float env = std::sin(s * kPi);
        const float wob = std::sin(s * 23.0f + beamFlicker_ * 1.7f) * 0.045f + std::sin(s * 61.0f - beamFlicker_ * 2.3f) * 0.02f;
        const Vec3 p = muzzle + line * s + (side * wob + up * wob * 0.6f) * env;
        glows.push_back({Vec4(p, 0.018f + 0.01f * env), Vec4(w.tracerColor * (0.7f + 0.3f * std::sin(beamFlicker_ + s * 9.0f)), 0)});
      }
      glows.push_back({Vec4(end, 0.12f), Vec4(w.tracerColor * 1.4f, 0)});
    }
  }
}

void FootView::addLights(const GameSession& session, const Quat& viewRotation, const Vec3& viewPosition,
                         std::vector<PointLight>& lights) const {
  // A weak lamp on the suit: derelicts are dark, and you need to see what the red eyes belong to.
  if (suitLamp_ && session.footArmed())
    lights.insert(lights.begin(), PointLight{viewRotation.rotate({-0.18f, 0.16f, 0.1f}), 16.0f, Vec3{1.5f, 1.45f, 1.3f}});
  // Globs glow sickly green on the walls they pass.
  for (const FootProjectile& pr : session.foot().projectiles())
    lights.push_back(PointLight{pr.position - viewPosition, 4.0f, kGlobColor * 1.6f});
  if (!session.footPlayers().empty() && session.footPlayers().front().beamActive) {
    const FootPlayer& me = session.footPlayers().front();
    lights.insert(lights.begin(), PointLight{me.beamEnd - viewPosition, 6.0f,
                                             session.foot().weapon(me).tracerColor * (0.9f + 0.3f * std::sin(beamFlicker_))});
  }
  if (flash_ <= 0) return;
  // Ahead of the barrel, so the gun does not throw a huge shadow of itself. First in the list, so a
  // full scene loses its last lamp for an instant rather than the flash.
  const Vec3 at = viewRotation.rotate(muzzleOffset(session) + Vec3{0, 0, -0.3f});
  lights.insert(lights.begin(), PointLight{at, 10.0f, flashColor_ * (0.64f * flash_)});
}

void FootView::buildSprites(const Vec3& viewPosition, std::vector<ParticleSprite>& out) const {
  particles_.buildSprites(viewPosition, {}, out);
}

}  // namespace fh
