#pragma once
// GameSession: the authoritative gameplay state that is independent of rendering.
// Owns the space world, docking, the player's location (cockpit / ship interior / station
// interior) and the active walkable interior. The app feeds it input and renders it; the
// headless integration test drives the very same object (tests/unit/test_session.cpp).
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "game/content/interior_def.h"
#include "game/economy/economy.h"
#include "game/space/landing.h"
#include "game/space/mining.h"
#include "game/space/travel_drive.h"
#include "game/player/character_controller.h"
#include "game/space/docking.h"
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
  explicit GameSession(std::unique_ptr<SpaceWorld> world) : world_(std::move(world)) {}

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
  const Interactable* focusedInteractable() const;
  ActionResult interact();            // uses the focused interactable
  ActionResult perform(const std::string& action);
  ActionResult leaveSeat();           // cockpit -> ship interior (docked only)

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

 private:
  ActionResult enterInterior(const AssetID& id, const std::string& spawn, Location loc);
  ActionResult fail(const std::string& msg);

  std::unique_ptr<SpaceWorld> world_;
  Docking docking_;
  TravelDrive travel_;
  Landing landing_;
  Location location_ = Location::Cockpit;
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
};

}  // namespace fh
