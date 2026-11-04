#include "game/market_ui.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "engine/platform/input.h"

namespace fh {

namespace {
OverlayColor col(const Vec4& c, float alpha = 1.0f) {
  auto b = [](float v) { return std::uint8_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
  return {b(c.x), b(c.y), b(c.z), b(c.w * alpha)};
}
std::string credits(std::int64_t c) {
  std::string s = std::to_string(c < 0 ? -c : c);
  for (int i = int(s.size()) - 3; i > 0; i -= 3) s.insert(size_t(i), ",");
  return (c < 0 ? "-" : "") + s + " cr";
}
const char* tabName(MarketPanel::Tab t) {
  switch (t) {
    case MarketPanel::Tab::Market: return "MARKET";
    case MarketPanel::Tab::Services: return "SERVICES";
    case MarketPanel::Tab::Contracts: return "CONTRACTS";
  }
  return "?";
}
std::string clip(const std::string& s, size_t n) { return s.size() <= n ? s : s.substr(0, n - 3) + "..."; }
}  // namespace

bool MarketPanel::available(const GameSession& session) {
  return const_cast<GameSession&>(session).dockedMarket() || session.hasService("repair") ||
         session.hasService("upgrades") || session.hasService("contracts");
}

void MarketPanel::open(const GameSession& session) {
  open_ = true;
  message_.clear();
  selected_ = 0;
  const auto t = tabs(session);
  if (std::find(t.begin(), t.end(), tab_) == t.end()) tab_ = t.empty() ? Tab::Market : t.front();
}

std::vector<MarketPanel::Tab> MarketPanel::tabs(const GameSession& session) const {
  std::vector<Tab> t;
  if (const_cast<GameSession&>(session).dockedMarket()) t.push_back(Tab::Market);
  if (session.hasService("repair") || session.hasService("upgrades")) t.push_back(Tab::Services);
  if (session.hasService("contracts")) t.push_back(Tab::Contracts);
  return t;
}

MarketPanel::Layout MarketPanel::layout(float width, float height) {
  Layout l;
  l.scale = width >= 2400 ? 3.0f : 2.0f;
  l.w = std::min(width - 40.0f, 560.0f * l.scale);
  l.x = (width - l.w) * 0.5f;
  l.y = std::max(20.0f, height * 0.10f);
  l.rowH = 14.0f * l.scale;
  l.tabY = l.y + 34.0f * l.scale;
  l.tableY = l.y + 56.0f * l.scale;
  l.buttonW = 40.0f * l.scale;
  l.allX = l.x + l.w - l.buttonW - 8.0f * l.scale;
  l.sellX = l.allX - l.buttonW - 4.0f * l.scale;
  l.buyX = l.sellX - l.buttonW - 4.0f * l.scale;
  return l;
}

int MarketPanel::rowCount(GameSession& session) const {
  switch (tab_) {
    case Tab::Market: {
      Market* m = session.dockedMarket();
      return m ? int(m->def().entries.size()) : 0;
    }
    case Tab::Services: {
      int n = session.hasService("repair") ? 1 : 0;
      if (session.hasService("upgrades") && session.upgradeDb()) n += int(session.upgradeDb()->all().size());
      return n;
    }
    case Tab::Contracts: return int(session.contractOffers().size() + session.contracts().active().size());
  }
  return 0;
}

void MarketPanel::doTrade(GameSession& session, int units) {
  Market* m = session.dockedMarket();
  if (!m || selected_ < 0 || selected_ >= int(m->def().entries.size())) return;
  const std::string& id = m->def().entries[size_t(selected_)].commodity;
  const CommodityDef* c = session.commodities().find(id);
  const TradeResult r = session.trade(id, units);
  const std::string name = c ? c->name : id;
  messageGood_ = r.units > 0;
  if (r.units > 0)
    message_ = std::format("{} {} x {} for {}", units > 0 ? "Bought" : "Sold", r.units, name, credits(r.credits));
  else
    message_ = r.message.empty() ? "No trade" : r.message;
}

void MarketPanel::activate(GameSession& session) {
  ActionResult r{false, ""};
  if (tab_ == Tab::Services) {
    int row = selected_;
    if (session.hasService("repair")) {
      if (row == 0) r = session.repair();
      --row;
    }
    if (row >= 0 && session.upgradeDb() && row < int(session.upgradeDb()->all().size()))
      r = session.buyUpgrade(session.upgradeDb()->all()[size_t(row)].id);
  } else if (tab_ == Tab::Contracts) {
    const auto offers = session.contractOffers();
    if (selected_ < int(offers.size())) r = session.acceptContract(offers[size_t(selected_)]);
    else r = {false, "Already yours. DEL abandons it."};
  } else {
    return;
  }
  if (r.message.empty()) return;
  message_ = r.message;
  messageGood_ = r.ok;
}

void MarketPanel::abandon(GameSession& session) {
  if (tab_ != Tab::Contracts) return;
  const int i = selected_ - int(session.contractOffers().size());
  if (i < 0) return;
  const ActionResult r = session.abandonContract(size_t(i));
  message_ = r.message;
  messageGood_ = false;
}

void MarketPanel::handleInput(const InputState& in, GameSession& session, float mx, float my, float width,
                              float height) {
  const auto ts = tabs(session);
  if (ts.empty() || in.pressed("ui.close") || in.pressed("ui.market")) {
    close();
    return;
  }
  const Layout l = layout(width, height);
  auto setTab = [&](Tab t) {
    if (t == tab_) return;
    tab_ = t;
    selected_ = 0;
    message_.clear();
  };
  if (in.pressed("ui.tabNext")) {
    auto it = std::find(ts.begin(), ts.end(), tab_);
    setTab(it == ts.end() || std::next(it) == ts.end() ? ts.front() : *std::next(it));
  }
  if (std::find(ts.begin(), ts.end(), tab_) == ts.end()) setTab(ts.front());
  if (in.mouseLeftPressed() && my >= l.tabY - 2 && my <= l.tabY + l.rowH) {
    float tx = l.x + 8.0f * l.scale;
    for (Tab t : ts) {
      const float w = OverlayBuilder::textWidth(std::string("[") + tabName(t) + "]", l.scale);
      if (mx >= tx && mx <= tx + w) setTab(t);
      tx += w + 12.0f * l.scale;
    }
  }
  const int rows = rowCount(session);
  if (rows > 0) {
    selected_ = std::clamp(selected_, 0, rows - 1);
    if (in.pressed("ui.up")) selected_ = (selected_ + rows - 1) % rows;
    if (in.pressed("ui.down")) selected_ = (selected_ + 1) % rows;
  }
  if (tab_ == Tab::Market) {
    Market* m = session.dockedMarket();
    if (!m) return;
    const int mult = in.down("ui.modifier") ? 10 : 1;
    if (in.pressed("ui.buy")) doTrade(session, mult);
    if (in.pressed("ui.sell")) doTrade(session, -mult);
    if (in.pressed("ui.sellAll") && rows > 0) {
      const std::string& id = m->def().entries[size_t(selected_)].commodity;
      if (const int have = session.cargo().units(id); have > 0) doTrade(session, -have);
      else message_ = "Nothing to sell";
    }
  } else {
    if (in.pressed("ui.accept") || in.pressed("ui.buy")) activate(session);
    if (in.pressed("ui.abandon")) abandon(session);
  }
  if (in.mouseLeftPressed()) {
    for (int r = 0; r < rows; ++r) {
      const float y = l.tableY + l.rowH * float(r + 1);
      if (my < y - 2 || my > y + l.rowH - 2 || mx < l.x || mx > l.x + l.w) continue;
      const bool again = selected_ == r;
      selected_ = r;
      if (tab_ == Tab::Market) {
        const int mult = in.down("ui.modifier") ? 10 : 1;
        if (mx >= l.buyX && mx <= l.buyX + l.buttonW) doTrade(session, mult);
        else if (mx >= l.sellX && mx <= l.sellX + l.buttonW) doTrade(session, -mult);
        else if (mx >= l.allX && mx <= l.allX + l.buttonW) {
          const std::string& id = session.dockedMarket()->def().entries[size_t(r)].commodity;
          if (const int have = session.cargo().units(id); have > 0) doTrade(session, -have);
        }
      } else if (again || mx >= l.allX) {
        activate(session);  // click a selected row (or its action column) to act
      }
    }
  }
}

void MarketPanel::draw(OverlayBuilder& o, GameSession& session, const HudPalette& pal, float width, float height,
                       double time) const {
  const auto ts = tabs(session);
  if (ts.empty()) return;
  const Layout l = layout(width, height);
  const OverlayColor fg = col(pal.primary), dim = col(pal.primary, 0.55f), nav = col(pal.nav), warn = col(pal.warn);
  const int rows = std::max(rowCount(session), 1);
  const float h = (56.0f + 14.0f * float(rows + 1) + 58.0f) * l.scale;
  // Terminal frame with a faint phosphor wash and scan bars.
  o.rect(0, 0, width, height, {0, 0, 0, 120});
  o.rect(l.x, l.y, l.w, h, {4, 8, 6, 248});
  for (float sy = l.y; sy < l.y + h; sy += 3.0f * l.scale) o.rect(l.x, sy, l.w, 1.0f, {0, 0, 0, 60});
  o.line(l.x, l.y, l.x + l.w, l.y, 2, fg);
  o.line(l.x, l.y + h, l.x + l.w, l.y + h, 2, fg);
  o.line(l.x, l.y, l.x, l.y + h, 2, fg);
  o.line(l.x + l.w, l.y, l.x + l.w, l.y + h, 2, fg);
  const float pad = 8.0f * l.scale;
  Market* m = session.dockedMarket();
  const std::string title =
      "> " + (m ? m->def().name : locationDisplayName(session.contractLocations(), session.currentLocationId()));
  o.text(l.x + pad, l.y + pad, title, fg, l.scale);
  const bool blink = std::fmod(time, 1.0) < 0.5;
  o.text(l.x + pad + OverlayBuilder::textWidth(title, l.scale) + 4, l.y + pad, blink ? "_" : " ", fg, l.scale);
  const Durability& dur = session.combat().player();
  o.text(l.x + pad, l.y + pad + 12 * l.scale,
         std::format("CREDITS {}    HOLD {:.0f}/{:.0f} t    HULL {:.0f}/{:.0f}", credits(session.credits()),
                     session.cargo().usedTons(), session.cargo().capacityTons(), dur.hull, dur.maxHull),
         nav, l.scale);
  // Tabs.
  float tx = l.x + pad;
  for (Tab t : ts) {
    const std::string label = std::string("[") + tabName(t) + "]";
    const float w = OverlayBuilder::textWidth(label, l.scale);
    if (t == tab_) o.rect(tx - 2, l.tabY - 3, w + 4, l.rowH, col(pal.primary, 0.25f));
    o.text(tx, l.tabY, label, t == tab_ ? fg : dim, l.scale);
    tx += w + 12.0f * l.scale;
  }
  o.line(l.x + pad, l.tabY + l.rowH, l.x + l.w - pad, l.tabY + l.rowH, 1, dim);

  std::string description;
  auto rowY = [&](int r) { return l.tableY + l.rowH * float(r + 1); };
  auto highlight = [&](int r) {
    if (r == selected_) o.rect(l.x + 2, rowY(r) - 3, l.w - 4, l.rowH, col(pal.primary, 0.18f));
    return r == selected_ ? fg : col(pal.primary, 0.8f);
  };
  std::string help;

  if (tab_ == Tab::Market && m) {
    const float cName = l.x + pad, cStock = l.x + l.w * 0.34f, cBuy = l.x + l.w * 0.45f, cSell = l.x + l.w * 0.55f,
                cHold = l.x + l.w * 0.65f;
    o.text(cName, l.tableY, "COMMODITY", dim, l.scale);
    o.text(cStock, l.tableY, "STOCK", dim, l.scale);
    o.text(cBuy, l.tableY, "BUY", dim, l.scale);
    o.text(cSell, l.tableY, "SELL", dim, l.scale);
    o.text(cHold, l.tableY, "HOLD", dim, l.scale);
    const CommodityDb& db = session.commodities();
    const int n = int(m->def().entries.size());
    for (int r = 0; r < n; ++r) {
      const MarketEntryDef& e = m->def().entries[size_t(r)];
      const CommodityDef* c = db.find(e.commodity);
      const float y = rowY(r);
      const bool sel = r == selected_;
      const OverlayColor rc = highlight(r);
      o.text(cName, y, (sel ? "> " : "  ") + (c ? c->name : e.commodity), rc, l.scale);
      o.text(cStock, y, std::to_string(m->stock(r)), rc, l.scale);
      o.text(cBuy, y, e.sells ? std::to_string(m->buyPrice(db, r)) : std::string("--"), rc, l.scale);
      o.text(cSell, y, e.buys ? std::to_string(m->sellPrice(db, r)) : std::string("--"), rc, l.scale);
      const int have = session.cargo().units(e.commodity);
      o.text(cHold, y, have ? std::to_string(have) : std::string("."), have ? nav : dim, l.scale);
      auto button = [&](float bx, const char* label, bool enabled) {
        const OverlayColor bc = enabled ? (sel ? fg : dim) : col(pal.primary, 0.2f);
        o.line(bx, y - 2, bx + l.buttonW, y - 2, 1, bc);
        o.line(bx, y + l.rowH - 5, bx + l.buttonW, y + l.rowH - 5, 1, bc);
        o.text(bx + (l.buttonW - OverlayBuilder::textWidth(label, l.scale)) * 0.5f, y, label, bc, l.scale);
      };
      button(l.buyX, "BUY", e.sells);
      button(l.sellX, "SELL", e.buys && have > 0);
      button(l.allX, "ALL", e.buys && have > 0);
      if (sel && c) description = c->description;
    }
    help = "UP/DOWN select  RIGHT buy  LEFT sell  SHIFT x10  HOME sell all  TAB next  ESC close";
  } else if (tab_ == Tab::Services) {
    o.text(l.x + pad, l.tableY, "SERVICE", dim, l.scale);
    o.text(l.x + l.w * 0.62f, l.tableY, "PRICE", dim, l.scale);
    int r = 0;
    if (session.hasService("repair")) {
      const OverlayColor rc = highlight(r);
      const auto cost = session.repairCost();
      o.text(l.x + pad, rowY(r), std::string(r == selected_ ? "> " : "  ") + "Hull repair", rc, l.scale);
      o.text(l.x + l.w * 0.62f, rowY(r), cost > 0 ? credits(cost) : std::string("--"), rc, l.scale);
      o.text(l.x + l.w * 0.82f, rowY(r), cost > 0 ? "REPAIR" : "SOUND", cost > 0 ? rc : dim, l.scale);
      if (r == selected_)
        description = std::format("Patch the hull back to {:.0f} points. Shields recharge on their own.", dur.maxHull);
      ++r;
    }
    if (session.hasService("upgrades") && session.upgradeDb()) {
      for (const auto& u : session.upgradeDb()->all()) {
        const OverlayColor rc = highlight(r);
        const bool owned = session.ownsUpgrade(u.id);
        o.text(l.x + pad, rowY(r), (r == selected_ ? "> " : "  ") + u.name, owned ? dim : rc, l.scale);
        o.text(l.x + l.w * 0.62f, rowY(r), credits(u.price), owned ? dim : (session.credits() >= u.price ? rc : warn),
               l.scale);
        o.text(l.x + l.w * 0.82f, rowY(r), owned ? "FITTED" : "FIT", owned ? nav : rc, l.scale);
        if (r == selected_) description = u.description;
        ++r;
      }
    } else if (r == 1) {
      o.text(l.x + pad, rowY(1), "  No shipyard at this outpost.", dim, l.scale);
    }
    help = "UP/DOWN select  ENTER repair / fit  TAB next  ESC close";
  } else if (tab_ == Tab::Contracts) {
    const auto offers = session.contractOffers();
    const auto& active = session.contracts().active();
    o.text(l.x + pad, l.tableY, "JOB", dim, l.scale);
    o.text(l.x + l.w * 0.70f, l.tableY, "PAY", dim, l.scale);
    int r = 0;
    for (const auto& c : offers) {
      const OverlayColor rc = highlight(r);
      o.text(l.x + pad, rowY(r), (r == selected_ ? "> " : "  ") + clip(c.title, 42), rc, l.scale);
      o.text(l.x + l.w * 0.70f, rowY(r), credits(c.reward), rc, l.scale);
      o.text(l.x + l.w * 0.86f, rowY(r), "TAKE", rc, l.scale);
      if (r == selected_) description = c.description;
      ++r;
    }
    if (offers.empty()) o.text(l.x + pad, rowY(-1) + l.rowH, " ", dim, l.scale);
    for (const auto& c : active) {
      const OverlayColor rc = highlight(r);
      std::string status;
      switch (c.kind) {
        case ContractKind::Courier: status = "DELIVER"; break;
        case ContractKind::Procurement:
          status = std::format("{}/{}", std::min(session.cargo().units(c.commodity), c.units), c.units);
          break;
        case ContractKind::Bounty: status = c.ready() ? "REPORT" : std::format("{}/{}", c.kills, c.killsRequired); break;
      }
      o.text(l.x + pad, rowY(r), (r == selected_ ? "> * " : "  * ") + clip(c.title, 40), c.ready() ? nav : rc, l.scale);
      o.text(l.x + l.w * 0.70f, rowY(r), credits(c.reward), rc, l.scale);
      o.text(l.x + l.w * 0.86f, rowY(r), status, nav, l.scale);
      if (r == selected_) {
        description = c.description;
        if (c.kind == ContractKind::Courier)
          description += "  Deliver to " + locationDisplayName(session.contractLocations(), c.destination) + ".";
        else
          description += "  Report to " + locationDisplayName(session.contractLocations(), c.issuer) + ".";
      }
      ++r;
    }
    if (r == 0) o.text(l.x + pad, rowY(0), "  The board is empty. Come back tomorrow.", dim, l.scale);
    help = std::format("ENTER take job  DEL abandon (* = yours, max {})  TAB next  ESC close",
                       session.contractDb() ? session.contractDb()->maxActive : 3);
  }

  const float fy = l.tableY + l.rowH * float(rows + 1) + 8 * l.scale;
  o.text(l.x + pad, fy, clip(description, 96), dim, l.scale * 0.85f);
  o.text(l.x + pad, fy + 14 * l.scale, message_.empty() ? std::string(" ") : message_, messageGood_ ? nav : warn,
         l.scale);
  o.text(l.x + pad, fy + 28 * l.scale, help, dim, l.scale * 0.8f);
}

}  // namespace fh
