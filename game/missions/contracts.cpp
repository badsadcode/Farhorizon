#include "game/missions/contracts.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "engine/core/file_io.h"
#include "engine/core/log.h"
#include "game/content/json_util.h"

namespace fh {

namespace {
std::uint64_t fnv1a(const std::string& s) {
  std::uint64_t h = 1469598103934665603ull;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ull;
  }
  return h;
}

struct SplitMix {
  std::uint64_t s;
  std::uint64_t next() {
    std::uint64_t z = (s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
  float uniform() { return float(next() >> 40) / float(1 << 24); }
  int range(int lo, int hi) { return hi <= lo ? lo : lo + int(next() % std::uint64_t(hi - lo + 1)); }
};

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
  for (size_t p = s.find(from); p != std::string::npos; p = s.find(from, p + to.size())) s.replace(p, from.size(), to);
  return s;
}

std::int64_t roundReward(double r) { return std::max<std::int64_t>(5, std::int64_t(std::lround(r / 5.0)) * 5); }
}  // namespace

const char* contractKindName(ContractKind k) {
  switch (k) {
    case ContractKind::Courier: return "courier";
    case ContractKind::Procurement: return "procurement";
    case ContractKind::Bounty: return "bounty";
  }
  return "?";
}

std::optional<ContractKind> contractKindFromName(const std::string& n) {
  if (n == "courier") return ContractKind::Courier;
  if (n == "procurement") return ContractKind::Procurement;
  if (n == "bounty") return ContractKind::Bounty;
  return std::nullopt;
}

const ContractTemplate* ContractDb::find(const std::string& id) const {
  for (const auto& t : templates)
    if (t.id == id) return &t;
  return nullptr;
}

std::optional<ContractDb> loadContracts(const AssetManifest& manifest, const AssetID& id, const CommodityDb& commodities,
                                        std::vector<std::string>* errors) {
  auto path = manifest.resolve(id, "contracts");
  auto text = path ? readTextFile(*path) : std::nullopt;
  content::Reader r("contracts '" + id + "'", errors);
  if (!text) {
    r.error("missing or unreadable");
    return std::nullopt;
  }
  auto j = nlohmann::json::parse(*text, nullptr, false);
  if (j.is_discarded() || !j.contains("templates") || !j["templates"].is_array()) {
    r.error("invalid JSON or missing 'templates' array");
    return std::nullopt;
  }
  ContractDb db;
  db.offersPerLocation = int(r.num(j, "offersPerLocation", false, db.offersPerLocation));
  db.maxActive = int(r.num(j, "maxActive", false, db.maxActive));
  db.daySeconds = r.num(j, "daySeconds", false, db.daySeconds);
  if (db.daySeconds < 60) r.error("daySeconds must be >= 60");
  auto strings = [&](const nlohmann::json& o, const char* key) {
    std::vector<std::string> v;
    if (o.contains(key) && o[key].is_array())
      for (const auto& s : o[key])
        if (s.is_string()) v.push_back(s.get<std::string>());
    return v;
  };
  for (const auto& tj : j["templates"]) {
    ContractTemplate t;
    t.id = r.str(tj, "id");
    const auto kind = contractKindFromName(r.str(tj, "kind"));
    if (!kind) r.error("template '" + t.id + "': kind must be courier, procurement or bounty");
    t.kind = kind.value_or(ContractKind::Courier);
    t.title = r.str(tj, "title");
    t.description = r.str(tj, "description", false);
    t.issuers = strings(tj, "issuers");
    t.destinations = strings(tj, "destinations");
    t.commodities = strings(tj, "commodities");
    t.unitsMin = int(r.num(tj, "unitsMin", false, 1));
    t.unitsMax = int(r.num(tj, "unitsMax", false, t.unitsMin));
    t.killsMin = int(r.num(tj, "killsMin", false, 1));
    t.killsMax = int(r.num(tj, "killsMax", false, t.killsMin));
    t.faction = r.str(tj, "faction", false, "pirate");
    t.rewardBase = float(r.num(tj, "rewardBase", false, 0));
    t.rewardPerUnit = float(r.num(tj, "rewardPerUnit", false, 0));
    t.priceMultiplier = float(r.num(tj, "priceMultiplier", false, 0));
    t.rewardPerKill = float(r.num(tj, "rewardPerKill", false, 0));
    t.weight = float(r.num(tj, "weight", false, 1));
    if (t.unitsMax < t.unitsMin || t.unitsMin < 1) r.error("template '" + t.id + "': bad units range");
    if (t.killsMax < t.killsMin || t.killsMin < 1) r.error("template '" + t.id + "': bad kills range");
    if (t.kind != ContractKind::Bounty && t.commodities.empty())
      r.error("template '" + t.id + "': needs 'commodities'");
    for (const auto& c : t.commodities)
      if (!commodities.find(c)) r.error("template '" + t.id + "': unknown commodity '" + c + "'");
    db.templates.push_back(t);
  }
  if (db.templates.empty()) r.error("no templates");
  if (!r.ok()) return std::nullopt;
  return db;
}

std::string locationDisplayName(const std::vector<ContractLocation>& locations, const std::string& id) {
  for (const auto& l : locations)
    if (l.id == id) return l.name;
  return id;
}

std::vector<Contract> ContractBook::offers(const ContractDb& db, const CommodityDb& commodities,
                                           const std::string& location, const std::vector<ContractLocation>& locations,
                                           double worldTime) const {
  std::vector<Contract> out;
  if (location.empty()) return out;
  const auto day = std::int64_t(std::floor(worldTime / db.daySeconds));
  std::vector<const ContractTemplate*> pool;
  float total = 0;
  for (const auto& t : db.templates) {
    const bool here = t.issuers.empty() || std::find(t.issuers.begin(), t.issuers.end(), location) != t.issuers.end();
    if (!here || t.weight <= 0) continue;
    pool.push_back(&t);
    total += t.weight;
  }
  if (pool.empty()) return out;
  for (int slot = 0; slot < db.offersPerLocation; ++slot) {
    const std::string key = std::format("{}/{}/{}", location, day, slot);
    SplitMix rng{fnv1a(key)};
    float pick = rng.uniform() * total;
    const ContractTemplate* t = pool.back();
    for (const auto* c : pool) {
      pick -= c->weight;
      if (pick <= 0) {
        t = c;
        break;
      }
    }
    Contract c;
    c.key = key;
    c.templateId = t->id;
    c.kind = t->kind;
    c.issuer = location;
    c.faction = t->faction;
    if (!t->commodities.empty()) c.commodity = t->commodities[size_t(rng.next() % t->commodities.size())];
    c.units = rng.range(t->unitsMin, t->unitsMax);
    double reward = t->rewardBase + double(c.units) * t->rewardPerUnit;
    if (t->kind == ContractKind::Courier) {
      std::vector<std::string> dests = t->destinations;
      if (dests.empty())
        for (const auto& l : locations) dests.push_back(l.id);
      std::erase(dests, location);
      if (dests.empty()) continue;
      c.destination = dests[size_t(rng.next() % dests.size())];
    } else if (t->kind == ContractKind::Procurement) {
      if (const CommodityDef* cd = commodities.find(c.commodity))
        reward += double(c.units) * cd->basePrice * t->priceMultiplier;
    } else {
      c.killsRequired = rng.range(t->killsMin, t->killsMax);
      reward += double(c.killsRequired) * t->rewardPerKill;
      c.units = 0;
    }
    c.reward = roundReward(reward);
    const CommodityDef* cd = commodities.find(c.commodity);
    auto fill = [&](std::string s) {
      s = replaceAll(s, "{units}", std::to_string(c.units));
      s = replaceAll(s, "{commodity}", cd ? cd->name : c.commodity);
      s = replaceAll(s, "{dest}", locationDisplayName(locations, c.destination));
      s = replaceAll(s, "{issuer}", locationDisplayName(locations, c.issuer));
      s = replaceAll(s, "{kills}", std::to_string(c.killsRequired));
      s = replaceAll(s, "{faction}", c.faction);
      return s;
    };
    c.title = fill(t->title);
    c.description = fill(t->description);
    if (taken_.count(key)) continue;
    out.push_back(c);
  }
  return out;
}

std::optional<std::string> ContractBook::accept(const ContractDb& db, const Contract& offer, CargoHold& cargo) {
  if (taken_.count(offer.key)) return "That job has already been taken";
  if (int(active_.size()) >= db.maxActive) return std::format("You can hold at most {} contracts", db.maxActive);
  if (offer.kind == ContractKind::Courier && !cargo.add(offer.commodity, offer.units))
    return std::format("Not enough cargo space ({} units needed)", offer.units);
  active_.push_back(offer);
  taken_.insert(offer.key);
  log::info("contracts", "accepted '{}' ({}, {} cr)", offer.title, contractKindName(offer.kind), offer.reward);
  return std::nullopt;
}

void ContractBook::abandon(size_t index, CargoHold& cargo) {
  if (index >= active_.size()) return;
  const Contract& c = active_[index];
  if (c.kind == ContractKind::Courier) cargo.remove(c.commodity, std::min(c.units, cargo.units(c.commodity)));
  log::info("contracts", "abandoned '{}'", c.title);
  active_.erase(active_.begin() + std::ptrdiff_t(index));
}

void ContractBook::onShipDestroyed(const std::string& faction, std::vector<std::string>* messages) {
  for (auto& c : active_) {
    if (c.kind != ContractKind::Bounty || c.faction != faction || c.kills >= c.killsRequired) continue;
    ++c.kills;
    if (messages)
      messages->push_back(c.ready() ? "Bounty complete: report to the issuer"
                                    : std::format("Bounty: {}/{} confirmed", c.kills, c.killsRequired));
    break;  // one kill counts toward one contract
  }
}

ContractUpdate ContractBook::onParked(const std::string& location, const std::vector<ContractLocation>& locations,
                                      CargoHold& cargo, std::int64_t& credits) {
  ContractUpdate u;
  for (size_t i = 0; i < active_.size();) {
    Contract& c = active_[i];
    bool done = false;
    if (c.kind == ContractKind::Courier && c.destination == location) {
      if (cargo.units(c.commodity) >= c.units) {
        cargo.remove(c.commodity, c.units);
        done = true;
      } else {
        u.messages.push_back("Courier: the sealed cargo is missing");
      }
    } else if (c.kind == ContractKind::Procurement && c.issuer == location) {
      if (cargo.units(c.commodity) >= c.units) {
        cargo.remove(c.commodity, c.units);
        done = true;
      }
    } else if (c.kind == ContractKind::Bounty && c.issuer == location && c.ready()) {
      done = true;
    }
    if (done) {
      credits += c.reward;
      u.paid += c.reward;
      ++u.completed;
      ++completed_;
      u.messages.push_back(std::format("Contract complete: {} (+{} cr) at {}", c.title, c.reward,
                                       locationDisplayName(locations, location)));
      log::info("contracts", "completed '{}' at {} (+{} cr)", c.title, location, c.reward);
      active_.erase(active_.begin() + std::ptrdiff_t(i));
    } else {
      ++i;
    }
  }
  return u;
}

std::vector<std::string> ContractBook::onShipLost() {
  std::vector<std::string> msgs;
  std::erase_if(active_, [&](const Contract& c) {
    if (c.kind != ContractKind::Courier) return false;
    msgs.push_back("Contract failed: " + c.title);
    return true;
  });
  return msgs;
}

}  // namespace fh
