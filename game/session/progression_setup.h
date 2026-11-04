#pragma once
// Loads the Stage 6 content (weapons, NPC ship types, upgrades, contracts) and installs it into a
// session. Shared by the game and the headless tests so both run exactly the same setup.
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "game/session/game_session.h"

namespace fh {

struct ProgressionContent {
  WeaponDb weapons;
  UpgradeDb upgrades;
  ContractDb contracts;
  std::map<AssetID, ShipDef> npcShips;  // every ship referenced by encounters and traffic
};

std::optional<ProgressionContent> loadProgression(const AssetManifest& manifest, const SceneDef& scene,
                                                  const CommodityDb& commodities, std::vector<std::string>* errors);

// `visual` (optional) uploads the NPC mesh and fills visual/meshFix/radius.
void installProgression(GameSession& session, const SceneDef& scene, const ProgressionContent& content,
                        const std::function<void(const ShipDef&, ShipType&)>& visual = {});

}  // namespace fh
