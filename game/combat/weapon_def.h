#pragma once
// Weapon definitions (content/combat/weapons.json, type "weapons"). Design doc 8: a rapid laser
// and a slower high-damage projectile/missile; all stats are tunable data.
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "engine/assets/asset_manifest.h"
#include "engine/math/math.h"

namespace fh {

struct WeaponDef {
  std::string id;
  std::string name;
  float damage = 10;            // per hit
  float fireInterval = 0.2f;    // seconds between shots per mount
  float heatPerShot = 3;        // weapon heat added per shot
  float projectileSpeed = 900;  // m/s (relative to the shooter)
  float range = 1500;           // metres (lifetime = range / speed)
  float spreadDeg = 0.4f;
  float shieldMultiplier = 1.0f;
  float hullMultiplier = 1.0f;
  float homing = 0.0f;          // turn rate in rad/s toward the locked target (0 = dumb)
  float boltLength = 6.0f;      // visual streak length (m)
  Vec3 color{3.0f, 0.6f, 0.3f}; // HDR
  std::string sound = "laser_shot";
};

class WeaponDb {
 public:
  const WeaponDef* find(const std::string& id) const;
  void add(WeaponDef w) { defs_[w.id] = std::move(w); }
  const std::map<std::string, WeaponDef>& all() const { return defs_; }

 private:
  std::map<std::string, WeaponDef> defs_;
};

std::optional<WeaponDb> loadWeapons(const AssetManifest& manifest, const AssetID& id, std::vector<std::string>* errors);

}  // namespace fh
