#include "game/combat/weapon_def.h"

#include "engine/core/file_io.h"
#include "game/content/json_util.h"

namespace fh {

const WeaponDef* WeaponDb::find(const std::string& id) const {
  auto it = defs_.find(id);
  return it == defs_.end() ? nullptr : &it->second;
}

std::optional<WeaponDb> loadWeapons(const AssetManifest& manifest, const AssetID& id, std::vector<std::string>* errors) {
  auto path = manifest.resolve(id, "weapons");
  auto text = path ? readTextFile(*path) : std::nullopt;
  content::Reader r("weapons '" + id + "'", errors);
  if (!text) {
    r.error("missing or unreadable");
    return std::nullopt;
  }
  auto j = nlohmann::json::parse(*text, nullptr, false);
  if (j.is_discarded() || !j.contains("weapons") || !j["weapons"].is_array()) {
    r.error("invalid JSON or missing 'weapons'");
    return std::nullopt;
  }
  WeaponDb db;
  for (const auto& wj : j["weapons"]) {
    WeaponDef w;
    w.id = r.str(wj, "id");
    w.name = r.str(wj, "name");
    w.damage = float(r.num(wj, "damage"));
    w.fireInterval = float(r.num(wj, "fireInterval"));
    w.heatPerShot = float(r.num(wj, "heatPerShot", false, w.heatPerShot));
    w.projectileSpeed = float(r.num(wj, "projectileSpeed"));
    w.range = float(r.num(wj, "range"));
    w.spreadDeg = float(r.num(wj, "spreadDeg", false, w.spreadDeg));
    w.shieldMultiplier = float(r.num(wj, "shieldMultiplier", false, 1.0));
    w.hullMultiplier = float(r.num(wj, "hullMultiplier", false, 1.0));
    w.homing = float(r.num(wj, "homing", false, 0.0));
    w.boltLength = float(r.num(wj, "boltLength", false, w.boltLength));
    w.color = r.vec3(wj, "color", false, w.color);
    w.sound = r.str(wj, "sound", false, w.sound);
    if (w.damage <= 0 || w.fireInterval <= 0 || w.projectileSpeed <= 0 || w.range <= 0)
      r.error("weapon '" + w.id + "': damage, fireInterval, projectileSpeed and range must be > 0");
    db.add(w);
  }
  if (!r.ok()) return std::nullopt;
  return db;
}

}  // namespace fh
