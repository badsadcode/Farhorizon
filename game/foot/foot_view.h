#pragma once
// What fighting on foot looks like: the gun in your hands, crawlers, loot, sparks and screen flashes.
// Only reads the simulation, so nothing here changes how a fight goes.
#include <cstdint>
#include <vector>

#include "engine/render/renderer.h"
#include "game/foot/foot_combat.h"
#include "game/fx/particles.h"

namespace fh {

class GameSession;

struct FootMeshes {
  std::uint32_t gun = 0, crawlerBody = 0, crawlerDead = 0, crawlerLeg = 0, lootSalvage = 0, lootMedical = 0;
};

class FootView {
 public:
  void reset();
  void update(float dt, const std::vector<FootEvent>& events, const GameSession& session);
  void draw(const GameSession& session, const FootMeshes& meshes, const Vec3& viewPosition, const Quat& viewRotation,
            std::vector<MeshDrawItem>& items, std::vector<GlowSprite>& glows) const;
  void addLights(const GameSession& session, const Quat& viewRotation, std::vector<PointLight>& lights) const;
  void buildSprites(const Vec3& viewPosition, std::vector<ParticleSprite>& out) const;

  float hurtFlash() const { return hurtFlash_; }
  float hitMark() const { return hitMark_; }
  float blackout() const { return blackout_; }
  void toggleSuitLamp() { suitLamp_ = !suitLamp_; }

 private:
  Vec3 muzzleOffset() const;
  void burst(const Vec3& at, const Vec3& away, const Vec3& color, int count, float speed, ParticleFrame frame);

  ParticleSystem particles_;
  std::uint32_t sparkSeed_ = 1;
  float kick_ = 0;
  float flash_ = 0;
  float bobPhase_ = 0;
  float bobAmount_ = 0;
  float reloadTurn_ = 0;  // 0..1 through a reload
  float wallPull_ = 0;
  float hurtFlash_ = 0;
  float hitMark_ = 0;
  float blackout_ = 0;
  bool suitLamp_ = true;
};

}  // namespace fh
