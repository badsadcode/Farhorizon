#pragma once
// Fighting on foot: guns, enemies and what happens between them. Takes a list of players, so co-op can
// feed it up to four without changing how it works. GPU-free, deterministic and unit-tested.
//
// Weapons and enemies are content (game/foot/foot_defs.h). Three fire modes: auto (one hitscan shot per
// interval), pellets (a cone of hitscan pellets) and beam (continuous, drains its magazine per second).
// Three behaviours: melee (chase and lunge), ranged (keep distance, lob slow projectiles you can dodge)
// and charger (wind up, charge in a straight line, then stand stunned and vulnerable). Enemies follow
// paths from the map's NavGrid when they cannot walk straight at their goal.
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "engine/math/math.h"
#include "game/foot/foot_defs.h"
#include "game/physics/collision.h"
#include "game/player/character_controller.h"

namespace fh {

class NavGrid;

struct FootPlayer {
  Vec3 feet{};
  Vec3 eye{};
  Vec3 look{0, 0, -1};
  float health = 100;
  // Loadout, indexed like FootContent::weapons / ::ammo (FootCombat::outfit fills them).
  std::vector<char> owned;
  std::vector<int> magazines;
  std::vector<int> ammo;
  int weapon = 0;             // index of the weapon in hand
  float cooldown = 0;
  float reloadLeft = 0;       // > 0 while reloading
  float switchLeft = 0;       // > 0 while changing weapons
  float beamCarry = 0;        // fractional magazine units the beam has used
  float beamTick = 0;
  bool beamActive = false;    // the beam is firing this step
  Vec3 beamEnd{};
  // Input, set every step by whoever drives this player.
  bool trigger = false;
  bool reloadRequest = false;
  int switchRequest = -1;     // weapon index to change to
  bool noisy = false;         // running: enemies nearby hear it
  // Out: a push the body should take (charges); the driver applies and clears it.
  Vec3 knock{};

  bool alive() const { return health > 0; }
  bool reloading() const { return reloadLeft > 0; }
  bool switching() const { return switchLeft > 0; }
};

enum class EnemyState { Asleep, Chasing, WindingUp, Aiming, ChargeWindup, Charging, Recovering, Dead };
using CrawlerState = EnemyState;  // the first enemy type gave the states their old name
const char* enemyStateName(EnemyState s);

struct FootEnemy {
  int def = 0;                // index into FootContent::enemies
  int markerIndex = -1;       // which map marker it came from
  Vec3 home{};
  float homeYaw = 0;
  CharacterState body;
  EnemyState state = EnemyState::Asleep;
  float health = 60;
  float timer = 0;            // wind-up, aim, charge wind-up, recovery
  float cooldown = 0;         // until the next attack of the current kind may start
  float sinceSeen = 0;
  Vec3 lastSeen{};
  float sightCheck = 0;
  bool seesTarget = false;
  float stride = 0;           // walk cycle, for drawing its legs
  float deadTime = 0;         // for drawing it collapse
  float stuckTime = 0;
  float sidestepTime = 0;
  float sidestepDir = 1;
  // Charging.
  Vec3 chargeDir{};
  float chargeLeft = 0;       // metres still to run
  // Pathfinding.
  std::vector<Vec3> path;
  size_t pathIndex = 0;
  Vec3 pathGoal{};
  float repath = 0;
  float directCheck = 0;
  bool direct = true;
};

struct FootProjectile {
  Vec3 position{}, velocity{};
  float damage = 10, radius = 0.2f, gravity = 4, life = 4;
  int enemy = -1;             // who threw it
};

struct FootEvent {
  enum class Type {
    Shot, Dry, ReloadStart, ReloadDone, HitWall, HitEnemy,
    EnemyWoke, EnemyLunged, EnemyStruck, EnemyDied, PlayerHurt, PlayerDown, Pickup,
    WeaponSwitched, BeamStart, BeamStop, EnemyFired, ProjectileHit, EnemyCharged, EnemyStunned, Unlocked
  };
  Type type = Type::Shot;
  Vec3 position{};            // what it happened at
  Vec3 from{};                // Shot: the eye of whoever fired
  int player = -1;
  int enemy = -1;
  int weapon = -1;            // Shot/Dry/Reload/Switch/Beam: which weapon
};

struct FootSpawn {
  int markerIndex = -1;
  Vec3 position{};
  float yawDeg = 0;
  std::string kind;           // enemy id or bare kind ("spitter"); empty = the first enemy type
};

class FootCombat {
 public:
  FootCombat();
  // Content outlives the combat; null = built-in defaults (rivet driver and crawler only).
  void configure(const FootContent* content);
  const FootContent& content() const { return *content_; }

