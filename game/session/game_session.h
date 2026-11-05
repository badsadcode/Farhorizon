#pragma once
// GameSession: the authoritative gameplay state that is independent of rendering.
// Owns the shared world (space, the ship, its hold, markets, what has been killed and taken) and the
// crew: 1-4 players, each with their own location, body, wallet and gun (ADR 0014). Walkable places
// with people in them are instances with their own enemies; a ship's cabin is its own place, in the
// ship's frame. Player slot 0 is this machine's player, and the single-player accessors (location(),
// character(), credits(), ...) all mean that player. The app feeds it input and renders it; the
// headless tests drive the very same object, with simulated players (game/session/sim_player.h).
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

using PlayerId = std::uint32_t;

// One person in the session. Everything that is "theirs": where they are and what they carry.
struct CrewMember {
  PlayerId id = 0;
  std::string name;
  Location location = Location::Cockpit;
  bool pilot = true;               // in the cockpit: at the helm (else a passenger seat)
  AssetID interior;                // the place they are in when not in the cockpit
  CharacterState character;        // in that place's frame (the ship's own frame for its cabin)
  CharacterInput input;
  std::int64_t credits = 0;        // personal wallet
  std::string notice;
  double noticeTime = -100;
  bool marketRequested = false;
  bool mapExitRequested = false;
  std::vector<FootEvent> footEvents;  // from the place they are in, in their own numbering
};

class GameSession {
 public:
  explicit GameSession(std::unique_ptr<SpaceWorld> world);
  GameSession(const GameSession&) = delete;
  GameSession& operator=(const GameSession&) = delete;

  // ---- Crew (co-op): slot 0 is this machine's player and is never removed ----
  PlayerId addPlayer(const std::string& name);  // joins seated in the cockpit; max 4
  bool removePlayer(PlayerId id);
  static constexpr int kMaxPlayers = 4;
  // Personal wallets of crew who have played on this ship, by name: kept while they are away and
  // given back when they rejoin (and saved). The host's own wallet is credits().
  const std::map<std::string, std::int64_t>& crewWallets() const { return crewWallets_; }
  void setCrewWallets(std::map<std::string, std::int64_t> w) { crewWallets_ = std::move(w); }
  std::map<std::string, std::int64_t> allCrewWallets() const;  // away crew plus everyone aboard (not the host)
  const std::vector<CrewMember>& crew() const { return players_; }
  CrewMember* player(PlayerId id);
  const CrewMember* player(PlayerId id) const;
  PlayerId localPlayer() const { return players_.front().id; }
  FootPlayer* footPlayer(PlayerId id);
  bool crewAboard() const;          // everyone is seated in the cockpit (the ship may leave)
  ActionResult undock(PlayerId by);
  ActionResult undock() { return undock(localPlayer()); }
  ActionResult perform(PlayerId who, const std::string& action);
  ActionResult interact(PlayerId who);
  ActionResult leaveSeat(PlayerId who);
  ActionResult enterMap(PlayerId who, const AssetID& id);
  const Interactable* focusedInteractable(PlayerId who) const;
  const InteriorDef* interiorOf(PlayerId who) const;
  const FootCombat* footCombatAt(const AssetID& interior) const;  // enemies of a place with people in it
  TradeResult trade(PlayerId who, const std::string& commodity, int units);
  void notifyPlayer(PlayerId who, const std::string& text);

  SpaceWorld& world() { return *world_; }
  const SpaceWorld& world() const { return *world_; }
  Docking& docking() { return docking_; }
  const Docking& docking() const { return docking_; }
  Location location() const { return players_.front().location; }
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
  bool launch();  // the local player, who must be the pilot, with the whole crew seated
  // Interior to render while on the surface in the cockpit (landing approach, landed, liftoff).
  const InteriorDef* surfaceScene() const;
  const AssetID* surfaceSceneId() const;

  // Interiors available to this session (ship cabin + station hangars), keyed by AssetID.
  void setShipInterior(const AssetID& id) { shipInterior_ = id; }
  void addInterior(const AssetID& id, InteriorDef def) { interiors_[id] = std::move(def); }
  const InteriorDef* activeInterior() const { return interiorOf(localPlayer()); }
  const AssetID& activeInteriorId() const;
  const std::vector<Aabb>& activeColliders() const;
  const HeightField& activeTerrain() const;

  // On-foot character (valid when not in the cockpit).
  CharacterState& character() { return players_.front().character; }
  const CharacterState& character() const { return players_.front().character; }
  const CharacterParams& characterParams() const { return characterParams_; }
  CharacterInput& characterInput() { return players_.front().input; }

  // Interaction: the interactable the player is currently looking at (if any).
  // Fighting on foot, in maps with enemies placed in them. A list of players, ready for co-op; the first
  // is this machine's.
  bool footArmed() const;
  // The fight in the local player's place (an idle one, with the same content, anywhere else).
  const FootCombat& foot() const;
  FootCombat& foot();
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
  const NavGrid* navAt(const AssetID& interior) const;  // baked the first time someone enters an armed map
  // Things dropped by killed enemies, still lying in the current map. Walk over one to take it.
  struct FootDrop {
    std::string key;    // "<scene>#drop<marker>"
    std::string table;  // loot table id
    Vec3 position{};
  };
  const std::vector<FootDrop>& footDrops() const;
  const std::map<std::string, Vec3>& dropPositions() const { return dropPositions_; }
  void setDropPositions(std::map<std::string, Vec3> d) { dropPositions_ = std::move(d); }
  std::vector<FootEvent> takeFootEvents() { return std::exchange(players_.front().footEvents, {}); }
  // Enemies killed and loot taken stay that way, in saves too. Keys are "<scene>#<marker>".
  bool markerCleared(int marker) const { return markerCleared(activeInteriorId(), marker); }
  bool markerCleared(const AssetID& interior, int marker) const;
  const std::set<std::string>& clearedMarkers() const { return clearedMarkers_; }
  void setClearedMarkers(std::set<std::string> keys) { clearedMarkers_ = std::move(keys); }

