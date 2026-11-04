// Stage 7: settings file parsing, the Ui kit's focus and scrolling logic, text fitting, and the
// rebinding rules. Runs headless: without a font atlas the overlay falls back to its debug font.
#include <fstream>
#include <map>
#include <sstream>

#include <nlohmann/json.hpp>

#include "game/settings.h"
#include "game/ui/bindings.h"
#include "game/ui/ui.h"
#include "test_framework.h"

using namespace fh;

// ---- Settings ------------------------------------------------------------------------------

TEST(settings_round_trip_keeps_every_field) {
  Settings a;
  a.look = "clean";
  a.vsync = false;
  a.fullscreen = true;
  a.fovOffsetDeg = 12;
  a.uiScale = 1.25f;
  a.cameraShake = false;
  a.shadows = false;
  a.ambientOcclusion = false;
  a.reflections = false;
  a.masterVolume = 0.5f;
  a.ambienceVolume = 0.25f;
  a.effectsVolume = 0.75f;
  a.engineVolume = 0.1f;
  a.uiVolume = 0.2f;
  a.mouseSensitivity = 2.0f;
  a.invertY = true;
  a.bindings["ship.boost"] = {"Right Shift"};
  a.bindings["foot.jump"] = {"Space", "J"};
  a.showDevOverlay = true;
  a.showContractTracker = false;
  a.showHints = false;
  std::string err;
  const auto b = parseSettings(serializeSettings(a), &err);
  CHECK(b.has_value());
  if (!b) return;
  CHECK(b->look == "clean");
  CHECK(!b->vsync);
  CHECK(b->fullscreen);
  CHECK_NEAR(b->fovOffsetDeg, 12, 1e-5);
  CHECK_NEAR(b->uiScale, 1.25, 1e-5);
  CHECK(!b->cameraShake);
  CHECK(!b->shadows);
  CHECK(!b->ambientOcclusion);
  CHECK(!b->reflections);
  CHECK_NEAR(b->masterVolume, 0.5, 1e-5);
  CHECK_NEAR(b->ambienceVolume, 0.25, 1e-5);
  CHECK_NEAR(b->effectsVolume, 0.75, 1e-5);
  CHECK_NEAR(b->engineVolume, 0.1, 1e-5);
  CHECK_NEAR(b->uiVolume, 0.2, 1e-5);
  CHECK_NEAR(b->mouseSensitivity, 2.0, 1e-5);
  CHECK(b->invertY);
  CHECK(b->bindings == a.bindings);
  CHECK(b->showDevOverlay);
  CHECK(!b->showContractTracker);
  CHECK(!b->showHints);
  CHECK(serializeSettings(*b) == serializeSettings(a));  // stable text
}

TEST(settings_out_of_range_values_are_clamped) {
  const auto s = parseSettings(
      R"({"graphics":{"fovOffsetDeg":500,"uiScale":0.01},"audio":{"master":7,"ui":-3},"controls":{"mouseSensitivity":99}})",
      nullptr);
  CHECK(s.has_value());
  if (!s) return;
  CHECK_NEAR(s->fovOffsetDeg, 20, 1e-5);
  CHECK_NEAR(s->uiScale, 0.75, 1e-5);
  CHECK_NEAR(s->masterVolume, 1, 1e-5);
  CHECK_NEAR(s->uiVolume, 0, 1e-5);
  CHECK_NEAR(s->mouseSensitivity, 3, 1e-5);
}

TEST(settings_wrong_types_and_unknown_keys_fall_back_to_defaults) {
  const Settings d;
  const auto s = parseSettings(
      R"({"graphics":{"vsync":"yes","uiScale":"big","look":7},"controls":{"bindings":{"a":"W","b":[1,"K"]}},"future":{"x":1}})",
      nullptr);
  CHECK(s.has_value());
  if (!s) return;
  CHECK(s->vsync == d.vsync);
  CHECK_NEAR(s->uiScale, d.uiScale, 1e-5);
  CHECK(s->look.empty());
  CHECK(s->bindings.count("a") == 0);  // not an array
  CHECK(s->bindings.at("b") == std::vector<std::string>{"K"});  // non-string keys dropped
}

TEST(settings_sections_of_the_wrong_type_do_not_throw) {
  const auto s = parseSettings(R"({"graphics":3,"audio":[],"controls":"x","gameplay":null})", nullptr);
  CHECK(s.has_value());
}

