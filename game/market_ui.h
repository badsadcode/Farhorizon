#pragma once
// Station / outpost terminal: a compact CRT-style panel (design doc 11 "station services") with tabs
//   MARKET    - buy/sell commodities,
//   SERVICES  - hull repair and shipyard upgrades,
//   CONTRACTS - today's job board and the jobs you hold.
// Keyboard: Tab next tab, Up/Down select, Right/= buy, Left/- sell (Shift = x10), Home sell all,
// Enter accept/repair/fit, Delete abandon, Esc/M close. Mouse: click tabs, rows and buttons.
#include <string>
#include <vector>

#include "engine/debug/overlay.h"
#include "game/content/visual_profile.h"
#include "game/session/game_session.h"

namespace fh {

class InputState;

class MarketPanel {
 public:
  enum class Tab { Market, Services, Contracts };

  bool isOpen() const { return open_; }
  void open(const GameSession& session);
  void close() { open_ = false; }
  Tab tab() const { return tab_; }
  // Terminal available at the current location (any tab has something to show).
  static bool available(const GameSession& session);

  // Handles input while open. Mouse position in drawable pixels.
  void handleInput(const InputState& in, GameSession& session, float mouseX, float mouseY, float width, float height);
  void draw(OverlayBuilder& o, GameSession& session, const HudPalette& palette, float width, float height,
            double time) const;

 private:
  struct Layout {
    float x, y, w, rowH, scale, tabY, tableY;
    float buyX, sellX, allX, buttonW;
  };
  static Layout layout(float width, float height);
  std::vector<Tab> tabs(const GameSession& session) const;
  int rowCount(GameSession& session) const;
  void doTrade(GameSession& session, int units);
  void activate(GameSession& session);   // Enter on the selected row (services / contracts)
  void abandon(GameSession& session);

  bool open_ = false;
  Tab tab_ = Tab::Market;
  int selected_ = 0;
  std::string message_;
  bool messageGood_ = true;
};

}  // namespace fh
