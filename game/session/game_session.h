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

  // Interiors available to this session (ship cabin + station hangars), keyed by AssetID.
  void setShipInterior(const AssetID& id) { shipInterior_ = id; }
  void addInterior(const AssetID& id, InteriorDef def) { interiors_[id] = std::move(def); }
  const InteriorDef* activeInterior() const;
  const AssetID& activeInteriorId() const { return activeInteriorId_; }
  const std::vector<Aabb>& activeColliders() const { return colliders_; }

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
  double noticeTime() const { return noticeTime_; }

  void fixedUpdate(float dt);         // simulates space (always) and the character (on foot)

 private:
  ActionResult enterInterior(const AssetID& id, const std::string& spawn, Location loc);
  ActionResult fail(const std::string& msg);

  std::unique_ptr<SpaceWorld> world_;
  Docking docking_;
  Location location_ = Location::Cockpit;
  AssetID shipInterior_;
  std::map<AssetID, InteriorDef> interiors_;
  AssetID activeInteriorId_;
  std::vector<Aabb> colliders_;
  CharacterState character_;
  CharacterParams characterParams_;
  CharacterInput characterInput_;
  std::string notice_;
  double noticeTime_ = -100;
};

}  // namespace fh