  void begin(const std::vector<FootSpawn>& spawns);
  void clear() {
    enemies_.clear();
    projectiles_.clear();
    events_.clear();
  }
  void update(float dt, std::vector<FootPlayer>& players, const std::vector<Aabb>& colliders,
              const TriangleCollider* mesh, const NavGrid* nav = nullptr);
  // Everyone goes back to where they started, at full health. The dead stay dead.
  void returnHome();

  // ---- Loadout ----
  void outfit(FootPlayer& p) const;  // the starting loadout, full health
  const FootWeaponDef& weapon(const FootPlayer& p) const;
  int magazine(const FootPlayer& p) const;
  int reserve(const FootPlayer& p) const;  // ammo left for the weapon in hand
  int addAmmo(FootPlayer& p, const std::string& ammoId, int amount) const;  // returns what fitted
  bool unlock(FootPlayer& p, const std::string& weaponId) const;              // false if already owned
  std::string apply(FootPlayer& p, const LootRoll& roll, int* creditsOut) const;  // returns a short summary

  // ---- State ----
  const std::vector<FootEnemy>& enemies() const { return enemies_; }
  std::vector<FootEnemy>& enemies() { return enemies_; }
  const FootEnemyDef& enemyDef(const FootEnemy& e) const { return content_->enemies[size_t(e.def)]; }
  const std::vector<FootProjectile>& projectiles() const { return projectiles_; }
  int aliveCount() const;
  std::vector<FootEvent> takeEvents() { return std::exchange(events_, {}); }

 private:
  void updatePlayer(int index, FootPlayer& p, float dt, const TriangleCollider* mesh);
  void fireHitscan(int index, FootPlayer& p, const FootWeaponDef& w, const Vec3& dir, const TriangleCollider* mesh,
                   bool tracer);
  void updateBeam(int index, FootPlayer& p, const FootWeaponDef& w, float dt, const TriangleCollider* mesh);
  void updateEnemy(int index, FootEnemy& e, float dt, std::vector<FootPlayer>& players,
                   const std::vector<Aabb>& colliders, const TriangleCollider* mesh, const NavGrid* nav);
  // Walks towards `goal` (along a path when needed). `away` walks backwards from it instead.
  void moveTowards(FootEnemy& e, const FootEnemyDef& d, const Vec3& goal, float dt, const std::vector<Aabb>& colliders,
                   const TriangleCollider* mesh, const NavGrid* nav, bool away = false);
  void stand(FootEnemy& e, float dt, const std::vector<Aabb>& colliders, const TriangleCollider* mesh);
  void meleeStrike(int index, FootEnemy& e, const FootEnemyDef& d, FootPlayer* target, int targetIndex, float distance);
  void updateProjectiles(float dt, std::vector<FootPlayer>& players, const TriangleCollider* mesh);
  void wake(int index, FootEnemy& e, const Vec3& towards);
  void hear(const Vec3& at, float radius);
  void hurtPlayer(FootPlayer& p, int playerIndex, int enemyIndex, float amount, const Vec3& at);
  void damageEnemy(int index, float amount, const Vec3& at, const Vec3& push);
  float random01();
  const CharacterParams& bodyParams(const FootEnemy& e) const { return bodies_[size_t(e.def)]; }

  const FootContent* content_ = nullptr;
  std::vector<CharacterParams> bodies_;  // per enemy type
  std::vector<FootEnemy> enemies_;
  std::vector<FootProjectile> projectiles_;
  std::vector<FootEvent> events_;
  std::uint64_t rng_ = 0x5EEDF007ull;
};

// Whether a straight line reaches its end without hitting the map.
bool lineOfSight(const Vec3& from, const Vec3& to, const TriangleCollider* mesh);

}  // namespace fh
