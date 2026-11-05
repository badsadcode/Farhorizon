#pragma once
// What fighting on foot looks like: the gun in your hands, enemies, loot, globs, the arc beam, sparks and
// screen flashes. Only reads the simulation, so nothing here changes how a fight goes.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "engine/render/renderer.h"
#include "game/foot/foot_combat.h"
#include "game/fx/particles.h"

namespace fh {

class GameSession;

struct FootEnemyMeshes {
  std::uint32_t body = 0, dead = 0, leg = 0;
};

struct FootMeshes {
  std::vector<std::uint32_t> guns;                 // by weapon index (FootContent::weapons)
  std::map<std::string, FootEnemyMeshes> enemies;  // by visual: crawler, spitter, brute
  std::map<std::string, std::uint32_t> loot;       // by loot mesh: salvage, medical, cache, ammo
};

class FootView {
 public:
  void reset();
  void update(float dt, const std::vector<FootEvent>& events, const GameSession& session);
  void draw(const GameSession& session, const FootMeshes& meshes, const Vec3& viewPosition, const Quat& viewRotation,
            std::vector<MeshDrawItem>& items, std::vector<GlowSprite>& glows) const;
  void addLights(const GameSession& session, const Quat& viewRotation, const Vec3& viewPosition,
                 std::vector<PointLight>& lights) const;
  void buildSprites(const Vec3& viewPosition, std::vector<ParticleSprite>& out) const;

  float hurtFlash() const { return hurtFlash_; }
  float hitMark() const { return hitMark_; }
  float blackout() const { return blackout_; }
  void toggleSuitLamp() { suitLamp_ = !suitLamp_; }

 private:
  Vec3 muzzleOffset(const GameSession& session) const;
  void burst(const Vec3& at, const Vec3& away, const Vec3& color, int count, float speed, ParticleFrame frame);

  ParticleSystem particles_;
  std::uint32_t sparkSeed_ = 1;
  float kick_ = 0;
  float flash_ = 0;
  Vec3 flashColor_{7.0f, 4.6f, 1.9f};
  float bobPhase_ = 0;
  float bobAmount_ = 0;
  float reloadTurn_ = 0;  // 0..1 through a reload
  float switchDip_ = 0;   // 0..1 while changing weapons
  float wallPull_ = 0;
  float hurtFlash_ = 0;
  float hitMark_ = 0;
  float blackout_ = 0;
  float beamFlicker_ = 0;
  bool suitLamp_ = true;
};

}  // namespace fh
