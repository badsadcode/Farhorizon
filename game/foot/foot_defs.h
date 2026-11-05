#pragma once
// What fighting on foot is made of, as content: weapons and their ammo, enemy types, loot tables.
// content/foot/{weapons,enemies,loot}.json (docs/data_schemas/foot.md). GPU-free.
// builtinFootContent() reproduces the original hard-coded rivet driver and crawler, so tests and
// tools can run without content files.
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "engine/assets/asset_manifest.h"
#include "engine/math/math.h"

namespace fh {

struct FootAmmoDef {
  std::string id;      // "ammo.rivets"
  std::string name;    // "RIVETS"
  int maxReserve = 200;
};

enum class FootFireMode { Auto, Pellets, Beam };

struct FootWeaponDef {
  std::string id;      // "weapon.rivet_driver"
  std::string name;    // "RIVET DRIVER"
  FootFireMode mode = FootFireMode::Auto;
  std::string ammo;    // FootAmmoDef id
  float damage = 18;          // per hit (Auto), per pellet (Pellets), per second (Beam)
  int pellets = 1;
  float spreadDeg = 0;        // cone for pellets
  float fireInterval = 0.24f; // Auto/Pellets
  float ammoPerSecond = 0;    // Beam: magazine units drained per second while firing
  float reloadSeconds = 1.6f;
  int magazine = 12;
  float range = 60;
  float noiseRadius = 22;     // sleeping enemies this close hear it
  float knockback = 0;        // m/s pushed into an enemy per hit (brutes ignore most of it)
  std::string mesh;           // first-person model (mesh asset id)
  Vec3 muzzle{0, 0.085f, -0.33f};  // on the gun model, metres
  Vec3 tracerColor{7.0f, 4.6f, 1.8f};
  std::string soundFire = "rivet_shot", soundReload = "rivet_reload", soundDry = "rivet_dry", soundHit = "rivet_hit";
};

enum class FootBehaviour { Melee, Ranged, Charger };

struct FootEnemyDef {
  std::string id;      // "enemy.crawler"
  std::string name;
  FootBehaviour behaviour = FootBehaviour::Melee;
  float health = 60;
  float speed = 3.7f;
  float bodyRadius = 0.38f, bodyHeight = 0.8f;
  float hitRadius = 0.55f, hitCentreHeight = 0.42f;
  float sightRange = 15, closeSenseRange = 5, hearRunRange = 7, forgetSeconds = 6;
  // Melee (all behaviours use it up close).
  float attackRange = 1.5f, attackReach = 2.1f, windupSeconds = 0.38f, attackCooldown = 1.0f, attackDamage = 14;
  // Ranged.
  float preferredRange = 9, retreatRange = 3.5f;
  float projectileSpeed = 11, projectileDamage = 12, projectileRadius = 0.18f, projectileGravity = 4.0f;
  float aimSeconds = 0.55f, fireCooldown = 1.8f;
  // Charger.
  float chargeMinRange = 3, chargeMaxRange = 11, chargeWindup = 0.8f, chargeSpeed = 9.5f, chargeDistance = 12;
  float chargeDamage = 32, chargeKnockback = 7, recoverSeconds = 1.4f, recoverDamageScale = 2.0f;
  float knockbackResistance = 0;   // 0 = takes weapon knockback fully, 1 = ignores it
  // Loot.
  std::string dropTable;           // empty = drops nothing
  float dropChance = 0;
  // Looks and sounds.
  std::string visual = "crawler";  // FootView mesh set: crawler, spitter, brute
  float visualScale = 1.0f;
  std::string soundWake = "crawler_wake", soundHit = "crawler_hit", soundDie = "crawler_die", soundAttack = "crawler_wake";
};

struct LootEntry {
  float weight = 1;
  int creditsMin = 0, creditsMax = 0;
  std::map<std::string, std::array<int, 2>> ammo;  // ammo id -> [min, max]
  float heal = 0;
  std::string weapon;  // unlocks this weapon (and fills its magazine)
};

struct LootTable {
  std::string id;      // "loot.salvage"; markers name it via fh_kind ("salvage" maps to "loot.salvage")
  std::string mesh = "salvage";  // what lies on the floor: salvage, medical, cache, ammo
  std::string prompt;            // "Take the salvage"
  std::vector<LootEntry> entries;
};

struct FootContent {
  std::vector<FootAmmoDef> ammo;
  std::vector<FootWeaponDef> weapons;
  std::vector<FootEnemyDef> enemies;
  std::vector<LootTable> loot;
  std::vector<std::string> startWeapons{"weapon.rivet_driver"};
  std::map<std::string, int> startAmmo{{"ammo.rivets", 36}};

  int ammoIndex(const std::string& id) const;
  int weaponIndex(const std::string& id) const;
  int enemyIndex(const std::string& id) const;   // also accepts the bare kind ("crawler")
  const LootTable* lootTable(const std::string& idOrKind) const;
};

FootContent builtinFootContent();
// Loads the three files named by manifest ids foot.weapons, foot.enemies, foot.loot. Cross-checks
// references (weapon -> ammo, enemy -> loot table, loot -> weapon/ammo).
std::optional<FootContent> loadFootContent(const AssetManifest& manifest, std::vector<std::string>* errors);

// What a loot roll gives (deterministic for a seed).
struct LootRoll {
  int credits = 0;
  std::map<std::string, int> ammo;
  float heal = 0;
  std::string weapon;
  bool empty() const { return credits == 0 && ammo.empty() && heal <= 0 && weapon.empty(); }
};
LootRoll rollLoot(const LootTable& table, std::uint64_t seed);
std::uint64_t lootSeed(const std::string& key);  // FNV-1a of "map#marker" style keys

}  // namespace fh
