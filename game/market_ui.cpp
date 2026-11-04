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
}  // namespace

MarketPanel::Layout MarketPanel::layout(float width, float height) {
  Layout l;
  l.scale = width >= 2400 ? 3.0f : 2.0f;
  l.w = std::min(width - 40.0f, 520.0f * l.scale);
  l.x = (width - l.w) * 0.5f;
  l.y = std::max(20.0f, height * 0.12f);
  l.rowH = 14.0f * l.scale;
  l.tableY = l.y + 52.0f * l.scale;
  l.buttonW = 40.0f * l.scale;
  l.allX = l.x + l.w - l.buttonW - 8.0f * l.scale;
  l.sellX = l.allX - l.buttonW - 4.0f * l.scale;
  l.buyX = l.sellX - l.buttonW - 4.0f * l.scale;
  return l;
}

void MarketPanel::doTrade(GameSession& session, int units) {
  Market* m = session.dockedMarket();
  if (!m || selected_ < 0 || selected_ >= int(m->def().entries.size())) return;
  const std::string& id = m->def().entries[size_t(selected_)].commodity;
  const CommodityDef* c = session.commodities().find(id);
  const TradeResult r = session.trade(id, units);
  const std::string name = c ? c->name : id;
  if (r.units > 0)
    message_ = std::format("{} {} x {} for {}", units > 0 ? "Bought" : "Sold", r.units, name, credits(r.credits));
  else
    message_ = r.message.empty() ? "No trade" : r.message;
}

void MarketPanel::handleInput(const InputState& in, GameSession& session, float mx, float my, float width,
                              float height) {
  Market* m = session.dockedMarket();
  if (!m) {
    close();
    return;
  }
  const int rows = int(m->def().entries.size());
  if (in.pressed("ui.close") || in.pressed("ui.market")) {
    close();
    return;
  }
  const int mult = in.down("ui.modifier") ? 10 : 1;
  if (in.pressed("ui.up")) selected_ = (selected_ + rows - 1) % rows;
  if (in.pressed("ui.down")) selected_ = (selected_ + 1) % rows;
  if (in.pressed("ui.buy")) doTrade(session, mult);
  if (in.pressed("ui.sell")) doTrade(session, -mult);
  if (in.pressed("ui.sellAll")) {
    const std::string& id = m->def().entries[size_t(selected_)].commodity;
    if (const int have = session.cargo().units(id); have > 0) doTrade(session, -have);
    else message_ = "Nothing to sell";
  }
  if (in.mouseLeftPressed()) {
    const Layout l = layout(width, height);
    for (int r = 0; r < rows; ++r) {
      const float y = l.tableY + l.rowH * float(r + 1);
      if (my < y - 2 || my > y + l.rowH - 2 || mx < l.x || mx > l.x + l.w) continue;
      selected_ = r;
      if (mx >= l.buyX && mx <= l.buyX + l.buttonW) doTrade(session, mult);
      else if (mx >= l.sellX && mx <= l.sellX + l.buttonW) doTrade(session, -mult);
      else if (mx >= l.allX && mx <= l.allX + l.buttonW) {
        const std::string& id = m->def().entries[size_t(r)].commodity;
        if (const int have = session.cargo().units(id); have > 0) doTrade(session, -have);
      }
    }
  }
}

void MarketPanel::draw(OverlayBuilder& o, GameSession& session, const HudPalette& pal, float width, float height,
                       double time) const {
  Market* m = session.dockedMarket();
  if (!m) return;
  const Layout l = layout(width, height);
  const OverlayColor fg = col(pal.primary), dim = col(pal.primary, 0.55f), nav = col(pal.nav), warn = col(pal.warn);
  const int rows = int(m->def().entries.size());
  const float h = (52.0f + 14.0f * float(rows + 1) + 46.0f) * l.scale;
  // Terminal frame with a faint phosphor wash and scan bars.
  o.rect(0, 0, width, height, {0, 0, 0, 120});
  o.rect(l.x, l.y, l.w, h, {4, 8, 6, 248});
  for (float sy = l.y; sy < l.y + h; sy += 3.0f * l.scale) o.rect(l.x, sy, l.w, 1.0f, {0, 0, 0, 60});
  o.line(l.x, l.y, l.x + l.w, l.y, 2, fg);
  o.line(l.x, l.y + h, l.x + l.w, l.y + h, 2, fg);
  o.line(l.x, l.y, l.x, l.y + h, 2, fg);
  o.line(l.x + l.w, l.y, l.x + l.w, l.y + h, 2, fg);
  const float pad = 8.0f * l.scale;
  o.text(l.x + pad, l.y + pad, "> " + m->def().name, fg, l.scale);
  const bool blink = std::fmod(time, 1.0) < 0.5;
  o.text(l.x + pad + OverlayBuilder::textWidth("> " + m->def().name, l.scale) + 4, l.y + pad, blink ? "_" : " ", fg,
         l.scale);
  o.text(l.x + pad, l.y + pad + 14 * l.scale,
         std::format("CREDITS {}    HOLD {:.0f} / {:.0f} t", credits(session.credits()), session.cargo().usedTons(),
                     session.cargo().capacityTons()),
         nav, l.scale);
  // Table header.
  const float cName = l.x + pad, cStock = l.x + l.w * 0.34f, cBuy = l.x + l.w * 0.45f, cSell = l.x + l.w * 0.55f,
              cHold = l.x + l.w * 0.65f;
  o.text(cName, l.tableY, "COMMODITY", dim, l.scale);
  o.text(cStock, l.tableY, "STOCK", dim, l.scale);
  o.text(cBuy, l.tableY, "BUY", dim, l.scale);
  o.text(cSell, l.tableY, "SELL", dim, l.scale);
  o.text(cHold, l.tableY, "HOLD", dim, l.scale);
  const CommodityDb& db = session.commodities();
  for (int r = 0; r < rows; ++r) {
    const MarketEntryDef& e = m->def().entries[size_t(r)];
    const CommodityDef* c = db.find(e.commodity);
    const float y = l.tableY + l.rowH * float(r + 1);
    const bool sel = r == selected_;
    if (sel) o.rect(l.x + 2, y - 3, l.w - 4, l.rowH, col(pal.primary, 0.18f));
    const OverlayColor rc = sel ? fg : col(pal.primary, 0.8f);
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
  }
  const float fy = l.tableY + l.rowH * float(rows + 1) + 8 * l.scale;
  if (const CommodityDef* c = db.find(m->def().entries[size_t(std::clamp(selected_, 0, rows - 1))].commodity))
    o.text(l.x + pad, fy, c->description, dim, l.scale * 0.85f);
  o.text(l.x + pad, fy + 14 * l.scale, message_.empty() ? std::string(" ") : message_,
         message_.find("Bought") == 0 || message_.find("Sold") == 0 ? nav : warn, l.scale);
  o.text(l.x + pad, fy + 28 * l.scale, "UP/DOWN select  RIGHT buy  LEFT sell  SHIFT x10  HOME sell all  ESC close",
         dim, l.scale * 0.8f);
}

}  // namespace fh
