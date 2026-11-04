#include "game/progression/upgrades.h"

#include <algorithm>

#include "engine/core/file_io.h"
#include "game/content/json_util.h"

namespace fh {

const UpgradeDef* UpgradeDb::find(const std::string& id) const {
  for (const auto& d : defs_)
    if (d.id == id) return &d;
  return nullptr;
}

std::optional<UpgradeDb> loadUpgrades(const AssetManifest& manifest, const AssetID& id,
                                      std::vector<std::string>* errors) {
  auto path = manifest.resolve(id, "upgrades");
  auto text = path ? readTextFile(*path) : std::nullopt;
  content::Reader r("upgrades '" + id + "'", errors);
  if (!text) {
    r.error("missing or unreadable");
    return std::nullopt;
  }
  auto j = nlohmann::json::parse(*text, nullptr, false);
  if (j.is_discarded() || !j.contains("upgrades") || !j["upgrades"].is_array()) {
    r.error("invalid JSON or missing 'upgrades' array");
    return std::nullopt;
  }
  UpgradeDb db;
  db.repairPricePerHull = float(r.num(j, "repairPricePerHull", false, db.repairPricePerHull));
  for (const auto& uj : j["upgrades"]) {
    UpgradeDef u;
    u.id = r.str(uj, "id");
    u.name = r.str(uj, "name");
    u.description = r.str(uj, "description", false);
    u.price = std::int64_t(r.num(uj, "price"));
    if (u.price <= 0) r.error("upgrade '" + u.id + "': price must be > 0");
    if (db.find(u.id)) r.error("duplicate upgrade '" + u.id + "'");
    if (uj.contains("effects") && uj["effects"].is_object()) {
      const auto& ej = uj["effects"];
      UpgradeEffects& e = u.effects;
      e.cargoTons = float(r.num(ej, "cargoTons", false, 0));
      e.maxHull = float(r.num(ej, "maxHull", false, 0));
      e.maxShield = float(r.num(ej, "maxShield", false, 0));
      e.shieldRegen = float(r.num(ej, "shieldRegen", false, 0));
      e.accelerationScale = float(r.num(ej, "accelerationScale", false, 1));
      e.speedScale = float(r.num(ej, "speedScale", false, 1));
      e.turnScale = float(r.num(ej, "turnScale", false, 1));
      e.miningRateScale = float(r.num(ej, "miningRateScale", false, 1));
      e.weaponDamageScale = float(r.num(ej, "weaponDamageScale", false, 1));
      e.heatCapacity = float(r.num(ej, "heatCapacity", false, 0));
      for (const auto& [k, v] : ej.items()) {
        static const char* known[] = {"cargoTons", "maxHull", "maxShield", "shieldRegen", "accelerationScale",
                                      "speedScale", "turnScale", "miningRateScale", "weaponDamageScale",
                                      "heatCapacity"};
        if (std::none_of(std::begin(known), std::end(known), [&](const char* n) { return k == n; }))
          r.error("upgrade '" + u.id + "': unknown effect '" + k + "'");
      }
    } else {
      r.error("upgrade '" + u.id + "': missing 'effects' object");
    }
    db.add(u);
  }
  if (!r.ok()) return std::nullopt;
  return db;
}

ShipDef applyUpgrades(const ShipDef& base, const UpgradeDb& db, const std::vector<std::string>& owned,
                      float* weaponDamageScale) {
  ShipDef s = base;
  float dmg = 1.0f;
  for (const auto& id : owned) {
    const UpgradeDef* u = db.find(id);
    if (!u) continue;
    const UpgradeEffects& e = u->effects;
    s.cargoCapacityTons += e.cargoTons;
    s.maxHull += e.maxHull;
    s.maxShield += e.maxShield;
    s.combat.shieldRegen += e.shieldRegen;
    s.combat.heatCapacity += e.heatCapacity;
    s.flight.forwardAcceleration *= e.accelerationScale;
    s.flight.reverseAcceleration *= e.accelerationScale;
    s.flight.lateralAcceleration *= e.accelerationScale;
    s.flight.maxCruiseSpeed *= e.speedScale;
    s.flight.angularAcceleration *= e.turnScale;
    s.flight.maxAngularRate = s.flight.maxAngularRate * e.turnScale;
    s.mining.unitsPerSecond *= e.miningRateScale;
    dmg *= e.weaponDamageScale;
  }
  if (weaponDamageScale) *weaponDamageScale = dmg;
  return s;
}

}  // namespace fh
