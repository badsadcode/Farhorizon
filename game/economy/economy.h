#pragma once
// Commodities, cargo and station markets (design doc 6 / 7.1). GPU-free, deterministic, unit-tested.
//
// Pricing:  mid = basePrice * clamp(1 + elasticity * (target - stock) / target, minMul, maxMul)
//           player buys at mid * (1 + spread/2), sells at mid * (1 - spread/2)
// Every unit of a trade is priced against the stock *after* the previous unit, so large trades
// move the price and a buy-then-sell round trip always loses money (spread + slippage).
// Restock/production runs in discrete ticks of world time, only when a market is touched.
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "engine/assets/asset_manifest.h"

namespace fh {

struct CommodityDef {
  std::string id;
  std::string name;
  std::string category;   // ore, goods, ...
  float basePrice = 10;   // credits per unit
  float massTons = 1;     // per unit
  bool legal = true;
  std::string description;
};

class CommodityDb {
 public:
  void add(CommodityDef c) { defs_[c.id] = std::move(c); }
  const CommodityDef* find(const std::string& id) const;
  const std::map<std::string, CommodityDef>& all() const { return defs_; }

 private:
  std::map<std::string, CommodityDef> defs_;
};

class CargoHold {
 public:
  CargoHold() = default;
  CargoHold(const CommodityDb* db, float capacityTons) : db_(db), capacity_(capacityTons) {}
  float capacityTons() const { return capacity_; }
  float usedTons() const;
  float freeTons() const { return capacity_ - usedTons(); }
  int units(const std::string& id) const;
  int maxAddable(const std::string& id) const;  // limited by free mass
  bool add(const std::string& id, int n);       // all-or-nothing
  bool remove(const std::string& id, int n);
  const std::map<std::string, int>& contents() const { return units_; }
  void setContents(std::map<std::string, int> c) { units_ = std::move(c); }

 private:
  const CommodityDb* db_ = nullptr;
  float capacity_ = 0;
  std::map<std::string, int> units_;
};

struct MarketEntryDef {
  std::string commodity;
  int stock = 0;            // initial stock
  int targetStock = 50;     // equilibrium the market drifts toward
  int maxStock = 200;
  float productionPerHour = 0;  // + produces, - consumes (units per world hour)
  float elasticity = 0.8f;
  float minMultiplier = 0.5f, maxMultiplier = 2.0f;
  bool buys = true;         // market buys from the player
  bool sells = true;        // market sells to the player
};

struct MarketDef {
  std::string id;
  std::string name;
  float spread = 0.08f;          // fraction between buy and sell prices
  double tickSeconds = 600.0;    // world seconds per market update
  float restockFraction = 0.05f; // per tick, fraction of (target - stock) recovered
  std::vector<MarketEntryDef> entries;
};

struct TradeResult {
  int units = 0;            // units actually traded
  std::int64_t credits = 0; // total paid (buy) or received (sell)
  std::string message;
};

class Market {
 public:
  Market() = default;
  Market(const MarketDef& def, double worldTime);

  const MarketDef& def() const { return def_; }
  int entryIndex(const std::string& commodity) const;  // -1 if not traded
  int stock(int i) const { return stock_.at(size_t(i)); }
  std::int64_t buyPrice(const CommodityDb& db, int i) const;   // price the player pays for 1 unit
  std::int64_t sellPrice(const CommodityDb& db, int i) const;  // price the player gets for 1 unit

  TradeResult buy(const CommodityDb& db, int i, int units, std::int64_t& credits, CargoHold& cargo);
  TradeResult sell(const CommodityDb& db, int i, int units, std::int64_t& credits, CargoHold& cargo);

  void advanceTo(double worldTime);  // applies whole ticks since the last update
  double lastTick() const { return lastTick_; }

  // Persistence.
  const std::vector<int>& stocks() const { return stock_; }
  const std::vector<double>& carry() const { return carry_; }
  void restore(const std::vector<int>& stocks, double lastTick, const std::vector<double>& carry = {});

 private:
  float midPrice(const CommodityDb& db, int i, int stock) const;

  MarketDef def_;
  std::vector<int> stock_;
  std::vector<double> carry_;  // fractional production carried between ticks
  double lastTick_ = 0;
};

std::optional<CommodityDb> loadCommodities(const AssetManifest& manifest, const AssetID& id,
                                           std::vector<std::string>* errors);
std::optional<MarketDef> loadMarketDef(const AssetManifest& manifest, const AssetID& id, const CommodityDb& db,
                                       std::vector<std::string>* errors);

}  // namespace fh
