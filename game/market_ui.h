#pragma once
// Station market terminal: a compact CRT-style panel (design doc 11 "station services").
// Keyboard: Up/Down select, Right/= buy, Left/- sell (Shift = x10), Home sell all, Esc/M close.
// Mouse: click a row to select, click [BUY]/[SELL]/[ALL] buttons.
#include <string>

#include "engine/debug/overlay.h"
#include "game/content/visual_profile.h"
#include "game/session/game_session.h"

namespace fh {

class InputState;

class MarketPanel {
 public:
  bool isOpen() const { return open_; }
  void open() {
    open_ = true;
    message_.clear();
  }
  void close() { open_ = false; }

  // Handles input while open. Mouse position in drawable pixels.
  void handleInput(const InputState& in, GameSession& session, float mouseX, float mouseY, float width, float height);
  void draw(OverlayBuilder& o, GameSession& session, const HudPalette& palette, float width, float height,
            double time) const;

 private:
  struct Layout {
    float x, y, w, rowH, scale, tableY;
    float buyX, sellX, allX, buttonW;
  };
  static Layout layout(float width, float height);
  void doTrade(GameSession& session, int units);

  bool open_ = false;
  int selected_ = 0;
  std::string message_;
};

}  // namespace fh
