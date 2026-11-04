// Particle system, procedural atlas, and the flight behaviour behind the full-stop hold.
#include <algorithm>
#include <cmath>
#include <vector>

#include "engine/render/particle_atlas.h"
#include "game/fx/particles.h"
#include "game/ships/flight_model.h"
#include "test_framework.h"

using namespace fh;

namespace {
constexpr float kFrame = 1.0f / 60.0f;

EngineFx thrusting(const Vec3& position, float demand = 1.0f) {
  EngineFx fx;
  fx.position = position;
  fx.demand = demand;
  fx.rate = 60;
  return fx;
}
// Runs an emitter moving along -Z for `seconds`, keeping the system ticking like the game does.
void fly(ParticleSystem& ps, TrailEmitter& em, float speed, float demand, float seconds) {
  Vec3 at = em.last;
  for (float t = 0; t < seconds; t += kFrame) {
    at.z -= speed * kFrame;
    EngineFx fx = thrusting(at, demand);
    fx.shipVelocity = {0, 0, -speed};
    ps.emitEngine(em, fx, kFrame);
    ps.update(kFrame);
  }
}
}  // namespace

// ---- Particle system --------------------------------------------------------------------------

TEST(fx_particles_age_out_and_are_removed) {
  ParticleSystem ps;
  Particle p;
  p.life = 0.5f;
  ps.add(p);
  ps.update(0.25f);
  CHECK(ps.size() == 1);
  ps.update(0.3f);
  CHECK(ps.size() == 0);
}

TEST(fx_particle_count_is_capped) {
  ParticleSystem ps;
  Particle p;
  p.life = 100.0f;
  for (std::size_t i = 0; i < ParticleSystem::kMaxParticles + 500; ++i) ps.add(p);
  CHECK(ps.size() == ParticleSystem::kMaxParticles);
}

TEST(fx_shift_moves_world_particles_but_not_attached_ones) {
  ParticleSystem ps;
  Particle world, attached;
  world.position = {1, 2, 3};
  attached.position = {1, 2, 3};
  attached.attached = true;
  ps.add(world);
  ps.add(attached);
  ps.shift({10, 0, 0});
  CHECK_NEAR(ps.particles()[0].position.x, 11, 1e-5);
  CHECK_NEAR(ps.particles()[1].position.x, 1, 1e-5);
}

TEST(fx_ribbon_quads_share_edges_so_there_are_no_gaps) {
  ParticleSystem ps;
  TrailEmitter em;
  fly(ps, em, 400.0f, 1.0f, 0.5f);
  CHECK(em.points.size() > 10);
  std::vector<RibbonVertex> v;
  ParticleSystem::buildRibbon(em, {30, 10, 0}, v);
  CHECK(v.size() >= 60);
  CHECK(v.size() % 6 == 0);
  // Quad layout: a b d, a d c. The far edge (c, d) of each quad is the near edge (a, b) of the next.
  for (size_t q = 0; q + 6 < v.size(); q += 6) {
    CHECK_VEC_NEAR(v[q + 5].posU, v[q + 6].posU, 1e-4);  // c == next a
    CHECK_VEC_NEAR(v[q + 2].posU, v[q + 7].posU, 1e-4);  // d == next b
  }
}

TEST(fx_ribbon_reaches_the_live_nozzle_and_the_gas_stays_close_together) {
  ParticleSystem ps;
  TrailEmitter em;
  fly(ps, em, 400.0f, 1.0f, 0.5f);
  for (size_t i = 1; i < em.points.size(); ++i) CHECK(length(em.points[i].position - em.points[i - 1].position) < 12.0f);
  const Vec3 camera{30, 10, 0};
  std::vector<RibbonVertex> v;
  ParticleSystem::buildRibbon(em, camera, v);
  const Vec3 head = (v.back().posU.xyz() + v[v.size() - 4].posU.xyz()) * 0.5f + camera;
  CHECK(length(head - em.last) < 0.5f);
}

TEST(fx_ribbon_fades_away_when_the_engine_stops) {
  ParticleSystem ps;
  TrailEmitter em;
  fly(ps, em, 100.0f, 1.0f, 0.5f);
  CHECK(!em.points.empty());
  for (int i = 0; i < 300; ++i) ps.tickTrail(em, kFrame);
  CHECK(em.points.empty());
  std::vector<RibbonVertex> v;
  ParticleSystem::buildRibbon(em, {}, v);
  CHECK(v.empty());
}

TEST(fx_ribbon_never_twists_or_breaks_when_seen_end_on) {
  ParticleSystem ps;
  TrailEmitter em;
  fly(ps, em, 200.0f, 1.0f, 1.0f);
  std::vector<RibbonVertex> v;
  ParticleSystem::buildRibbon(em, {0, 0, 50.0f}, v);  // the camera sits right on the flight path
  for (const RibbonVertex& r : v) {
    CHECK(std::isfinite(r.posU.x) && std::isfinite(r.posU.y) && std::isfinite(r.posU.z));
    CHECK(std::isfinite(r.color.x));
  }
}