TEST(settings_corrupt_text_is_an_error_not_a_crash) {
  std::string err;
  CHECK(!parseSettings("{not json", &err).has_value());
  CHECK(!err.empty());
  CHECK(!parseSettings("[1,2,3]", nullptr).has_value());
  CHECK(!parseSettings("", nullptr).has_value());
}

// ---- Ui focus ------------------------------------------------------------------------------

namespace {
constexpr float kRowH = 40;  // UiTheme default rowHeight, in design px; the test window is 1080p

struct Harness {
  OverlayBuilder o;
  UiTheme theme;
  Ui ui;
  // One frame with `count` rows at x=0, w=500, rowH apart. Returns the index of a row that fired.
  int frame(int count, UiInput in = {}) {
    o.clear();
    ui.begin(o, theme, 1920, 1080, 1.0f, in, 0.0);
    int fired = -1;
    for (int i = 0; i < count; ++i)
      if (ui.keyRow(0, float(i) * kRowH, 500, "row", "K", false)) fired = i;
    ui.end();
    return fired;
  }
};
UiInput key(bool UiInput::*k) {
  UiInput in;
  in.*k = true;
  return in;
}
UiInput mouseAt(float x, float y, bool pressed = false) {
  UiInput in;
  in.mouseX = x;
  in.mouseY = y;
  in.mouseMoved = true;
  in.mousePressed = pressed;
  in.mouseDown = pressed;
  return in;
}
}  // namespace

TEST(ui_focus_moves_and_wraps_with_the_keyboard) {
  Harness h;
  h.frame(3);  // first frame teaches the kit the row count
  CHECK(h.ui.focus() == 0);
  h.frame(3, key(&UiInput::down));
  CHECK(h.ui.focus() == 1);
  h.frame(3, key(&UiInput::down));
  h.frame(3, key(&UiInput::down));
  CHECK(h.ui.focus() == 0);  // wrapped past the end
  h.frame(3, key(&UiInput::up));
  CHECK(h.ui.focus() == 2);  // wrapped past the start
}

TEST(ui_accept_fires_only_the_focused_row) {
  Harness h;
  h.frame(3);
  h.frame(3, key(&UiInput::down));
  CHECK(h.frame(3, key(&UiInput::accept)) == 1);
  CHECK(h.frame(3) == -1);
}

TEST(ui_focus_is_clamped_when_the_list_shrinks) {
  Harness h;
  h.frame(5);
  h.ui.setFocus(4);
  h.frame(5);
  h.frame(2);
  CHECK(h.ui.focus() == 1);
}

TEST(ui_mouse_hover_takes_focus_and_click_fires_that_row) {
  Harness h;
  h.frame(3);
  CHECK(h.frame(3, mouseAt(100, kRowH * 2 + 5)) == -1);
  CHECK(h.ui.focus() == 2);
  CHECK(h.frame(3, mouseAt(100, kRowH + 5, true)) == 1);
  CHECK(h.ui.focus() == 1);
}

TEST(ui_a_resting_mouse_does_not_steal_keyboard_focus) {
  Harness h;
  h.frame(3);
  h.frame(3, mouseAt(100, 5));  // rests on row 0
  h.frame(3, key(&UiInput::down));
  UiInput still = key(&UiInput::down);
  still.mouseX = 100;
  still.mouseY = 5;
  h.frame(3, still);  // mouse is over row 0 but has not moved
  CHECK(h.ui.focus() == 2);
}

TEST(ui_slider_steps_with_keys_and_drags_with_the_mouse) {
  Harness h;
  float v = 0.5f;
  auto frame = [&](UiInput in) {
    h.o.clear();
    h.ui.begin(h.o, h.theme, 1920, 1080, 1.0f, in, 0.0);
    const bool changed = h.ui.sliderRow(0, 0, 1000, "s", v, 0.0f, 1.0f, 0.1f, "");
    h.ui.end();
    return changed;
  };
  frame({});
  CHECK(frame(key(&UiInput::right)));
  CHECK_NEAR(v, 0.6, 1e-5);
  CHECK(frame(key(&UiInput::left)));
  CHECK_NEAR(v, 0.5, 1e-5);
  // The bar spans x in [480, 860] for w=1000. Press on it, then drag past both ends.
  frame(mouseAt(480 + 380 * 0.2f, 20, true));
  CHECK_NEAR(v, 0.2, 1e-4);
  CHECK(h.ui.dragging());
  UiInput drag = mouseAt(5000, 500);  // far outside the row: the drag still owns the slider
  drag.mouseDown = true;
  frame(drag);
  CHECK_NEAR(v, 1.0, 1e-5);
  drag.mouseX = -100;
  frame(drag);
  CHECK_NEAR(v, 0.0, 1e-5);
  frame({});  // released
  CHECK(!h.ui.dragging());
  frame(mouseAt(5000, 500));
  CHECK_NEAR(v, 0.0, 1e-5);
}

