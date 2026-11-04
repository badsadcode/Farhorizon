#pragma once
// Contracts (design doc 9): small jobs offered on station and outpost terminals.
//  * courier     - carry sealed cargo to another location,
//  * procurement - bring N units of a commodity back to the issuer,
//  * bounty      - destroy N ships of a faction, then report to the issuer.
// Offers are generated deterministically from (location, world day, slot), so they need no
// storage: a save only keeps accepted contracts and the keys of offers already taken.
// Progress is driven by game events (parked at a location, ship destroyed). GPU-free, unit-tested.
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "engine/assets/asset_manifest.h"
#include "game/economy/economy.h"

namespace fh {

enum class ContractKind { Courier, Procurement, Bounty };
const char* contractKindName(ContractKind k);
std::optional<ContractKind> contractKindFromName(const std::string& n);

struct ContractTemplate {
  std::string id;
  ContractKind kind = ContractKind::Courier;
  std::string title;        // placeholders: {units} {commodity} {dest} {issuer} {kills} {faction}
  std::string description;
  std::vector<std::string> issuers;       // location ids; empty = everywhere
  std::vector<std::string> destinations;  // courier; empty = any other location
  std::vector<std::string> commodities;   // procurement choices / courier cargo (first)
  int unitsMin = 1, unitsMax = 1;
  int killsMin = 1, killsMax = 1;
  std::string faction = "pirate";
  float rewardBase = 0;
  float rewardPerUnit = 0;
  float priceMultiplier = 0;  // procurement: x commodity base price per unit
  float rewardPerKill = 0;
  float weight = 1;
};

struct ContractDb {
  std::vector<ContractTemplate> templates;
  int offersPerLocation = 3;
  int maxActive = 3;
  double daySeconds = 1800.0;  // offers refresh every world "day"
  const ContractTemplate* find(const std::string& id) const;
};

std::optional<ContractDb> loadContracts(const AssetManifest& manifest, const AssetID& id, const CommodityDb& commodities,
                                        std::vector<std::string>* errors);

struct Contract {
  std::string key;          // offer key "location/day/slot" (unique)
  std::string templateId;
  ContractKind kind = ContractKind::Courier;
  std::string title;
  std::string description;
  std::string issuer;       // location id
  std::string destination;  // courier
  std::string commodity;
  int units = 0;
  std::string faction;
  int killsRequired = 0;
  int kills = 0;
  std::int64_t reward = 0;
  bool ready() const { return kind != ContractKind::Bounty || kills >= killsRequired; }
};

struct ContractLocation {
  std::string id;
  std::string name;
};

struct ContractUpdate {
  std::vector<std::string> messages;
  std::int64_t paid = 0;
  int completed = 0;
};

class ContractBook {
 public:
  // Offers available at `location` today (taken offers removed).
  std::vector<Contract> offers(const ContractDb& db, const CommodityDb& commodities, const std::string& location,
                               const std::vector<ContractLocation>& locations, double worldTime) const;
  // Accepts an offer (courier cargo is loaded now). Fails with a reason.
  std::optional<std::string> accept(const ContractDb& db, const Contract& offer, CargoHold& cargo);
  // Abandons an active contract; courier cargo is forfeited.
  void abandon(size_t index, CargoHold& cargo);
  // Event hooks.
  void onShipDestroyed(const std::string& faction, std::vector<std::string>* messages = nullptr);
  ContractUpdate onParked(const std::string& location, const std::vector<ContractLocation>& locations,
                          CargoHold& cargo, std::int64_t& credits);
  // The ship was lost: courier cargo is gone, so those contracts fail.
  std::vector<std::string> onShipLost();

  const std::vector<Contract>& active() const { return active_; }
  const std::set<std::string>& taken() const { return taken_; }
  int completedCount() const { return completed_; }
  // Persistence.
  void restore(std::vector<Contract> active, std::set<std::string> taken, int completed) {
    active_ = std::move(active);
    taken_ = std::move(taken);
    completed_ = completed;
  }

 private:
  std::vector<Contract> active_;
  std::set<std::string> taken_;
  int completed_ = 0;
};

std::string locationDisplayName(const std::vector<ContractLocation>& locations, const std::string& id);

}  // namespace fh
