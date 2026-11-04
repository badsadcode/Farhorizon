#pragma once
// Ship upgrades (design doc 9): one-off purchases at a shipyard that modify the effective ship
// definition. The base ShipDef stays untouched; applyUpgrades() derives the flown one, so saves
// only need the list of owned upgrade ids. Content: content/economy/upgrades.json.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "engine/assets/asset_manifest.h"
#include "game/content/ship_def.h"

namespace fh {

struct UpgradeEffects {
  float cargoTons = 0;          // added capacity
  float maxHull = 0;            // added hull points
  float maxShield = 0;          // added shield points
  float shieldRegen = 0;        // added points/second
  float accelerationScale = 1;  // multiplies all linear accelerations
  float speedScale = 1;         // multiplies max cruise speed
  float turnScale = 1;          // multiplies angular acceleration and rates
  float miningRateScale = 1;    // multiplies mining units/second
  float weaponDamageScale = 1;  // multiplies weapon damage
  float heatCapacity = 0;       // added weapon heat capacity
};

struct UpgradeDef {
  std::string id;
  std::string name;
  std::string description;
  std::int64_t price = 0;
  UpgradeEffects effects;
};

class UpgradeDb {
 public:
  const UpgradeDef* find(const std::string& id) const;
  const std::vector<UpgradeDef>& all() const { return defs_; }
  void add(UpgradeDef d) { defs_.push_back(std::move(d)); }
  float repairPricePerHull = 3.0f;  // credits per hull point at a repair service

 private:
  std::vector<UpgradeDef> defs_;
};

std::optional<UpgradeDb> loadUpgrades(const AssetManifest& manifest, const AssetID& id,
                                      std::vector<std::string>* errors);

// Effective ship = base + owned upgrades (unknown ids are ignored). Also returns the weapon damage
// scale, which lives on the armament rather than the ship definition.
ShipDef applyUpgrades(const ShipDef& base, const UpgradeDb& db, const std::vector<std::string>& owned,
                      float* weaponDamageScale = nullptr);

}  // namespace fh
