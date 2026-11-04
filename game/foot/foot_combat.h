#pragma once
// Fighting on foot: guns, enemies and what happens between them. Takes a list of players, so co-op can
// feed it up to four without changing how it works. GPU-free, deterministic and unit-tested.
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "engine/math/math.h"
#include "game/physics/collision.h"
#include "game/player/character_controller.h"

namespace fh {

// The only gun so far: a hull fitter's rivet driver, which was never meant for this.
struct RivetDriverParams {
  float damage = 18;
  float fireInterval = 0.24f;
  float reloadSeconds = 1.6f;
  int magazine = 12;
  int startReserve = 36;
  float range = 60;
  float noiseRadius = 22;  // sleeping enemies this close hear a shot
};

struct CrawlerParams {
  float health = 60;
  float speed = 3.7f;         // m/s: faster than walking, slower than running
  float hitRadius = 0.55f;
  float hitCentreHeight = 0.42f;
  float sightRange = 15;
  float closeSenseRange = 5;  // notices anyone this close, in any direction
  float attackRange = 1.5f;
  float attackReach = 2.1f;   // step back this far during the wind-up and it misses
  float windupSeconds = 0.38f;
  float attackCooldown = 1.0f;
  float attackDamage = 14;
  float forgetSeconds = 6;
};

struct FootPlayer {
  Vec3 feet{};
  Vec3 eye{};
  Vec3 look{0, 0, -1};
  float health = 100;
  int magazine = 12;
  int reserve = 36;
  float cooldown = 0;
  float reloadLeft = 0;       // > 0 while reloading
  bool trigger = false;
  bool reloadRequest = false;

  bool alive() const { return health > 0; }
  bool reloading() const { return reloadLeft > 0; }
};

enum class CrawlerState { Asleep, Chasing, WindingUp, Dead };

struct FootEnemy {
  int markerIndex = -1;       // which map marker it came from
  Vec3 home{};
  float homeYaw = 0;
  CharacterState body;
  CrawlerState state = CrawlerState::Asleep;
  float health = 60;
  float timer = 0;            // wind-up or cooldown
  float sinceSeen = 0;
  Vec3 lastSeen{};
  float sightCheck = 0;
  bool seesTarget = false;
  float stride = 0;           // walk cycle, for drawing its legs
  float deadTime = 0;         // for drawing it collapse
  float stuckTime = 0;
  float sidestepTime = 0;
  float sidestepDir = 1;
};

struct FootEvent {
  enum class Type {
    Shot, Dry, ReloadStart, ReloadDone, HitWall, HitEnemy,
    EnemyWoke, EnemyLunged, EnemyStruck, EnemyDied, PlayerHurt, PlayerDown, Pickup
  };
  Type type = Type::Shot;
  Vec3 position{};            // what it happened at
  Vec3 from{};                // Shot: the eye of whoever fired
  int player = -1;
  int enemy = -1;
};

struct FootSpawn {
  int markerIndex = -1;
  Vec3 position{};
  float yawDeg = 0;
};

class FootCombat {
 public:
  void begin(const std::vector<FootSpawn>& spawns);
  void clear() { enemies_.clear(); events_.clear(); }
  void update(float dt, std::vector<FootPlayer>& players, const std::vector<Aabb>& colliders,
              const TriangleCollider* mesh);
  // Everyone goes back to where they started, at full health. The dead stay dead.
  void returnHome();

  const std::vector<FootEnemy>& enemies() const { return enemies_; }
  std::vector<FootEnemy>& enemies() { return enemies_; }
  int aliveCount() const;
  std::vector<FootEvent> takeEvents() { return std::exchange(events_, {}); }
  const RivetDriverParams& gun() const { return gun_; }
  const CrawlerParams& crawler() const { return crawler_; }

 private:
  void updatePlayer(int index, FootPlayer& p, float dt, const TriangleCollider* mesh);
  void updateEnemy(int index, FootEnemy& e, float dt, std::vector<FootPlayer>& players,
                   const std::vector<Aabb>& colliders, const TriangleCollider* mesh);
  void wake(int index, FootEnemy& e, const Vec3& towards);
  void hear(const Vec3& at, float radius);
  void damageEnemy(int index, float amount, const Vec3& at);

  RivetDriverParams gun_;
  CrawlerParams crawler_;
  CharacterParams crawlerBody_;
  std::vector<FootEnemy> enemies_;
  std::vector<FootEvent> events_;
};

// Whether a straight line reaches its end without hitting the map.
bool lineOfSight(const Vec3& from, const Vec3& to, const TriangleCollider* mesh);

}  // namespace fh