TEST(ui_toggle_and_choice_rows_respond_to_keys) {
  Harness h;
  bool on = false;
  int pick = 0;
  const std::vector<std::string> opts{"a", "b", "c"};
  auto frame = [&](UiInput in) {
    h.o.clear();
    h.ui.begin(h.o, h.theme, 1920, 1080, 1.0f, in, 0.0);
    h.ui.toggleRow(0, 0, 500, "t", on);
    h.ui.choiceRow(0, 50, 500, "c", pick, opts);
    h.ui.end();
  };
  frame({});
  frame(key(&UiInput::accept));
  CHECK(on);
  frame(key(&UiInput::down));
  frame(key(&UiInput::left));
  CHECK(pick == 2);  // wraps backwards
  frame(key(&UiInput::right));
  CHECK(pick == 0);
  CHECK(on);  // the unfocused toggle did not react
}

TEST(ui_tab_key_cycles_tabs) {
  Harness h;
  auto frame = [&](UiInput in, int cur) {
    h.o.clear();
    h.ui.begin(h.o, h.theme, 1920, 1080, 1.0f, in, 0.0);
    const int r = h.ui.tabs(0, 0, {"A", "B", "C"}, cur);
    h.ui.end();
    return r;
  };
  CHECK(frame({}, 1) == 1);
  CHECK(frame(key(&UiInput::tabNext), 1) == 2);
  CHECK(frame(key(&UiInput::tabNext), 2) == 0);
}

TEST(ui_keyboard_focus_skips_disabled_items_both_ways) {
  Harness h;
  auto frame = [&](UiInput in) {
    h.o.clear();
    h.ui.begin(h.o, h.theme, 1920, 1080, 1.0f, in, 0.0);
    int fired = -1;
    if (h.ui.menuItem(0, 0, 400, "disabled", false)) fired = 0;
    if (h.ui.menuItem(0, 50, 400, "a")) fired = 1;
    if (h.ui.menuItem(0, 100, 400, "b")) fired = 2;
    h.ui.end();
    return fired;
  };
  frame({});
  CHECK(h.ui.focus() == 1);  // starts on the first enabled item, as the main menu needs
  frame(key(&UiInput::up));  // up from there lands on the disabled item and passes over it
  frame({});
  CHECK(h.ui.focus() == 2);
  frame(key(&UiInput::down));  // wraps to the disabled item, passes over it downward
  CHECK(h.ui.focus() == 1);
  CHECK(frame(key(&UiInput::accept)) == 1);
  CHECK(frame(mouseAt(100, 10, true)) == -1);  // clicking a disabled item does nothing
}

TEST(ui_reset_focus_restores_the_downward_skip) {
  Harness h;
  auto frame = [&](UiInput in) {
    h.o.clear();
    h.ui.begin(h.o, h.theme, 1920, 1080, 1.0f, in, 0.0);
    h.ui.menuItem(0, 0, 400, "disabled", false);
    h.ui.menuItem(0, 50, 400, "a");
    h.ui.menuItem(0, 100, 400, "b");
    h.ui.end();
  };
  frame({});
  frame(key(&UiInput::up));
  h.ui.resetFocus();  // reopening a screen after moving up must not jump to the last item
  frame({});
  CHECK(h.ui.focus() == 1);
}

// ---- Scrolling (the Controls tab) ----------------------------------------------------------

