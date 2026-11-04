#include "game/economy/economy.h"
#include "test_framework.h"

using namespace fh;

namespace {
CommodityDb db() {
  CommodityDb d;
  d.add({"ore.iron", "Iron", "ore", 30, 1, true, ""});
  d.add({"goods.heavy", "Heavy", "goods", 100, 2, true, ""});
  return d;
}
MarketDef marketDef() {
  MarketDef m;
  m.id = "m";
  m.spread = 0.1f;
  m.tickSeconds = 600;
  m.restockFraction = 0.1f;
  MarketEntryDef iron;
  iron.commodity = "ore.iron";
  iron.stock = 50;
  iron.targetStock = 100;
  iron.maxStock = 200;
  iron.productionPerHour = -12;
  MarketEntryDef heavy;
  heavy.commodity = "goods.heavy";
  heavy.stock = 20;
  heavy.targetStock = 20;
  heavy.maxStock = 40;
  heavy.productionPerHour = 6;
  m.entries = {iron, heavy};
  return m;
}
}  // namespace

TEST(cargo_respects_mass_capacity) {
  const CommodityDb d = db();
  CargoHold hold(&d, 10);
  CHECK(hold.add("goods.heavy", 4));     // 8 t
  CHECK(!hold.add("goods.heavy", 2));    // would be 12 t
  CHECK(hold.maxAddable("ore.iron") == 2);
  CHECK(hold.add("ore.iron", 2));
  CHECK_NEAR(hold.usedTons(), 10.0, 1e-5);
  CHECK(!hold.remove("ore.iron", 3));
  CHECK(hold.remove("ore.iron", 2));
  CHECK(hold.units("ore.iron") == 0);
}

TEST(market_prices_follow_scarcity_and_stay_bounded) {
  const CommodityDb d = db();
  Market m(marketDef(), 0);
  const int i = m.entryIndex("ore.iron");
  CHECK(m.buyPrice(d, i) > m.sellPrice(d, i));      // spread
  std::int64_t credits = 0;
  CargoHold hold(&d, 1000);
  hold.add("ore.iron", 150);
  const std::int64_t before = m.sellPrice(d, i);
  CHECK(m.sell(d, i, 50, credits, hold).units == 50);
  CHECK(m.sellPrice(d, i) < before);                // selling floods the market
  m.sell(d, i, 100, credits, hold);
  CHECK(m.stock(i) <= 200);
  CHECK(m.sellPrice(d, i) >= std::llround(30 * 0.5 * 0.95) - 1);  // clamped at minMultiplier
}

TEST(market_round_trip_never_makes_money) {
  const CommodityDb d = db();
  Market m(marketDef(), 0);
  const int i = m.entryIndex("goods.heavy");
  std::int64_t credits = 10000;
  CargoHold hold(&d, 100);
  for (int n : {1, 5, 15}) {
    const std::int64_t start = credits;
    const TradeResult b = m.buy(d, i, n, credits, hold);
    const TradeResult s = m.sell(d, i, b.units, credits, hold);
    CHECK(s.units == b.units);
    CHECK(credits < start);  // spread + slippage always lose money
  }
}

TEST(market_refuses_invalid_trades) {
  const CommodityDb d = db();
  Market m(marketDef(), 0);
  const int i = m.entryIndex("goods.heavy");
  std::int64_t credits = 50;  // cannot afford a single unit
  CargoHold hold(&d, 100);
  CHECK(m.buy(d, i, 1, credits, hold).units == 0);
  CHECK(credits == 50);
  credits = 100000;
  CargoHold tiny(&d, 3);  // 1 unit of 2 t fits, the second does not
  CHECK(m.buy(d, i, 5, credits, tiny).units == 1);
  CHECK(m.sell(d, m.entryIndex("ore.iron"), 1, credits, tiny).units == 0);  // nothing to sell
  CHECK(m.entryIndex("does.not.exist") == -1);
}

TEST(market_ticks_are_deterministic_and_bounded) {
  Market a(marketDef(), 0), b(marketDef(), 0);
  a.advanceTo(3600 * 5);
  for (int k = 1; k <= 30; ++k) b.advanceTo(600.0 * k);  // same time reached in small steps
  CHECK(a.stock(0) == b.stock(0));
  CHECK(a.stock(1) == b.stock(1));
  CHECK_NEAR(a.lastTick(), 18000.0, 1e-6);
  a.advanceTo(1e9);  // long absence converges, stays in bounds
  CHECK(a.stock(0) >= 0 && a.stock(0) <= 200);
  CHECK(a.stock(1) >= 0 && a.stock(1) <= 40);
}
