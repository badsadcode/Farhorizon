#include "game/market_ui.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>

#include "engine/platform/input.h"

namespace fh {

namespace {
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
std::string keyOf(const InputState& in, const char* action) {
  const auto names = in.bindingNames(action);
  return names.empty() ? std::string("--") : names.front();
}

constexpr int kMaxVisibleRows = 8;
constexpr float kRowH = 40, kBtnH = 28;
}  // namespace

bool MarketPanel::available(const GameSession& session) {
  return const_cast<GameSession&>(session).dockedMarket() || session.hasService("repair") ||
         session.hasService("upgrades") || session.hasService("contracts");
}

void MarketPanel::open(const GameSession& session) {
  open_ = true;
  justOpened_ = true;
  message_.clear();
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

void MarketPanel::doTrade(GameSession& session, const std::string& commodity, int units) {
  const CommodityDef* c = session.commodities().find(commodity);
  const TradeResult r = session.trade(commodity, units);
  const std::string name = c ? c->name : commodity;
  messageGood_ = r.units > 0;
  if (r.units > 0)
    message_ = std::format("{} {} x {} for {}", units > 0 ? "Bought" : "Sold", r.units, name, credits(r.credits));
  else
    message_ = r.message.empty() ? "No trade" : r.message;
}

void MarketPanel::show(const ActionResult& r) {
  if (r.message.empty()) return;
  message_ = r.message;
  messageGood_ = r.ok;
}

void MarketPanel::update(Ui& ui, const InputState& in, GameSession& session) {
  const auto ts = tabs(session);
  const bool closeKey = !justOpened_ && (in.pressed("ui.close") || in.pressed("ui.market"));
  justOpened_ = false;
  if (ts.empty() || closeKey) {
    close();
    return;
  }
  if (std::find(ts.begin(), ts.end(), tab_) == ts.end()) tab_ = ts.front();

  const UiTheme& t = ui.theme();
  ui.backdrop(0.7f);
  const float W = ui.width() / ui.scale(), H = ui.height() / ui.scale();
  const float pw = std::min(1240.0f, W - 80);
  const float headerH = 156, footerH = 174;
  const int visible = std::clamp(int((H - 110 - headerH - footerH) / kRowH), 3, kMaxVisibleRows);
  const float ph = headerH + kRowH * float(visible) + footerH;
  const float px = (W - pw) * 0.5f, py = std::max(16.0f, (H - ph) * 0.5f - 14);
  const float pad = t.padding + 10, rx = px + pad, rw = pw - pad * 2;
  ui.panel(px, py, pw, ph);

  // ---- Header: where you are, what you have ----
  Market* market = session.dockedMarket();
  const std::string title =
      market ? market->def().name : locationDisplayName(session.contractLocations(), session.currentLocationId());
  const float titlePx = t.heading + 4;
  ui.label(rx, py + 20, title, titlePx, t.textBright, FontFace::Sans);
  if (std::fmod(ui.time(), 1.0) < 0.5)  // terminal cursor
    ui.label(rx + OverlayBuilder::measure(title, ui.s(titlePx), FontFace::Sans) / ui.scale() + 6, py + 20, "_", titlePx,
             t.text, FontFace::Sans);
  const Durability& dur = session.combat().player();
  const float statsX = px + pw - pad;
  ui.label(statsX, py + 18, credits(session.credits()), t.heading, t.textBright, FontFace::Mono, TextAlign::Right);
  ui.label(statsX, py + 54,
           std::format("HOLD {:.0f}/{:.0f} t    HULL {:.0f}/{:.0f}", session.cargo().usedTons(), session.cargo().capacityTons(),
                       dur.hull, dur.maxHull),
           t.small, t.text, FontFace::Mono, TextAlign::Right);

  // ---- Tabs ----
  std::vector<std::string> labels;
  int current = 0;
  for (size_t i = 0; i < ts.size(); ++i) {
    labels.push_back(tabName(ts[i]));
    if (ts[i] == tab_) current = int(i);
  }
  const int picked = ui.tabs(rx, py + 66, labels, current);
  if (picked != current) {
    tab_ = ts[size_t(picked)];
    ui.resetFocus();
    message_.clear();
  }

  // ---- Table ----
  const float headY = py + 120, rowsY = headY + 32;
  const float small = t.small;
  auto column = [&](float x, const char* name, TextAlign a = TextAlign::Left) {
    ui.label(x, headY, name, small, t.textDim, FontFace::Mono, a);
  };
  auto textY = [&](float y) { return y + (kRowH - OverlayBuilder::lineHeight(ui.s(t.body)) / ui.scale()) * 0.5f; };
  std::string description;
  int rowIndex = 0;
  int count = 0;
  float y = rowsY;
  int first = 0;
  auto beginList = [&](int rows) {
    count = rows;
    first = ui.scrollFor(0, rows, visible);
  };
  // Runs `draw(index, y)` for visible rows; rows scrolled away keep their focus index.
  auto rowVisible = [&](int i) {
    if (i < first || i >= first + visible) {
      ui.skipFocusable();
      return false;
    }
    return true;
  };

  const float btnY = (kRowH - kBtnH) * 0.5f;
  std::string help;
  if (tab_ == Tab::Market && market) {
    const float cStock = rx + rw * 0.36f, cBuy = rx + rw * 0.47f, cSell = rx + rw * 0.56f, cHold = rx + rw * 0.65f;
    const float bw = 76, bAll = rx + rw - bw - 8, bSell = bAll - bw - 8, bBuy = bSell - bw - 8;
    column(rx + 16, "COMMODITY");
    column(cStock, "STOCK", TextAlign::Right);
    column(cBuy, "BUY", TextAlign::Right);
    column(cSell, "SELL", TextAlign::Right);
    column(cHold, "HOLD", TextAlign::Right);
    const CommodityDb& db = session.commodities();
    const auto& entries = market->def().entries;
    beginList(int(entries.size()));
    const int mult = in.down("ui.modifier") ? 10 : 1;
    for (int r = 0; r < count; ++r) {
      if (!rowVisible(r)) continue;
      const MarketEntryDef& e = entries[size_t(r)];
      const CommodityDef* c = db.find(e.commodity);
      const Ui::RowState st = ui.row(rx, y, rw, kRowH);
      const OverlayColor fg = st.focused ? t.textBright : t.text;
      const float ty = textY(y);
      const int have = session.cargo().units(e.commodity);
      ui.label(rx + 16, ty, fitText(c ? c->name : e.commodity, ui.s(t.body), ui.s(rw * 0.28f)), t.body, fg);
      ui.label(cStock, ty, std::to_string(market->stock(r)), t.body, fg, FontFace::Mono, TextAlign::Right);
      ui.label(cBuy, ty, e.sells ? std::to_string(market->buyPrice(db, r)) : std::string("--"), t.body, e.sells ? fg : t.textDim,
               FontFace::Mono, TextAlign::Right);
      ui.label(cSell, ty, e.buys ? std::to_string(market->sellPrice(db, r)) : std::string("--"), t.body, e.buys ? fg : t.textDim,
               FontFace::Mono, TextAlign::Right);
      ui.label(cHold, ty, have ? std::to_string(have) : std::string("--"), t.body, have ? t.accent : t.textDim, FontFace::Mono,
               TextAlign::Right);
      const bool canSell = e.buys && have > 0;
      if (ui.inlineButton(bBuy, y + btnY, bw, kBtnH, mult > 1 ? "BUY 10" : "BUY", e.sells)) doTrade(session, e.commodity, mult);
      if (ui.inlineButton(bSell, y + btnY, bw, kBtnH, mult > 1 ? "SELL 10" : "SELL", canSell)) doTrade(session, e.commodity, -mult);
      if (ui.inlineButton(bAll, y + btnY, bw, kBtnH, "ALL", canSell)) doTrade(session, e.commodity, -have);
      if (st.focused) {
        if (c) description = c->description;
        if (in.pressed("ui.buy")) doTrade(session, e.commodity, mult);
        if (in.pressed("ui.sell")) doTrade(session, e.commodity, -mult);
        if (in.pressed("ui.sellAll")) {
          if (have > 0) doTrade(session, e.commodity, -have);
          else {
            message_ = "Nothing to sell";
            messageGood_ = false;
          }
        }
      }
      y += kRowH;
    }
    help = "UP/DOWN select    RIGHT buy    LEFT sell    SHIFT x10    HOME sell all    TAB next    ESC close";
  } else if (tab_ == Tab::Services) {
    const float cPrice = rx + rw * 0.76f, bw = 110, bx = rx + rw - bw - 8;
    column(rx + 16, "SERVICE");
    column(cPrice, "PRICE", TextAlign::Right);
    const bool repair = session.hasService("repair");
    const bool yard = session.hasService("upgrades") && session.upgradeDb();
    beginList((repair ? 1 : 0) + (yard ? int(session.upgradeDb()->all().size()) : 0));
    if (repair) {
      if (rowVisible(rowIndex)) {
        const auto cost = session.repairCost();
        const Ui::RowState st = ui.row(rx, y, rw, kRowH);
        const float ty = textY(y);
        const OverlayColor fg = st.focused ? t.textBright : t.text;
        ui.label(rx + 16, ty, "Hull repair", t.body, fg);
        ui.label(cPrice, ty, cost > 0 ? credits(cost) : std::string("--"), t.body, cost > session.credits() ? t.danger : fg,
                 FontFace::Mono, TextAlign::Right);
        const bool clicked = ui.inlineButton(bx, y + btnY, bw, kBtnH, cost > 0 ? "REPAIR" : "SOUND", cost > 0);
        if (clicked || (st.accepted && cost > 0)) show(session.repair());
        if (st.focused)
          description = std::format("Patch the hull back to {:.0f} points. Shields recharge on their own.", dur.maxHull);
        y += kRowH;
      }
      ++rowIndex;
    }
    if (yard) {
      for (const auto& u : session.upgradeDb()->all()) {
        if (rowVisible(rowIndex)) {
          const Ui::RowState st = ui.row(rx, y, rw, kRowH);
          const bool owned = session.ownsUpgrade(u.id);
          const float ty = textY(y);
          const OverlayColor fg = owned ? t.textDim : st.focused ? t.textBright : t.text;
          ui.label(rx + 16, ty, u.name, t.body, fg);
          ui.label(cPrice, ty, credits(u.price), t.body, owned ? t.textDim : session.credits() >= u.price ? fg : t.danger,
                   FontFace::Mono, TextAlign::Right);
          const bool clicked = ui.inlineButton(bx, y + btnY, bw, kBtnH, owned ? "FITTED" : "FIT", !owned);
          if (clicked || (st.accepted && !owned)) show(session.buyUpgrade(u.id));
          if (st.focused) description = u.description;
          y += kRowH;
        }
        ++rowIndex;
      }
    } else {
      ui.label(rx + 16, textY(y), "No shipyard at this outpost.", t.body, t.textDim);
    }
    help = "UP/DOWN select    ENTER repair / fit    TAB next    ESC close";
  } else if (tab_ == Tab::Contracts) {
    const auto offers = session.contractOffers();
    const auto& active = session.contracts().active();
    const float cPay = rx + rw * 0.74f, cState = rx + rw * 0.88f, bw = 110, bx = rx + rw - bw - 8;
    column(rx + 16, "JOB");
    column(cPay, "PAY", TextAlign::Right);
    beginList(int(offers.size() + active.size()));
    const float titleW = rw * 0.58f;
    for (const auto& c : offers) {
      if (rowVisible(rowIndex)) {
        const Ui::RowState st = ui.row(rx, y, rw, kRowH);
        const float ty = textY(y);
        const OverlayColor fg = st.focused ? t.textBright : t.text;
        ui.label(rx + 16, ty, fitText(c.title, ui.s(t.body), ui.s(titleW)), t.body, fg);
        ui.label(cPay, ty, credits(c.reward), t.body, fg, FontFace::Mono, TextAlign::Right);
        const bool clicked = ui.inlineButton(bx, y + btnY, bw, kBtnH, "TAKE");
        if (clicked || st.accepted) show(session.acceptContract(c));
        if (st.focused) description = c.description;
        y += kRowH;
      }
      ++rowIndex;
    }
    size_t heldIndex = 0;
    std::optional<size_t> toAbandon;
    for (const auto& c : active) {
      if (rowVisible(rowIndex)) {
        const Ui::RowState st = ui.row(rx, y, rw, kRowH);
        const float ty = textY(y);
        const OverlayColor fg = c.ready() ? t.ok : st.focused ? t.textBright : t.text;
        ui.label(rx + 16, ty, fitText("* " + c.title, ui.s(t.body), ui.s(titleW)), t.body, fg);
        ui.label(cPay, ty, credits(c.reward), t.body, fg, FontFace::Mono, TextAlign::Right);
        std::string status;
        switch (c.kind) {
          case ContractKind::Courier: status = "DELIVER"; break;
          case ContractKind::Procurement:
            status = std::format("{}/{}", std::min(session.cargo().units(c.commodity), c.units), c.units);
            break;
          case ContractKind::Bounty: status = c.ready() ? "REPORT" : std::format("{}/{}", c.kills, c.killsRequired); break;
        }
        ui.label(cState - 8, ty, status, t.small, t.accent, FontFace::Mono, TextAlign::Right);
        if (ui.inlineButton(bx, y + btnY, bw, kBtnH, "DROP")) toAbandon = heldIndex;
        if (st.accepted) {
          message_ = "Already yours. " + keyOf(in, "ui.abandon") + " abandons it.";
          messageGood_ = false;
        }
        if (st.focused) {
          if (in.pressed("ui.abandon")) toAbandon = heldIndex;
          const auto& locs = session.contractLocations();
          description = c.description;
          if (c.kind == ContractKind::Courier) description += "  Deliver to " + locationDisplayName(locs, c.destination) + ".";
          else description += "  Report to " + locationDisplayName(locs, c.issuer) + ".";
        }
        y += kRowH;
      }
      ++rowIndex;
      ++heldIndex;
    }
    if (toAbandon) {
      const ActionResult r = session.abandonContract(*toAbandon);
      message_ = r.message;
      messageGood_ = false;
    }
    if (count == 0) ui.label(rx + 16, textY(y), "The board is empty. Come back tomorrow.", t.body, t.textDim);
    help = std::format("UP/DOWN select    ENTER take job    {} abandon (* = yours, max {})    TAB next    ESC close",
                       keyOf(in, "ui.abandon"), session.contractDb() ? session.contractDb()->maxActive : 3);
  }
  if (count > visible) {  // scroll indicator
    const float sx = ui.s(px + pw - 22), sy0 = ui.s(rowsY), sh = ui.s(kRowH * float(visible));
    ui.draw().rect(sx, sy0, 2, sh, t.textDim.withAlpha(0.4f));
    ui.draw().rect(sx - 1, sy0 + sh * float(first) / float(count), 4, sh * float(visible) / float(count), t.text);
  }

  // ---- Footer: details, last result, close ----
  const float footY = rowsY + kRowH * float(visible) + 14;
  ui.textBlock(rx + 16, footY, rw - 32, description, t.small, t.text, 3);
  const float closeW = 170, closeH = 44, closeY = py + ph - closeH - 18;
  if (!message_.empty()) ui.label(rx + 16, closeY + 10, fitText(message_, ui.s(t.body), ui.s(rw - closeW - 50)), t.body, messageGood_ ? t.ok : t.danger);
  if (ui.button(px + pw - pad - closeW, closeY, closeW, closeH, "CLOSE")) close();
  ui.hintBar(help);
}

}  // namespace fh