namespace {
struct ScrollHarness {
  OverlayBuilder o;
  UiTheme theme;
  Ui ui;
  static constexpr int kAbove = 2, kCount = 30, kVisible = 8;  // two rows, a list, then a button
  // Returns the first visible list row; `fired` receives the list row that fired, or -1.
  int frame(UiInput in = {}, int* fired = nullptr) {
    o.clear();
    ui.begin(o, theme, 1920, 1080, 1.0f, in, 0.0);
    ui.keyRow(0, 0, 500, "above", "K", false);
    ui.keyRow(0, kRowH, 500, "above", "K", false);
    const int first = ui.scrollFor(kAbove, kCount, kVisible);
    float y = kRowH * 2;
    if (fired) *fired = -1;
    for (int i = 0; i < kCount; ++i) {
      if (i < first || i >= first + kVisible) {
        ui.skipFocusable();
        continue;
      }
      if (ui.keyRow(0, y, 500, "row", "K", false) && fired) *fired = i;
      y += kRowH;
    }
    ui.button(600, 0, 100, 40, "RESET");
    ui.end();
    return first;
  }
};
}  // namespace

TEST(ui_scroll_follows_keyboard_focus_down_and_back_up) {
  ScrollHarness h;
  h.frame();
  int first = 0;
  for (int i = 0; i < ScrollHarness::kAbove + 20; ++i) first = h.frame(key(&UiInput::down));
  const int inList = h.ui.focus() - ScrollHarness::kAbove;
  CHECK(inList == 20);
  CHECK(inList >= first && inList < first + ScrollHarness::kVisible);
  for (int i = 0; i < 18; ++i) first = h.frame(key(&UiInput::up));
  CHECK(h.ui.focus() - ScrollHarness::kAbove == 2);
  CHECK(first <= 2);
}

TEST(ui_every_list_row_is_reachable_and_activates_itself) {
  ScrollHarness h;
  h.frame();
  for (int i = 0; i < ScrollHarness::kAbove; ++i) h.frame(key(&UiInput::down));
  for (int row = 0; row < ScrollHarness::kCount; ++row) {
    int fired = -1;
    h.frame(key(&UiInput::accept), &fired);
    CHECK(fired == row);
    h.frame(key(&UiInput::down));
  }
}

TEST(ui_scroll_wraps_to_the_ends_with_focus) {
  ScrollHarness h;
  h.frame();
  CHECK(h.frame(key(&UiInput::up)) == ScrollHarness::kCount - ScrollHarness::kVisible);  // wrapped to the button
  CHECK(h.ui.focus() == ScrollHarness::kAbove + ScrollHarness::kCount);
  CHECK(h.frame(key(&UiInput::down)) == 0);  // wrapped to the rows above the list
  CHECK(h.ui.focus() == 0);
}

TEST(ui_wheel_keeps_focus_on_a_visible_row_so_enter_still_works) {
  ScrollHarness h;
  h.frame();
  for (int i = 0; i < ScrollHarness::kAbove; ++i) h.frame(key(&UiInput::down));
  UiInput wheel;
  wheel.wheel = -3;  // scroll down 6 rows
  const int first = h.frame(wheel);
  CHECK(first == 6);
  CHECK(h.ui.focus() - ScrollHarness::kAbove == 6);  // pulled along with the list
  int fired = -1;
  h.frame(key(&UiInput::accept), &fired);
  CHECK(fired == 6);
  for (int i = 0; i < 20; ++i) h.frame(wheel);
  CHECK(h.frame() == ScrollHarness::kCount - ScrollHarness::kVisible);  // clamped at the end
}

TEST(ui_short_lists_never_scroll) {
  OverlayBuilder o;
  UiTheme t;
  Ui ui;
  ui.begin(o, t, 1920, 1080, 1.0f, {}, 0.0);
  CHECK(ui.scrollFor(0, 4, 8) == 0);
  ui.end();
}

// ---- Text ----------------------------------------------------------------------------------

TEST(ui_wrap_text_respects_width_and_keeps_every_word) {
  const std::string text = "The quick brown fox jumps over the lazy dog";
  const float px = 18, w = OverlayBuilder::measure("The quick brown", px);
  const auto lines = wrapText(text, px, w);
  CHECK(lines.size() > 1);
  std::string joined;
  for (const auto& l : lines) {
    CHECK(OverlayBuilder::measure(l, px) <= w + 0.01f);
    joined += (joined.empty() ? "" : " ") + l;
  }
  CHECK(joined == text);
}

TEST(ui_wrap_text_handles_edge_cases) {
  CHECK(wrapText("", 18, 100).empty());
  CHECK(wrapText("   ", 18, 100).empty());
  const auto one = wrapText("Unbreakablelongword next", 18, 10);
  CHECK(one.size() == 2);  // an overlong word gets its own line instead of looping forever
  CHECK(one[0] == "Unbreakablelongword");
}

