#include "game/session/progression_setup.h"

namespace fh {

std::optional<ProgressionContent> loadProgression(const AssetManifest& manifest, const SceneDef& scene,
                                                  const CommodityDb& commodities, std::vector<std::string>* errors) {
  auto weapons = loadWeapons(manifest, "combat.weapons", errors);
  auto upgrades = loadUpgrades(manifest, "economy.upgrades", errors);
  auto contracts = loadContracts(manifest, "missions.contracts", commodities, errors);
  if (!weapons || !upgrades || !contracts) return std::nullopt;
  ProgressionContent c{std::move(*weapons), std::move(*upgrades), std::move(*contracts), {}};
  std::vector<AssetID> ids;
  for (const auto& e : scene.encounters) ids.push_back(e.ship);
  for (const auto& t : scene.traffic) ids.push_back(t.ship);
  for (const auto& id : ids) {
    if (c.npcShips.count(id)) continue;
    auto def = loadShipDef(manifest, id, errors);
    if (!def) return std::nullopt;
    c.npcShips[id] = std::move(*def);
  }
  return c;
}

void installProgression(GameSession& session, const SceneDef& scene, const ProgressionContent& content,
                        const std::function<void(const ShipDef&, ShipType&)>& visual) {
  session.setProgression(scene, &content.weapons, &content.upgrades, &content.contracts);
  for (const auto& [id, def] : content.npcShips) {
    ShipType t;
    t.def = def;
    t.radius = def.collisionRadius > 0 ? def.collisionRadius : 6.0f;
    if (visual) visual(def, t);
    session.combat().registerShipType(id, std::move(t));
  }
}

}  // namespace fh
