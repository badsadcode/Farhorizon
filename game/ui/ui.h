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
  std::string text;        // typed this frame
  bool backspace = false;
};

// Layout scale shared by the menus, terminal and HUD: 1080p design pixels -> drawable pixels.
float uiLayoutScale(float height, float userScale);
// Greedy word wrap to `maxWidth` pixels; a word wider than the line stays on its own line.
std::vector<std::string> wrapText(std::string_view text, float px, float maxWidth, FontFace face = FontFace::Mono);
// Text shortened with "..." so it fits `maxWidth` pixels (unchanged when it already fits).
std::string fitText(std::string_view text, float px, float maxWidth, FontFace face = FontFace::Mono);

class Ui {
 public:
  void begin(OverlayBuilder& o, const UiTheme& theme, float width, float height, float userScale, const UiInput& in,
             double time);
  void end();
  void resetFocus(int index = 0) {
    focus_ = index;
    scroll_ = 0;
    moveDir_ = 1;
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
  // A text field. Returns true when Enter is pressed on it.
  bool textRow(float x, float y, float w, std::string_view label, std::string& value, size_t maxLength, bool secret = false);
  bool choiceRow(float x, float y, float w, std::string_view label, int& index, const std::vector<std::string>& options);
  // `conflict` marks a key that another action also fires on.
  bool keyRow(float x, float y, float w, std::string_view label, std::string_view keys, bool listening,
              bool conflict = false);
  // Simple list row with left text and right detail; returns true when activated.
  bool listRow(float x, float y, float w, float h, std::string_view text, std::string_view detail,
               std::string_view sub = {}, bool enabled = true);
  int tabs(float x, float y, const std::vector<std::string>& labels, int current);
  // Focusable row for custom content (the caller draws its columns on top).
  struct RowState {
    bool focused = false, hot = false, pressed = false, accepted = false;
  };
  RowState row(float x, float y, float w, float h);
  // Mouse-only button inside a row: does not take keyboard focus.
  bool inlineButton(float x, float y, float w, float h, std::string_view label, bool enabled = true);
  // Wrapped text; returns the height used (design px). `maxLines` 0 = no limit.
  float textBlock(float x, float y, float w, std::string_view text, float px, OverlayColor c, int maxLines = 0);
  bool dragging() const { return dragging_ >= 0; }

  // Scrolling for a list whose first row has focus index `firstFocus`: rows [first, first+visible)
  // are shown. Keyboard focus stays visible, and the wheel pulls focus along with the list.
  int scrollFor(int firstFocus, int count, int visible);
  // Registers a focusable that is scrolled out of view (keeps indices stable).
  int skipFocusable() { return nextFocus(); }

 private:
  int nextFocus() { return count_++; }
  bool hovered(float x, float y, float w, float h) const;  // scaled rect
  void rowFrame(float x, float y, float w, float h, bool focused);
  void hoverFocus(int id, bool hot);  // the mouse takes focus when it moves or clicks
  void skipIfDisabled(int id, bool enabled);  // keyboard focus passes over disabled widgets

  OverlayBuilder* o_ = nullptr;
  const UiTheme* theme_ = nullptr;
  UiInput in_;
  float w_ = 0, h_ = 0, scale_ = 1;
  double time_ = 0;
  int focus_ = 0, count_ = 0, lastCount_ = 0;
  int moveDir_ = 1;  // last keyboard direction
  int dragging_ = -1;
  int scroll_ = 0;
};

}  // namespace fh
