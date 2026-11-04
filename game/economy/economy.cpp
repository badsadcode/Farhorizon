#include "game/economy/economy.h"

#include <algorithm>
#include <cmath>

#include "engine/core/file_io.h"
#include "game/content/json_util.h"

namespace fh {

const CommodityDef* CommodityDb::find(const std::string& id) const {
  auto it = defs_.find(id);
  return it == defs_.end() ? nullptr : &it->second;
}

float CargoHold::usedTons() const {
  float t = 0;
  for (const auto& [id, n] : units_) {
    const CommodityDef* c = db_ ? db_->find(id) : nullptr;
    t += float(n) * (c ? c->massTons : 1.0f);
  }
  return t;
}

int CargoHold::units(const std::string& id) const {
  auto it = units_.find(id);
  return it == units_.end() ? 0 : it->second;
}

int CargoHold::maxAddable(const std::string& id) const {
  const CommodityDef* c = db_ ? db_->find(id) : nullptr;
  const float m = c ? c->massTons : 1.0f;
  return int(std::floor((freeTons() + 1e-4f) / m));
}

bool CargoHold::add(const std::string& id, int n) {
  if (n <= 0 || maxAddable(id) < n) return false;
  units_[id] += n;
  return true;
}

bool CargoHold::remove(const std::string& id, int n) {
  auto it = units_.find(id);
  if (n <= 0 || it == units_.end() || it->second < n) return false;
  it->second -= n;
  if (it->second == 0) units_.erase(it);
  return true;
}

Market::Market(const MarketDef& def, double worldTime) : def_(def), lastTick_(worldTime) {
  for (const auto& e : def_.entries) stock_.push_back(std::clamp(e.stock, 0, e.maxStock));
  carry_.assign(stock_.size(), 0.0);
}

int Market::entryIndex(const std::string& commodity) const {
  for (size_t i = 0; i < def_.entries.size(); ++i)
    if (def_.entries[i].commodity == commodity) return int(i);
  return -1;
}

float Market::midPrice(const CommodityDb& db, int i, int stock) const {
  const MarketEntryDef& e = def_.entries.at(size_t(i));
  const CommodityDef* c = db.find(e.commodity);
  const float base = c ? c->basePrice : 1.0f;
  const float target = float(std::max(1, e.targetStock));
  const float scarcity = e.elasticity * (target - float(stock)) / target;
  return base * std::clamp(1.0f + scarcity, e.minMultiplier, e.maxMultiplier);
}

std::int64_t Market::buyPrice(const CommodityDb& db, int i) const {
  return std::max<std::int64_t>(1, std::llround(midPrice(db, i, stock_.at(size_t(i))) * (1.0f + def_.spread * 0.5f)));
}

std::int64_t Market::sellPrice(const CommodityDb& db, int i) const {
  return std::max<std::int64_t>(0, std::llround(midPrice(db, i, stock_.at(size_t(i))) * (1.0f - def_.spread * 0.5f)));
}

TradeResult Market::buy(const CommodityDb& db, int i, int units, std::int64_t& credits, CargoHold& cargo) {
  TradeResult r;
  if (i < 0 || size_t(i) >= stock_.size()) return {0, 0, "Not traded here"};
  const MarketEntryDef& e = def_.entries[size_t(i)];
  if (!e.sells) return {0, 0, "This station does not sell that"};
  for (int n = 0; n < units; ++n) {
    if (stock_[size_t(i)] <= 0) {
      r.message = "Out of stock";
      break;
    }
    const std::int64_t price = buyPrice(db, i);
    if (credits < price) {
      r.message = "Not enough credits";
      break;
    }
    if (!cargo.add(e.commodity, 1)) {
      r.message = "Cargo hold full";
      break;
    }
    credits -= price;
    --stock_[size_t(i)];
    r.credits += price;
    ++r.units;
  }
  return r;
}

TradeResult Market::sell(const CommodityDb& db, int i, int units, std::int64_t& credits, CargoHold& cargo) {
  TradeResult r;
  if (i < 0 || size_t(i) >= stock_.size()) return {0, 0, "Not traded here"};
  const MarketEntryDef& e = def_.entries[size_t(i)];
  if (!e.buys) return {0, 0, "This station does not buy that"};
  for (int n = 0; n < units; ++n) {
    if (cargo.units(e.commodity) <= 0) {
      if (r.units == 0) r.message = "Nothing to sell";
      break;
    }
    if (stock_[size_t(i)] >= e.maxStock) {
      r.message = "Market saturated";
      break;
    }
    const std::int64_t price = sellPrice(db, i);
    cargo.remove(e.commodity, 1);
    credits += price;
    ++stock_[size_t(i)];
    r.credits += price;
    ++r.units;
  }
  return r;
}

void Market::advanceTo(double worldTime) {
  if (def_.tickSeconds <= 0 || worldTime <= lastTick_) return;
  const auto ticks = static_cast<long long>(std::floor((worldTime - lastTick_) / def_.tickSeconds));
  const long long applied = std::min<long long>(ticks, 10000);  // long absences converge anyway
  for (long long t = 0; t < applied; ++t) {
    for (size_t i = 0; i < stock_.size(); ++i) {
      const MarketEntryDef& e = def_.entries[i];
      double s = double(stock_[i]) + carry_[i];
      s += double(e.productionPerHour) * def_.tickSeconds / 3600.0;
      s += (double(e.targetStock) - s) * double(def_.restockFraction);
      s = std::clamp(s, 0.0, double(e.maxStock));
      stock_[i] = int(std::floor(s));
      carry_[i] = s - double(stock_[i]);
    }
  }
  lastTick_ += double(ticks) * def_.tickSeconds;
}

void Market::restore(const std::vector<int>& stocks, double lastTick, const std::vector<double>& carry) {
  for (size_t i = 0; i < stock_.size() && i < stocks.size(); ++i)
    stock_[i] = std::clamp(stocks[i], 0, def_.entries[i].maxStock);
  if (carry.size() == stock_.size()) carry_ = carry;
  lastTick_ = lastTick;
}

std::optional<CommodityDb> loadCommodities(const AssetManifest& manifest, const AssetID& id,
                                           std::vector<std::string>* errors) {
  auto path = manifest.resolve(id, "commodities");
  auto text = path ? readTextFile(*path) : std::nullopt;
  content::Reader r("commodities '" + id + "'", errors);
  if (!text) {
    r.error("missing or unreadable");
    return std::nullopt;
  }
  auto j = nlohmann::json::parse(*text, nullptr, false);
  if (j.is_discarded() || !j.contains("commodities") || !j["commodities"].is_array()) {
    r.error("invalid JSON or missing 'commodities' array");
    return std::nullopt;
  }
  CommodityDb db;
  for (const auto& cj : j["commodities"]) {
    CommodityDef c;
    c.id = r.str(cj, "id");
    c.name = r.str(cj, "name");
    c.category = r.str(cj, "category", false, "goods");
    c.basePrice = float(r.num(cj, "basePrice"));
    c.massTons = float(r.num(cj, "massTons", false, 1.0));
    c.legal = cj.value("legal", true);
    c.description = r.str(cj, "description", false);
    if (c.basePrice <= 0 || c.massTons <= 0) r.error("commodity '" + c.id + "': basePrice and massTons must be > 0");
    if (db.find(c.id)) r.error("duplicate commodity '" + c.id + "'");
    db.add(c);
  }
  if (!r.ok()) return std::nullopt;
  return db;
}

std::optional<MarketDef> loadMarketDef(const AssetManifest& manifest, const AssetID& id, const CommodityDb& db,
                                       std::vector<std::string>* errors) {
  auto path = manifest.resolve(id, "market");
  auto text = path ? readTextFile(*path) : std::nullopt;
  content::Reader r("market '" + id + "'", errors);
  if (!text) {
    r.error("missing or unreadable");
    return std::nullopt;
  }
  auto j = nlohmann::json::parse(*text, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    r.error("invalid JSON");
    return std::nullopt;
  }
  MarketDef m;
  m.id = r.str(j, "id");
  m.name = r.str(j, "name");
  m.spread = float(r.num(j, "spread", false, m.spread));
  m.tickSeconds = r.num(j, "tickSeconds", false, m.tickSeconds);
  m.restockFraction = float(r.num(j, "restockFraction", false, m.restockFraction));
  if (m.spread < 0.02f || m.spread > 0.8f) r.error("spread must be within [0.02, 0.8] (anti-exploit floor)");
  if (j.contains("entries") && j["entries"].is_array())
    for (const auto& ej : j["entries"]) {
      MarketEntryDef e;
      e.commodity = r.str(ej, "commodity");
      e.stock = int(r.num(ej, "stock", false, e.stock));
      e.targetStock = int(r.num(ej, "targetStock", false, e.targetStock));
      e.maxStock = int(r.num(ej, "maxStock", false, e.maxStock));
      e.productionPerHour = float(r.num(ej, "productionPerHour", false, e.productionPerHour));
      e.elasticity = float(r.num(ej, "elasticity", false, e.elasticity));
      e.minMultiplier = float(r.num(ej, "minMultiplier", false, e.minMultiplier));
      e.maxMultiplier = float(r.num(ej, "maxMultiplier", false, e.maxMultiplier));
      e.buys = ej.value("buys", true);
      e.sells = ej.value("sells", true);
      if (!db.find(e.commodity)) r.error("unknown commodity '" + e.commodity + "'");
      if (e.maxStock <= 0 || e.targetStock <= 0 || e.minMultiplier <= 0 || e.maxMultiplier < e.minMultiplier)
        r.error("entry '" + e.commodity + "': bad stock or multiplier bounds");
      m.entries.push_back(e);
    }
  if (m.entries.empty()) r.error("market has no entries");
  if (!r.ok()) return std::nullopt;
  return m;
}

}  // namespace fh
