#pragma once
// Small immediate-mode UI kit for menus (keyboard + mouse). Widgets are called every frame in a
// fixed order; each focusable widget gets the next focus index. Up/Down moves focus, Enter
// activates, Left/Right adjusts sliders and choices, the mouse hovers/clicks/drags.
// Layout is in 1080p "design pixels" scaled by `scale()`.
#include <string>
#include <string_view>
#include <vector>

#include "engine/debug/overlay.h"
#include "game/ui/ui_theme.h"

namespace fh {

struct UiInput {
  float mouseX = -1, mouseY = -1;  // drawable pixels
  bool mouseDown = false, mousePressed = false, mouseReleased = false, mouseMoved = false;
  float wheel = 0;
  bool up = false, down = false, left = false, right = false;
  bool accept = false, back = false, tabNext = false;
};

class Ui {
 public:
  void begin(OverlayBuilder& o, const UiTheme& theme, float width, float height, float userScale, const UiInput& in,
             double time);
  void end();
  void resetFocus(int index = 0) {
    focus_ = index;
    scroll_ = 0;
  }
  int focus() const { return focus_; }
  void setFocus(int i) { focus_ = i; }

  float scale() const { return scale_; }
  float s(float v) const { return v * scale_; }
  float width() const { return w_; }
  float height() const { return h_; }
  const UiTheme& theme() const { return *theme_; }
  OverlayBuilder& draw() { return *o_; }
  const UiInput& input() const { return in_; }
  double time() const { return time_; }

  // ---- Decoration ----
  void backdrop(float alpha = 1.0f);
  void leftShade(float widthFraction);  // dark gradient from the left edge (menu over a scene)
  void panel(float x, float y, float w, float h);
  void label(float x, float y, std::string_view text, float px, OverlayColor c, FontFace face = FontFace::Mono,
             TextAlign align = TextAlign::Left);
  // Wide-spaced title text (letter spacing from the theme).
  void spacedTitle(float x, float y, std::string_view text, float px, OverlayColor c);
  void hintBar(std::string_view text);  // bottom line of key hints

  // ---- Widgets (positions/sizes in design px; scaled internally) ----
  bool button(float x, float y, float w, float h, std::string_view label, bool enabled = true);
  // Menu entry: text with a bracket marker when focused (main/pause menus).
  bool menuItem(float x, float y, float w, std::string_view label, bool enabled = true);
  bool sliderRow(float x, float y, float w, std::string_view label, float& value, float lo, float hi, float step,
                 std::string_view valueText);
  bool toggleRow(float x, float y, float w, std::string_view label, bool& value);
  bool choiceRow(float x, float y, float w, std::string_view label, int& index, const std::vector<std::string>& options);
  bool keyRow(float x, float y, float w, std::string_view label, std::string_view keys, bool listening);
  // Simple list row with left text and right detail; returns true when activated.
  bool listRow(float x, float y, float w, float h, std::string_view text, std::string_view detail,
               std::string_view sub = {}, bool enabled = true);
  int tabs(float x, float y, const std::vector<std::string>& labels, int current);

  // Scrolling helper for long lists: rows [first, first+visible) are shown; keeps focus visible.
  int scrollFor(int focusInList, int count, int visible);
  // Registers a focusable that is scrolled out of view (keeps indices stable).
  int skipFocusable() { return nextFocus(); }

 private:
  int nextFocus() { return count_++; }
  bool hovered(float x, float y, float w, float h) const;  // scaled rect
  void rowFrame(float x, float y, float w, float h, bool focused);

  OverlayBuilder* o_ = nullptr;
  const UiTheme* theme_ = nullptr;
  UiInput in_;
  float w_ = 0, h_ = 0, scale_ = 1;
  double time_ = 0;
  int focus_ = 0, count_ = 0, lastCount_ = 0;
  int dragging_ = -1;
  int scroll_ = 0;
};

}  // namespace fh
