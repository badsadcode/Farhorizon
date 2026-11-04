#include "game/session/world_builder.h"

#include <algorithm>

#include "engine/render/starfield.h"  // Rng
#include "game/world/asteroid_gen.h"

namespace fh {

Quat eulerDegToQuat(const Vec3& deg) {
  return normalize(Quat::fromAxisAngle({0, 0, 1}, radians(deg.z)) * Quat::fromAxisAngle({0, 1, 0}, radians(deg.y)) *
                   Quat::fromAxisAngle({1, 0, 0}, radians(deg.x)));
}

std::unique_ptr<SpaceWorld> buildSpaceWorld(const SceneDef& scene, const ShipDef& ship, const WorldVisuals& visuals,
                                            std::optional<SystemPosition> startOverride) {
  auto world = std::make_unique<SpaceWorld>();
  const SystemPosition start = startOverride.value_or(scene.player.position);
  world->setOrigin(start, scene.rebaseDistance);

  PlayerShip& player = world->player();
  player.def = ship;
  const VisualInfo sv = visuals.ship ? visuals.ship(ship) : VisualInfo{};
  player.visual = sv.handle;
  player.meshFix = sv.meshFix;
  player.collisionRadius = ship.collisionRadius > 0 ? ship.collisionRadius : sv.boundsRadius;
  // Sockets: mesh nodes win; ship JSON provides fallbacks (both end up in ship space).
  const Quat fixRot = quatFromMatrix(sv.meshFix);
  for (const auto& s : sv.sockets)
    player.sockets[s.name] = {sv.meshFix.transformPoint(s.position), normalize(fixRot * s.rotation)};
  for (const auto& [name, s] : ship.sockets)
    if (!player.sockets.count(name)) player.sockets[name] = {s.position, eulerDegToQuat(s.rotationDeg)};
  player.state = FlightState{};
  player.state.orientation = eulerDegToQuat(scene.player.rotationDeg);
  player.state.boostEnergy = ship.flight.boostEnergyCapacity;
  player.previous = player.state;

  for (const PropDef& pd : scene.props) {
    Prop p;
    p.def = pd;
    p.system = pd.position;
    p.rotation = eulerDegToQuat(pd.rotationDeg);
    const VisualInfo v = visuals.prop ? visuals.prop(pd) : VisualInfo{};
    p.visual = v.handle;
    p.meshFix = v.meshFix;
    float radius = pd.collisionRadius > 0 ? pd.collisionRadius : v.boundsRadius;
    for (const auto& c : pd.colliders) {
      p.colliders.push_back({c.center, eulerDegToQuat(c.rotationDeg), c.halfExtents});
      radius = std::max(radius, length(c.center) + length(c.halfExtents));  // broad-phase sphere
    }
    p.collisionRadius = radius;
    world->addProp(std::move(p));
  }

  for (const AsteroidFieldDef& f : scene.asteroidFields) {
    AsteroidFieldRuntime rt;
    rt.def = f;
    rt.variants = visuals.asteroidVariants ? visuals.asteroidVariants(f) : std::vector<VisualHandle>(f.variants, 0);
    if (rt.variants.size() < f.variants) rt.variants.resize(f.variants, 0);
    const std::uint32_t fieldIndex = world->addAsteroidField(rt);
    AsteroidFieldParams ap;
    ap.seed = f.seed;
    ap.count = f.count;
    ap.radius = f.radius;
    ap.thickness = f.thickness;
    ap.clusters = f.clusters;
    ap.minScale = f.minScale;
    ap.maxScale = f.maxScale;
    ap.variants = f.variants;
    const auto instances = generateAsteroidField(ap);
    // Composition is derived from its own RNG stream so placement stays unchanged when tuned.
    Rng comp(0xC0FFEEull ^ (std::uint64_t(f.seed) << 20));
    float totalWeight = 0;
    for (const auto& rdef : f.resources) totalWeight += std::max(0.0f, rdef.weight);
    const auto& variants = world->fields()[fieldIndex].variants;
    for (std::uint32_t i = 0; i < instances.size(); ++i) {
      const AsteroidInstance& inst = instances[i];
      Asteroid a;
      a.system = {f.center.metres + DVec3(inst.offset)};
      a.rotation = inst.rotation;
      a.spinAxis = inst.spinAxis;
      a.spinRate = inst.spinRate;
      a.radius = inst.radius;
      a.visual = variants[inst.variant];
      a.field = fieldIndex;
      a.index = i;
      if (totalWeight > 0) {
        float pick = comp.uniform() * totalWeight;
        for (const auto& rdef : f.resources) {
          pick -= std::max(0.0f, rdef.weight);
          if (pick <= 0) {
            a.resource = rdef.commodity;
            break;
          }
        }
        if (a.resource.empty()) a.resource = f.resources.back().commodity;
        a.initialYield = std::max(1, int(inst.radius * f.yieldPerMetre * comp.range(0.6f, 1.3f)));
        a.yield = a.initialYield;
        a.hardness = comp.range(f.hardnessMin, f.hardnessMax);
      }
      world->addAsteroid(a);
    }
  }
  return world;
}

}  // namespace fh