TEST(ui_fit_text_only_shortens_when_needed) {
  CHECK(fitText("short", 18, 10000) == "short");
  const std::string s = fitText("a rather long contract title that will not fit", 18, 120);
  CHECK(s.size() >= 3 && s.substr(s.size() - 3) == "...");
  CHECK(OverlayBuilder::measure(s, 18) <= 120);
  CHECK(fitText("anything", 18, 0) == "...");  // never loops or underflows
}

TEST(ui_layout_scale_follows_height_and_user_scale) {
  CHECK_NEAR(uiLayoutScale(1080, 1.0f), 1.0, 1e-5);
  CHECK_NEAR(uiLayoutScale(2160, 1.0f), 2.0, 1e-5);
  CHECK_NEAR(uiLayoutScale(1080, 1.5f), 1.5, 1e-5);
  CHECK_NEAR(uiLayoutScale(100, 1.0f), 0.6, 1e-5);  // floors on tiny windows
}

// ---- Rebinding rules -----------------------------------------------------------------------

namespace {
BindingLookup lookupFrom(std::map<std::string, std::vector<std::string>> m) {
  return [m = std::move(m)](const std::string& a) {
    auto it = m.find(a);
    return it == m.end() ? std::vector<std::string>{} : it->second;
  };
}
}  // namespace

TEST(bindings_flight_and_foot_may_share_a_key) {
  const auto l = lookupFrom({{"ship.throttleUp", {"W"}}, {"foot.forward", {"W"}}});
  CHECK(bindingConflict("ship.throttleUp", "W", l).empty());
  CHECK(bindingConflict("foot.forward", "W", l).empty());
}

TEST(bindings_same_context_conflicts_and_names_the_other_action) {
  const auto l = lookupFrom({{"ship.boost", {"K"}}, {"foot.jump", {"K"}}});
  CHECK(bindingConflict("ship.throttleUp", "K", l) == "Boost");
  CHECK(bindingConflict("foot.run", "K", l) == "Jump");
}

TEST(bindings_an_always_action_conflicts_with_everything) {
  const auto l = lookupFrom({{"foot.jump", {"K"}}, {"interact", {"F"}}});
  CHECK(bindingConflict("ui.market", "K", l) == "Jump");
  CHECK(bindingConflict("ship.boost", "F", l) == "Interact / leave seat");
  CHECK(bindingConflict("foot.jump", "F", l) == "Interact / leave seat");
}

TEST(bindings_an_action_never_conflicts_with_itself) {
  const auto l = lookupFrom({{"ship.boost", {"K"}}});
  CHECK(bindingConflict("ship.boost", "K", l).empty());
}

TEST(bindings_system_keys_are_reserved) {
  const auto l = lookupFrom({{"app.quit", {"F10"}}, {"app.menu", {"Escape"}}, {"ship.boost", {"K"}}});
  CHECK(isReservedKey("F10", l));
  CHECK(isReservedKey("Escape", l));
  CHECK(!isReservedKey("K", l));
}

TEST(bindings_list_has_unique_actions_with_labels) {
  std::map<std::string, int> seen;
  for (const Bindable& b : bindables()) {
    CHECK(++seen[b.action] == 1);
    CHECK(b.label && *b.label);
  }
}

TEST(bindings_shipped_input_file_binds_every_action_without_conflicts) {
  std::ifstream f(std::string(FH_TEST_CONTENT_DIR) + "/config/input.json");
  std::stringstream ss;
  ss << f.rdbuf();
  const auto j = nlohmann::json::parse(ss.str(), nullptr, false);
  CHECK(!j.is_discarded() && j.contains("bindings"));
  if (j.is_discarded() || !j.contains("bindings")) return;
  std::map<std::string, std::vector<std::string>> m;
  for (const auto& [action, keys] : j["bindings"].items()) m[action] = keys.get<std::vector<std::string>>();
  const auto l = lookupFrom(m);
  for (const Bindable& b : bindables()) {
    const auto keys = l(b.action);
    CHECK(!keys.empty());
    for (const auto& k : keys) {
      CHECK(bindingConflict(b.action, k, l).empty());
      CHECK(!isReservedKey(k, l));
    }
  }
}
