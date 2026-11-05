#pragma once
// GameSession: the authoritative gameplay state that is independent of rendering.
// Owns the space world, docking, the player's location (cockpit / ship interior / station
// interior) and the active walkable interior. The app feeds it input and renders it; the
// headless integration test drives the very same object (tests/unit/test_session.cpp).
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "game/combat/combat.h"
#include "game/content/interior_def.h"
#include "game/missions/contracts.h"
#include "game/progression/upgrades.h"
#include "game/economy/economy.h"
#include "game/space/landing.h"
#include "game/space/mining.h"
#include "game/space/travel_drive.h"
#include "game/player/character_controller.h"
#include "game/space/docking.h"
#include <set>

#include "game/foot/foot_combat.h"
#include "game/foot/nav_grid.h"
#include "game/world/space_world.h"

namespace fh {

enum class Location { Cockpit, ShipInterior, StationInterior };
const char* locationName(Location l);

struct ActionResult {
  bool ok = false;
  std::string message;
};

class GameSession {
 public:
  explicit GameSession(std::unique_ptr<SpaceWorld> world);
  GameSession(const GameSession&) = delete;
  GameSession& operator=(const GameSession&) = delete;

  SpaceWorld& world() { return *world_; }
  const SpaceWorld& world() const { return *world_; }
  Docking& docking() { return docking_; }
  const Docking& docking() const { return docking_; }
  Location location() const { return location_; }
  TravelDrive& travel() { return travel_; }
  const TravelDrive& travel() const { return travel_; }
  Landing& landing() { return landing_; }
  const Landing& landing() const { return landing_; }
  // Docked at a station or landed at a site: the only states where leaving the seat is allowed.
  bool parked() const { return docking_.state() == DockState::Docked || landing_.state() == LandState::Landed; }
  bool freeFlight() const {
    return docking_.state() == DockState::Undocked && !landing_.busy() && !travel_.active();
  }
  bool engageTravel();               // to the current target (must be a nav destination)
  bool requestLanding();
  bool launch();
  // Interior to render while on the surface in the cockpit (landing approach, landed, liftoff).
  const InteriorDef* surfaceScene() const;
  const AssetID* surfaceSceneId() const;

  // Interiors available to this session (ship cabin + station hangars), keyed by AssetID.
  void setShipInterior(const AssetID& id) { shipInterior_ = id; }
  void addInterior(const AssetID& id, InteriorDef def) { interiors_[id] = std::move(def); }
  const InteriorDef* activeInterior() const;
  const AssetID& activeInteriorId() const { return activeInteriorId_; }
  const std::vector<Aabb>& activeColliders() const { return colliders_; }
  const HeightField& activeTerrain() const { return terrain_; }

  // On-foot character (valid when not in the cockpit).
  CharacterState& character() { return character_; }
  const CharacterState& character() const { return character_; }
  const CharacterParams& characterParams() const { return characterParams_; }
  CharacterInput& characterInput() { return characterInput_; }

  // Interaction: the interactable the player is currently looking at (if any).
  // Fighting on foot, in maps with enemies placed in them. A list of players, ready for co-op; the first
  // is this machine's.
  bool footArmed() const { return footArmed_; }
  const FootCombat& foot() const { return foot_; }
  FootCombat& foot() { return foot_; }
  const std::vector<FootPlayer>& footPlayers() const { return footPlayers_; }
  std::vector<FootPlayer>& footPlayers() { return footPlayers_; }
  void setFootTrigger(bool on) { footPlayers_.front().trigger = on; }
  void requestFootReload() { footPlayers_.front().reloadRequest = true; }
  void setFootNoisy(bool on) { footPlayers_.front().noisy = on; }
  void selectFootWeapon(int index) { footPlayers_.front().switchRequest = index; }
  void cycleFootWeapon(int direction);  // next/previous owned weapon
  // Weapons, enemy types and loot tables (content outlives the session); null = built-in defaults.
  void setFootContent(const FootContent* content);
  const NavGrid* activeNav() const;
  // Things dropped by killed enemies, still lying in the current map. Walk over one to take it.
  struct FootDrop {
    std::string key;    // "<scene>#drop<marker>"
    std::string table;  // loot table id
    Vec3 position{};
  };
  const std::vector<FootDrop>& footDrops() const { return drops_; }
  const std::map<std::string, Vec3>& dropPositions() const { return dropPositions_; }
  void setDropPositions(std::map<std::string, Vec3> d) { dropPositions_ = std::move(d); }
  std::vector<FootEvent> takeFootEvents() { return std::exchange(footEvents_, {}); }
  // Enemies killed and loot taken stay that way, in saves too. Keys are "<scene>#<marker>".
  bool markerCleared(int marker) const;
  const std::set<std::string>& clearedMarkers() const { return clearedMarkers_; }
  void setClearedMarkers(std::set<std::string> keys) { clearedMarkers_ = std::move(keys); }

  const Interactable* focusedInteractable() const;
  std::string promptFor(const Interactable& it) const;  // loot shows its table's prompt
  ActionResult interact();            // uses the focused interactable
  ActionResult perform(const std::string& action);
  ActionResult leaveSeat();           // cockpit -> ship interior (docked only)
  ActionResult enterMap(const AssetID& id);  // an interior added with addInterior, at spawn "start"
  bool consumeMapExit() { const bool r = mapExitRequested_; mapExitRequested_ = false; return r; }

  // Message shown to the player (prompts, refusals, terminal text) with its timestamp.
  const std::string& notice() const { return notice_; }
  void notify(const std::string& text) {
    notice_ = text;
    noticeTime_ = world_->simulationTime();
  }
  double noticeTime() const { return noticeTime_; }