TEST(fx_rebase_moves_the_whole_trail) {
  ParticleSystem ps;
  TrailEmitter em;
  fly(ps, em, 100.0f, 1.0f, 0.3f);
  const Vec3 before = em.points.front().position;
  em.shift({1000, 0, 0});
  CHECK_NEAR(em.points.front().position.x, before.x + 1000, 1e-2);
  CHECK_NEAR(em.last.x, 1000, 1e-2);
}

TEST(fx_plume_is_continuous_and_vanishes_when_the_tunnel_closes) {
  std::vector<RibbonVertex> v;
  ParticleSystem::buildPlume({0, 0, 0}, {0, 0, -1}, {1, 1, 1}, 0.0f, 1.0f, {5, 5, 5}, v);
  CHECK(v.empty());
  ParticleSystem::buildPlume({0, 0, 0}, {0, 0, -1}, {1, 1, 1}, 1.0f, 1.0f, {5, 5, 5}, v);
  CHECK(v.size() > 100);
  for (size_t q = 0; q + 6 < v.size(); q += 6) CHECK_VEC_NEAR(v[q + 5].posU, v[q + 6].posU, 1e-4);
}

TEST(fx_transit_streaks_have_a_real_variety_of_widths) {
  const Vec3 nozzles[1] = {{0, 0, 3}};
  TravelFx fx;
  fx.nozzles = nozzles;
  fx.nozzleCount = 1;
  fx.phase = TravelFxPhase::Transit;
  fx.speedFraction = 1.0f;
  ParticleSystem ps;
  ps.emitTravel(fx, 0.5f);
  float lo = 100, hi = 0;
  for (const Particle& p : ps.particles()) {
    lo = std::min(lo, p.sizeStart);
    hi = std::max(hi, p.sizeStart);
  }
  CHECK(lo < 0.4f);
  CHECK(hi > 1.0f);
}

// ---- Speed dust ---------------------------------------------------------------------------------

TEST(dust_is_invisible_when_stopped_and_hands_over_to_the_tunnel) {
  CHECK(SpeedDust::visibility(0.0f) == 0.0f);
  CHECK(SpeedDust::visibility(170.0f) > 0.95f);
  CHECK(SpeedDust::visibility(50000.0f) == 0.0f);
  SpeedDust dust;
  std::vector<ParticleSprite> out;
  dust.buildSprites({}, {0, 0, 0}, out);
  CHECK(out.empty());
  dust.buildSprites({}, {0, 0, -170.0f}, out);
  CHECK(out.size() > 100);
}

TEST(dust_stays_around_the_camera_however_far_it_flies) {
  SpeedDust dust;
  Vec3 camera{};
  for (int i = 0; i < 200; ++i) {
    camera.z -= 700.0f;
    camera.x += 90.0f;
    dust.wrap(camera);
  }
  for (const Vec3& p : dust.positions()) {
    CHECK(std::fabs(p.x - camera.x) <= SpeedDust::kHalfBox + 0.01f);
    CHECK(std::fabs(p.y - camera.y) <= SpeedDust::kHalfBox + 0.01f);
    CHECK(std::fabs(p.z - camera.z) <= SpeedDust::kHalfBox + 0.01f);
  }
}

TEST(dust_stays_put_in_the_world_between_wraps) {
  SpeedDust dust;
  const std::vector<Vec3> before = dust.positions();
  dust.wrap({1, 2, 3});  // a small move wraps almost nothing, and no mote drifts on its own
  int moved = 0;
  for (size_t i = 0; i < before.size(); ++i) moved += length(dust.positions()[i] - before[i]) > 1e-3f ? 1 : 0;
  CHECK(moved < 30);
}

TEST(dust_streaks_get_longer_with_speed_and_are_not_all_one_width) {
  SpeedDust dust;
  std::vector<ParticleSprite> slow, fast;
  dust.buildSprites({}, {0, 0, -60.0f}, slow);
  dust.buildSprites({}, {0, 0, -300.0f}, fast);
  CHECK(fast[0].axisLength.w > slow[0].axisLength.w * 3);
  float lo = 100, hi = 0;
  for (const ParticleSprite& p : fast) {
    lo = std::min(lo, p.positionSize.w);
    hi = std::max(hi, p.positionSize.w);
  }
  CHECK(hi > lo * 4);
}

TEST(fx_more_thrust_means_more_particles) {
  ParticleSystem idle, full;
  TrailEmitter a, b;
  fly(idle, a, 10.0f, 0.0f, 1.0f);
  fly(full, b, 10.0f, 1.0f, 1.0f);
  CHECK(full.size() > idle.size() * 4);
}

