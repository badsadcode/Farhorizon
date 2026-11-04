#pragma once
// Space combat (design doc 8): durability (shields then hull, recharge delay), weapon heat,
// projectiles (bolts and homing rockets), NPC ships flown by the same flight model as the player
// and steered by a readable finite state machine, destruction with explosions and salvage.
// Only ships near the player exist (spawned by encounters/traffic, despawned when far away).
// GPU-free, deterministic for a given seed, unit-tested.
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "game/combat/weapon_def.h"
#include "game/content/scene_def.h"
#include "game/content/ship_def.h"
#include "game/world/space_world.h"

namespace fh {

class Mining;

struct Durability {
  float maxHull = 100, hull = 100, maxShield = 60, shield = 60;
  float regen = 8, delay = 4;
  double lastHit = -100;
  void reset(float maxHullIn, float maxShieldIn, const CombatParams& p);
  // Applies damage (shields first). Returns hull damage actually taken.
  float apply(float damage, float shieldMul, float hullMul, double now);
  void tick(float dt, double now);
  bool dead() const { return hull <= 0.0f; }
  float hullFraction() const { return maxHull > 0 ? hull / maxHull : 0.0f; }
};

struct WeaponSlot {
  std::string mount;
  const WeaponDef* def = nullptr;
  float cooldown = 0;
};

struct Armament {
  std::vector<WeaponSlot> primary, secondary;
  float heat = 0, heatCapacity = 100, dissipation = 22;
  bool overheated = false;
  float damageScale = 1.0f;  // upgrades
  void setup(const CombatParams& p, const WeaponDb& db);
  void tick(float dt);
  float heatFraction() const { return heatCapacity > 0 ? heat / heatCapacity : 0.0f; }
};

enum class AiState { Patrol, Intercept, Attack, Evade, Retreat, Route, Flee };
const char* aiStateName(AiState s);

constexpr std::uint32_t kPlayerId = 0;

struct NpcShip {
  std::uint32_t id = 0;
  AssetID shipDef;
  FlightState state, previous;
  FlightInput input;
  Durability dur;
  Armament arm;
  AiState ai = AiState::Patrol;
  double stateTime = 0;
  Vec3 goal{};          // patrol point / waypoint (local frame)
  Vec3 evadeDir{1, 0, 0};
  int encounter = -1, traffic = -1, waypoint = 0;
  bool hostile = false;  // hostile to the player
  double lastHitByPlayer = -100;
  double secondaryReady = 0;
  double engagedAnnounced = -100;
};

struct Projectile {
  Vec3 pos{}, vel{};
  float life = 0;
  std::uint32_t owner = 0;               // kPlayerId or npc id
  const WeaponDef* def = nullptr;
  float damageScale = 1.0f;
  std::optional<std::uint32_t> homingTarget;  // npc id, or kPlayerId
};

struct Explosion {
  Vec3 pos{};
  double time = 0;
  float size = 20;
};

struct CombatEvent {
  enum class Type { ShipDestroyed, PlayerDestroyed, PlayerHit, NpcHit, ShotFired, Engaged, Escaped };
  Type type = Type::ShotFired;
  std::string faction;
  AssetID shipDef;
  std::string sound;
  Vec3 where{};
  bool byPlayer = false;
  float amount = 0;
};

struct ShipType {
  ShipDef def;
  VisualHandle visual = 0;
  Mat4 meshFix;
  float radius = 6;
};

class Combat {
 public:
  void configure(const WeaponDb* weapons, const SceneDef& scene, std::uint64_t seed);
  void registerShipType(const AssetID& id, ShipType type) { types_[id] = std::move(type); }
  const ShipType* shipType(const AssetID& id) const;
  void setupPlayer(const ShipDef& def);  // durability limits + armament (keeps current damage)
  Durability& player() { return player_; }
  const Durability& player() const { return player_; }
  Armament& playerArms() { return playerArms_; }
  const Armament& playerArms() const { return playerArms_; }

  // `playerActive`: the player ship is flying freely in space (not docked/landed/in transit).
  void fixedUpdate(SpaceWorld& world, Mining& mining, bool firePrimary, bool fireSecondary, bool playerActive,
                   float dt);
  void applyRebase(const Vec3& delta);
  void applyCollisionDamage(float impactSpeed, double now, const CombatParams& p);
  void despawnAll();

  const std::vector<NpcShip>& npcs() const { return npcs_; }
  NpcShip* npc(std::uint32_t id);
  const std::vector<Projectile>& projectiles() const { return projectiles_; }
  const std::vector<Explosion>& explosions() const { return explosions_; }
  std::vector<CombatEvent> takeEvents() { return std::exchange(events_, {}); }

  // Targeting helpers (TargetRef::Kind::Ship uses the npc id as index).
  bool targetNearestHostile(SpaceWorld& world) const;
  std::optional<TargetInfo> describe(const SpaceWorld& world, std::uint32_t npcId) const;
  // Aim point for hitting `npcId` with the player's primary weapon (lead pip).
  std::optional<Vec3> leadPoint(const SpaceWorld& world, std::uint32_t npcId) const;
  // Spawns a ship now (tests, debug). Returns its id.
  std::uint32_t spawn(SpaceWorld& world, const AssetID& shipDef, const Vec3& local, int encounter, int traffic);

 private:
  void updateSpawns(SpaceWorld& world, bool playerActive, float dt);
  void think(NpcShip& n, SpaceWorld& world, bool playerActive, float dt);
  void fire(std::uint32_t owner, Armament& arm, bool secondary, const FlightState& pose,
            const std::map<std::string, Transform>& sockets, float radius, std::optional<std::uint32_t> lock);
  void moveProjectiles(SpaceWorld& world, Mining& mining, float dt);
  void destroyNpc(size_t index, SpaceWorld& world, Mining& mining, bool byPlayer);
  float rand01();

  const WeaponDb* weapons_ = nullptr;
  std::vector<EncounterDef> encounters_;
  std::vector<TrafficDef> traffic_;
  std::vector<double> encounterTimers_, trafficTimers_;
  std::map<AssetID, ShipType> types_;
  std::map<std::string, std::map<std::string, Transform>> socketCache_;
  Durability player_;
  Armament playerArms_;
  std::vector<NpcShip> npcs_;
  std::vector<Projectile> projectiles_;
  std::vector<Explosion> explosions_;
  std::vector<CombatEvent> events_;
  std::uint32_t nextId_ = 1;
  std::uint64_t rng_ = 0x5EEDull;
  double time_ = 0;
};

}  // namespace fh
