#pragma once
// Station / outpost terminal (design doc 11 "station services"), drawn with the Ui kit. Tabs:
//   MARKET    - buy/sell commodities,
//   SERVICES  - hull repair and shipyard upgrades,
//   CONTRACTS - today's job board and the jobs you hold.
// Keyboard: Tab next tab, Up/Down select, Right/= buy, Left/- sell (Shift = x10), Home sell all,
// Enter accept/repair/fit, Delete abandon, Esc or the terminal key close.
// Mouse: hover to select; each row has its own action buttons, so a click on a row never spends.
#include <string>
#include <vector>

#include "game/session/game_session.h"
#include "game/ui/ui.h"

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

  // Draws the terminal and handles its input. May close it (check isOpen() afterwards).
  void update(Ui& ui, const InputState& in, GameSession& session);

 private:
  std::vector<Tab> tabs(const GameSession& session) const;
  void doTrade(GameSession& session, const std::string& commodity, int units);
  void show(const ActionResult& r);

  bool open_ = false;
  bool justOpened_ = false;  // the key that opened the terminal must not also close it
  Tab tab_ = Tab::Market;
  std::string message_;
  bool messageGood_ = true;
};

}  // namespace fh