TEST(fx_teleport_restarts_the_trail_without_a_streak) {
  ParticleSystem ps;
  TrailEmitter em;
  ps.emitEngine(em, thrusting({0, 0, 0}), kFrame);
  const std::size_t before = ps.size();
  ps.emitEngine(em, thrusting({0, 0, -50000.0f}), kFrame);  // towed, loaded, or a rebase
  CHECK(ps.size() == before);
  for (const Particle& p : ps.particles()) CHECK(std::fabs(p.position.z) < 10.0f);
}

TEST(fx_emission_stays_bounded_over_a_long_flight) {
  ParticleSystem ps;
  TrailEmitter em;
  fly(ps, em, 300.0f, 1.0f, 30.0f);
  CHECK(ps.size() < ParticleSystem::kMaxParticles / 2);  // lifetimes balance the rate
}

TEST(fx_travel_phases_emit_the_right_kinds) {
  const Vec3 nozzles[2] = {{-2, 0, 3}, {2, 0, 3}};
  TravelFx fx;
  fx.nozzles = nozzles;
  fx.nozzleCount = 2;
  ParticleSystem ps;
  ps.emitTravel(fx, 1.0f);  // idle
  CHECK(ps.size() == 0);
  fx.phase = TravelFxPhase::Spooling;
  fx.spool = 0.8f;
  ps.emitTravel(fx, 0.2f);
  CHECK(ps.size() > 0);
  for (const Particle& p : ps.particles()) CHECK(p.attached);
  ps.clear();
  fx.phase = TravelFxPhase::Transit;
  fx.speedFraction = 1.0f;
  ps.emitTravel(fx, 0.2f);
  CHECK(ps.size() > 40);
  for (const Particle& p : ps.particles()) {
    CHECK(p.attached);
    CHECK(p.stretch > 0);
    CHECK(p.velocity.z > 0);  // rushing past from ahead (forward is -Z)
  }
}

TEST(fx_sprites_follow_the_ship_when_attached) {
  ParticleSystem ps;
  Particle world, attached;
  world.position = {100, 0, 0};
  attached.position = {5, 0, 0};
  attached.attached = true;
  ps.add(world);
  ps.add(attached);
  ps.update(0.5f);  // past the fade-in
  std::vector<ParticleSprite> out;
  ps.buildSprites({1000, 0, 0}, {1000, 0, 0}, out);
  CHECK(out.size() == 2);
  CHECK_NEAR(out[0].positionSize.x, -900, 1e-3);
  CHECK_NEAR(out[1].positionSize.x, 5, 1e-3);
}

// ---- Atlas ------------------------------------------------------------------------------------

TEST(fx_atlas_is_deterministic_with_every_frame_filled) {
  const ImageRGBA8 a = generateParticleAtlas(64), b = generateParticleAtlas(64);
  CHECK(a.valid());
  CHECK(a.pixels == b.pixels);
  for (int cell = 0; cell < 4; ++cell) {
    const int ox = (cell % 2) * 64, oy = (cell / 2) * 64;
    int peak = 0;
    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 64; ++x)
        peak = std::max(peak, int(a.pixels[(size_t(oy + y) * 128 + size_t(ox + x)) * 4 + 3]));
    CHECK(peak > 120);
  }
}

TEST(fx_atlas_cells_fade_to_nothing_at_their_borders) {
  const ImageRGBA8 a = generateParticleAtlas(64);
  auto alpha = [&](int x, int y) { return int(a.pixels[(size_t(y) * 128 + size_t(x)) * 4 + 3]); };
  for (int cell = 0; cell < 4; ++cell) {
    const int ox = (cell % 2) * 64, oy = (cell / 2) * 64;
    for (int i = 0; i < 64; ++i) {
      CHECK(alpha(ox + i, oy + 63) <= 4 || cell == 2);  // the streak's head sits near one end
      CHECK(alpha(ox, oy + i) <= 4);
      CHECK(alpha(ox + 63, oy + i) <= 4);
      CHECK(alpha(ox + i, oy) <= 4);
    }
  }
}

// ---- Full stop --------------------------------------------------------------------------------
// What ShipController sends while holding: no thrust, no rotation, assist on, full authority.

TEST(full_stop_input_stops_a_tumbling_drifting_ship_in_any_direction) {
  FlightParams p;
  FlightState s;
  s.velocity = {35, -12, -90};  // includes sideways drift, as after flying with assist off
  s.angularVelocity = {0.9f, -0.6f, 0.4f};
  FlightInput hold;
  hold.assist = true;
  for (int i = 0; i < 60 * 40; ++i) stepFlight(s, hold, p, kFrame);
  CHECK(length(s.velocity) < 1e-3f);
  CHECK(length(s.angularVelocity) < 1e-3f);
  const Vec3 parked = s.position;
  for (int i = 0; i < 600; ++i) stepFlight(s, hold, p, kFrame);
  CHECK(length(s.position - parked) < 1e-3f);  // and it stays put
}