  // Simulates space (always), mining, the character (on foot) and the floating origin.
  // Returns the rebase delta when the origin moved (callers shift their own local state).
  std::optional<Vec3> fixedUpdate(float dt);

  // ---- Economy (Stage 4) ----
  void setEconomy(CommodityDb db, std::int64_t credits);  // also sizes the hold from the ship def
  const CommodityDb& commodities() const { return commodities_; }
  std::int64_t credits() const { return credits_; }
  void setCredits(std::int64_t c) { credits_ = c; }
  CargoHold& cargo() { return cargo_; }
  const CargoHold& cargo() const { return cargo_; }
  void addMarket(const AssetID& id, const MarketDef& def) { markets_[id] = Market(def, world_->simulationTime()); }
  std::map<AssetID, Market>& markets() { return markets_; }
  Market* dockedMarket();            // market of the station we are docked at (advanced to now)
  TradeResult trade(const std::string& commodity, int units);  // > 0 buy, < 0 sell
  bool consumeMarketRequest() { const bool r = marketRequested_; marketRequested_ = false; return r; }

  // ---- Mining ----
  Mining& mining() { return mining_; }
  const Mining& mining() const { return mining_; }
  void setMiningTrigger(bool on) { miningTrigger_ = on; }
  const MiningEvents& lastMiningEvents() const { return miningEvents_; }

  // ---- Stage 6: combat, upgrades, contracts ----
  // Content is owned by the caller and must outlive the session. Call after setEconomy().
  void setProgression(const SceneDef& scene, const WeaponDb* weapons, const UpgradeDb* upgrades,
                      const ContractDb* contracts);
  Combat& combat() { return combat_; }
  const Combat& combat() const { return combat_; }
  void setFireTriggers(bool primary, bool secondary) {
    firePrimary_ = primary;
    fireSecondary_ = secondary;
  }
  // Events from the last fixed step(s), after the session has handled them (for audio/HUD).
  std::vector<CombatEvent> takeCombatEvents() { return std::exchange(combatEvents_, {}); }
  bool combatActive() const { return location_ == Location::Cockpit && freeFlight(); }

  // Services of the place we are parked at ("repair", "upgrades", "contracts").
  bool hasService(const std::string& s) const;
  std::string currentLocationId() const;  // station prop id / site id; empty when not parked
  const std::vector<ContractLocation>& contractLocations() const { return locations_; }

  const ShipDef& baseShip() const { return baseShip_; }
  const std::vector<std::string>& ownedUpgrades() const { return upgrades_; }
  const UpgradeDb* upgradeDb() const { return upgradeDb_; }
  bool ownsUpgrade(const std::string& id) const;
  ActionResult buyUpgrade(const std::string& id);
  void setOwnedUpgrades(std::vector<std::string> ids);  // save restore; re-derives the ship
  std::int64_t repairCost() const;
  ActionResult repair();

  ContractBook& contracts() { return contracts_; }
  const ContractBook& contracts() const { return contracts_; }
  const ContractDb* contractDb() const { return contractDb_; }
  std::vector<Contract> contractOffers() const;  // at the current location
  ActionResult acceptContract(const Contract& offer);
  ActionResult abandonContract(size_t index);
  int defeats() const { return defeats_; }
  void restoreDefeats(int n) { defeats_ = n; }
  std::int64_t insuranceDeductible() const { return deductible_; }

 private:
  void beginFoot(const InteriorDef& def);
  void stepFoot(float dt);
  ActionResult takeLoot(int marker);
  void refitFoot();       // somewhere safe: patched up, magazines full, at least the starting ammo
  void rebuildDrops();
  void pickUpDrops();
  FootCombat foot_;
  std::map<AssetID, std::unique_ptr<NavGrid>> navs_;
  std::vector<FootDrop> drops_;
  std::map<std::string, Vec3> dropPositions_;
  std::vector<FootPlayer> footPlayers_{FootPlayer{}};
  std::vector<FootEvent> footEvents_;
  std::set<std::string> clearedMarkers_;
  bool footArmed_ = false;
  ActionResult enterInterior(const AssetID& id, const std::string& spawn, Location loc);
  ActionResult fail(const std::string& msg);
  void applyShip();              // base ship + upgrades -> flown ship, hold, mining, combat
  void handleCombatEvents();
  void handleDefeat();
  void onParked();

  std::unique_ptr<SpaceWorld> world_;
  Docking docking_;
  TravelDrive travel_;
  Landing landing_;
  Location location_ = Location::Cockpit;
  bool mapExitRequested_ = false;
  AssetID shipInterior_;
  std::map<AssetID, InteriorDef> interiors_;
  AssetID activeInteriorId_;
  std::vector<Aabb> colliders_;
  HeightField terrain_;
  CharacterState character_;
  CharacterParams characterParams_;
  CharacterInput characterInput_;
  std::string notice_;
  double noticeTime_ = -100;
  CommodityDb commodities_;
  std::int64_t credits_ = 0;
  CargoHold cargo_;
  std::map<AssetID, Market> markets_;
  bool marketRequested_ = false;
  Mining mining_;
  bool miningTrigger_ = false;
  MiningEvents miningEvents_;
  // Stage 6.
  Combat combat_;
  bool firePrimary_ = false, fireSecondary_ = false;
  std::vector<CombatEvent> combatEvents_;
  ShipDef baseShip_;
  const UpgradeDb* upgradeDb_ = nullptr;
  const ContractDb* contractDb_ = nullptr;
  std::vector<std::string> upgrades_;
  ContractBook contracts_;
  std::vector<ContractLocation> locations_;
  std::optional<bool> wasParked_;
  std::int64_t deductible_ = 250;
  int defeats_ = 0;
  std::uint32_t lastCollisions_ = 0;
};

}  // namespace fh