  const Interactable* focusedInteractable() const { return focusedInteractable(localPlayer()); }
  std::string promptFor(const Interactable& it) const;  // loot shows its table's prompt
  ActionResult interact() { return interact(localPlayer()); }  // uses the focused interactable
  ActionResult perform(const std::string& action) { return perform(localPlayer(), action); }
  ActionResult leaveSeat() { return leaveSeat(localPlayer()); }  // cockpit -> ship interior (parked only)
  ActionResult enterMap(const AssetID& id) { return enterMap(localPlayer(), id); }  // at spawn "start"
  bool consumeMapExit() { return std::exchange(players_.front().mapExitRequested, false); }

  // Message shown to the local player (prompts, refusals, terminal text) with its timestamp.
  const std::string& notice() const { return players_.front().notice; }
  void notify(const std::string& text) { notifyPlayer(localPlayer(), text); }
  double noticeTime() const { return players_.front().noticeTime; }

  // Simulates space (always), mining, the character (on foot) and the floating origin.
  // Returns the rebase delta when the origin moved (callers shift their own local state).
  std::optional<Vec3> fixedUpdate(float dt);

  // ---- Economy (Stage 4) ----
  void setEconomy(CommodityDb db, std::int64_t credits);  // also sizes the hold from the ship def
  const CommodityDb& commodities() const { return commodities_; }
  std::int64_t credits() const { return players_.front().credits; }  // the local player's wallet
  void setCredits(std::int64_t c) { players_.front().credits = c; }
  CargoHold& cargo() { return cargo_; }
  const CargoHold& cargo() const { return cargo_; }
  void addMarket(const AssetID& id, const MarketDef& def) { markets_[id] = Market(def, world_->simulationTime()); }
  std::map<AssetID, Market>& markets() { return markets_; }
  Market* dockedMarket();            // market of the station we are docked at (advanced to now)
  TradeResult trade(const std::string& commodity, int units) { return trade(localPlayer(), commodity, units); }  // > 0 buy
  bool consumeMarketRequest() { return std::exchange(players_.front().marketRequested, false); }

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
  // In open space, whether or not anyone is at the helm: leaving the seat does not make pirates go away.
  bool combatActive() const { return freeFlight() && !networked_; }
  // Played over a network. Until the host can share them, a networked game has no pirates, mining, landing
  // or fights on foot. A client's session also simulates only the walking: the host runs everything else.
  void setNetworked(bool on) { networked_ = on; }
  bool networked() const { return networked_; }
  void setNetClient(bool on);
  bool netClient() const { return netClient_; }
  void netApplyShipStatus(bool docked, std::uint32_t prop, std::uint32_t bay, float hull, float shield);
  void netApplyPlace(PlayerId who, Location where, const AssetID& interior, bool pilot);
  // How hard the ship was just hit, 0..1. The app turns this into camera shake.
  float takeShipJolt() { return std::exchange(shipJolt_, 0.0f); }

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
  bool networked_ = false;
  bool netClient_ = false;
  float shipJolt_ = 0;
  // A walkable place with at least one player in it: its own colliders and its own fight.
  struct Instance {
    std::vector<Aabb> colliders;
    HeightField terrain;
    float gravity = 9.81f;
    FootCombat foot;
    bool armed = false;
    std::vector<FootDrop> drops;
  };
  int slotOf(PlayerId id) const;
  int pilotSlot() const;
  Instance* instanceOf(int slot);
  const Instance* instanceOf(int slot) const;
  void leavePlace(int slot);                  // out of whatever instance it was in
  void beginInstance(const AssetID& id, Instance& in, const InteriorDef& def);
  void stepInstance(const AssetID& id, Instance& in, float dt);
  ActionResult takeLoot(int slot, int marker);
  void refitFoot(int slot);  // somewhere safe: patched up, magazines full, at least the starting ammo
  void rebuildDrops(const AssetID& id, Instance& in);
  void pickUpDrops(const AssetID& id, Instance& in);
  std::map<AssetID, std::unique_ptr<NavGrid>> navs_;
  std::map<AssetID, Instance> instances_;
  FootCombat idleFoot_;                      // content and loadouts when not in a place with a fight
  std::map<std::string, Vec3> dropPositions_;
  std::vector<CrewMember> players_;          // slot 0: this machine's player
  std::vector<FootPlayer> footPlayers_;      // one per player slot (the fight's view of them)
  PlayerId nextPlayerId_ = 1;
  std::map<std::string, std::int64_t> crewWallets_;
  std::set<std::string> clearedMarkers_;
  ActionResult enterInterior(int slot, const AssetID& id, const std::string& spawn, Location loc);
  ActionResult fail(const std::string& msg) { return failFor(0, msg); }
  ActionResult failFor(int slot, const std::string& msg);
  void applyShip();              // base ship + upgrades -> flown ship, hold, mining, combat
  void handleCombatEvents();
  void handleDefeat();
  void onParked();

  std::unique_ptr<SpaceWorld> world_;
  Docking docking_;
  TravelDrive travel_;
  Landing landing_;
  AssetID shipInterior_;
  std::map<AssetID, InteriorDef> interiors_;
  CharacterParams characterParams_;
  CommodityDb commodities_;
  CargoHold cargo_;                          // the ship's hold, shared by the crew
  std::map<AssetID, Market> markets_;
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
