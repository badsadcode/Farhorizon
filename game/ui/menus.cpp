#include "game/ui/menus.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "game/ui/bindings.h"

namespace fh {

namespace {
std::string joinKeys(const std::vector<std::string>& keys) {
  std::string s;
  for (const auto& k : keys) s += (s.empty() ? "" : " / ") + k;
  return s.empty() ? std::string("--") : s;
}
}  // namespace

void MenuSystem::open(Screen s) {
  if (s == Screen::Main || s == Screen::Pause) root_ = s;
  screen_ = s;
  pendingFocusFor_ = s;
  listening_.clear();
  notice_.clear();
  pendingUiScale_ = -1;
}

void MenuSystem::back() {
  listening_.clear();
  switch (screen_) {
    case Screen::Pause: screen_ = Screen::None; break;
    case Screen::Main: open(Screen::ConfirmQuit); break;
    default: open(root_); break;
  }
}

std::vector<MenuAction> MenuSystem::update(Ui& ui, MenuEnv& env) {
  std::vector<MenuAction> out;
  if (pendingFocusFor_ == screen_ && screen_ != Screen::None) {
    ui.resetFocus();
    pendingFocusFor_ = Screen::None;
  }
  // Key capture for rebinding swallows everything (Escape cancels).
  if (!listening_.empty()) {
    const int key = env.input.firstKeyPressed();
    const std::string name = key >= 0 ? InputState::keyName(key) : std::string();
    if (!name.empty()) {  // keys SDL cannot name cannot be stored: keep listening
      const BindingLookup lookup = [&](const std::string& a) { return env.input.bindingNames(a); };
      if (name == "Escape") {
        notice_.clear();
      } else if (isReservedKey(name, lookup)) {
        notice_ = name + " is reserved by the game";
        noticeBad_ = true;
      } else {
        const std::string other = bindingConflict(listening_, name, lookup);
        noticeBad_ = !other.empty();
        notice_ = other.empty() ? "Bound to " + name : name + " is also used by: " + other;
        env.settings.bindings[listening_] = {name};
        out.push_back({MenuAction::Type::BindingsChanged, {}});
      }
      listening_.clear();
    }
  } else if (ui.input().back) {
    const Screen before = screen_;
    if (before == Screen::Options) out.push_back({MenuAction::Type::SettingsClosed, {}});
    back();
    if (before == Screen::Pause) out.push_back({MenuAction::Type::Resume, {}});
    return out;
  }
  switch (screen_) {
    case Screen::None: break;
    case Screen::Main: mainMenu(ui, env, out); break;
    case Screen::Pause: pauseMenu(ui, env, out); break;
    case Screen::Options: options(ui, env, out); break;
    case Screen::Load: slots(ui, env, out, false); break;
    case Screen::Save: slots(ui, env, out, true); break;
    case Screen::ConfirmQuit: confirm(ui, out, "Quit to desktop?", MenuAction::Type::Quit); break;
    case Screen::ConfirmNew: confirm(ui, out, "Start a new game? Unsaved progress is lost.", MenuAction::Type::NewGame); break;
    case Screen::ConfirmMenu:
      confirm(ui, out, "Quit to the main menu? Unsaved progress is lost.", MenuAction::Type::QuitToMenu);
      break;
  }
  return out;
}

void MenuSystem::mainMenu(Ui& ui, MenuEnv& env, std::vector<MenuAction>& out) {
  const UiTheme& t = ui.theme();
  ui.leftShade(0.62f);
  const float x = 120;
  ui.spacedTitle(x, 190, "FARHORIZON", t.title, t.textBright);
  ui.label(x + 6, 304, "A QUIET FRONTIER", t.subtitle, t.text, FontFace::Sans);
  ui.draw().line(ui.s(x + 6), ui.s(340), ui.s(x + 520), ui.s(340), 1.0f, t.textDim);
  float y = 400;
  const float step = 58, w = 460;
  if (ui.menuItem(x, y, w, "CONTINUE", env.canContinue)) out.push_back({MenuAction::Type::Continue, {}});
  y += step;
  if (ui.menuItem(x, y, w, "NEW GAME")) {
    if (env.canContinue) open(Screen::ConfirmNew);
    else out.push_back({MenuAction::Type::NewGame, {}});
  }
  y += step;
  if (ui.menuItem(x, y, w, "LOAD GAME")) open(Screen::Load);
  y += step;
  if (ui.menuItem(x, y, w, "OPTIONS")) open(Screen::Options);
  y += step;
  if (ui.menuItem(x, y, w, "QUIT")) open(Screen::ConfirmQuit);
  ui.label(x + 6, ui.height() / ui.scale() - 92, env.version, t.small, t.textDim);
  ui.hintBar("UP/DOWN select    ENTER confirm    ESC quit    Mouse works too");
}

void MenuSystem::pauseMenu(Ui& ui, MenuEnv& env, std::vector<MenuAction>& out) {
  const UiTheme& t = ui.theme();
  (void)env;
  ui.backdrop(0.85f);
  const float W = ui.width() / ui.scale(), H = ui.height() / ui.scale();
  const float pw = 520, ph = 6 * 58 + 150, px = (W - pw) * 0.5f, py = (H - ph) * 0.5f;
  ui.panel(px, py, pw, ph);
  ui.spacedTitle(px + 40, py + 30, "PAUSED", t.heading + 8, t.textBright);
  ui.draw().line(ui.s(px + 40), ui.s(py + 84), ui.s(px + pw - 40), ui.s(py + 84), 1.0f, t.textDim);
  float y = py + 106;
  const float step = 58, w = pw - 60;
  if (ui.menuItem(px + 30, y, w, "RESUME")) out.push_back({MenuAction::Type::Resume, {}});
  y += step;
  if (ui.menuItem(px + 30, y, w, "SAVE GAME")) open(Screen::Save);
  y += step;
  if (ui.menuItem(px + 30, y, w, "LOAD GAME")) open(Screen::Load);
  y += step;
  if (ui.menuItem(px + 30, y, w, "OPTIONS")) open(Screen::Options);
  y += step;
  if (ui.menuItem(px + 30, y, w, "MAIN MENU")) open(Screen::ConfirmMenu);
  y += step;
  if (ui.menuItem(px + 30, y, w, "QUIT TO DESKTOP")) open(Screen::ConfirmQuit);
  ui.hintBar("UP/DOWN select    ENTER confirm    ESC resume");
}

void MenuSystem::options(Ui& ui, MenuEnv& env, std::vector<MenuAction>& out) {
  const UiTheme& t = ui.theme();
  Settings& s = env.settings;
  ui.backdrop(root_ == Screen::Main ? 0.55f : 0.85f);
  const float W = ui.width() / ui.scale(), H = ui.height() / ui.scale();
  const float pw = std::min(1180.0f, W - 80), ph = std::min(820.0f, H - 120);
  const float px = (W - pw) * 0.5f, py = (H - ph) * 0.5f - 10;
  ui.panel(px, py, pw, ph);
  ui.spacedTitle(px + 40, py + 28, "OPTIONS", t.heading + 8, t.textBright);
  const int tab = ui.tabs(px + 40, py + 84, {"GRAPHICS", "AUDIO", "CONTROLS", "GAMEPLAY"}, optionsTab_);
  if (tab != optionsTab_) {
    optionsTab_ = tab;
    ui.resetFocus();
    listening_.clear();
    notice_.clear();
  }
  const float rx = px + 40, rw = pw - 80, row = t.rowHeight + 6;
  float y = py + 146;
  bool changed = false;
  auto pct = [](float v) { return std::format("{:.0f}%", v * 100.0f); };
  switch (optionsTab_) {
    case 0: {
      // All rows must clear the BACK button at any interface scale.
      const float gRow = std::min(row, (py + ph - 84 - y) / 12.0f);  // 12 rows: keep them inside the panel
      int look = 0;
      for (size_t i = 0; i < env.lookIds.size(); ++i)
        if (env.lookIds[i] == s.look) look = int(i);
      if (ui.choiceRow(rx, y, rw, "Look", look, env.lookNames)) {
        s.look = env.lookIds[size_t(look)];
        changed = true;
      }
      y += gRow;
      changed |= ui.toggleRow(rx, y, rw, "Fullscreen", s.fullscreen);
      y += gRow;
      changed |= ui.toggleRow(rx, y, rw, "Vertical sync", s.vsync);
      y += gRow;
      changed |= ui.sliderRow(rx, y, rw, "Field of view", s.fovOffsetDeg, -15, 20, 1,
                              std::format("{:+.0f} deg", s.fovOffsetDeg));
      y += gRow;
      {  // Applied on release: the slider itself moves when the scale changes.
        if (pendingUiScale_ < 0) pendingUiScale_ = s.uiScale;
        ui.sliderRow(rx, y, rw, "Interface scale", pendingUiScale_, 0.75f, 1.5f, 0.05f, pct(pendingUiScale_));
        if (!ui.dragging() && pendingUiScale_ != s.uiScale) {
          s.uiScale = pendingUiScale_;
          changed = true;
        }
      }
      y += gRow;
      changed |= ui.toggleRow(rx, y, rw, "Camera shake", s.cameraShake);
      y += gRow;
      changed |= ui.toggleRow(rx, y, rw, "Shadows", s.shadows);
      y += gRow;
      changed |= ui.toggleRow(rx, y, rw, "Ambient occlusion", s.ambientOcclusion);
      y += gRow;
      changed |= ui.toggleRow(rx, y, rw, "Reflections", s.reflections);
      y += gRow;
      changed |= ui.toggleRow(rx, y, rw, "Volumetric clouds", s.volumetricClouds);
      y += gRow;
      changed |= ui.sliderRow(rx, y, rw, "Render scale", s.renderScale, 0.5f, 1.0f, 0.05f, pct(s.renderScale));
      y += gRow;
      changed |= ui.toggleRow(rx, y, rw, "Low spec mode (fast, plain)", s.lowSpec);
      break;
    }
    case 1:
      changed |= ui.sliderRow(rx, y, rw, "Master volume", s.masterVolume, 0, 1, 0.05f, pct(s.masterVolume));
      y += row;
      changed |= ui.sliderRow(rx, y, rw, "Ambience", s.ambienceVolume, 0, 1, 0.05f, pct(s.ambienceVolume));
      y += row;
      changed |= ui.sliderRow(rx, y, rw, "Effects", s.effectsVolume, 0, 1, 0.05f, pct(s.effectsVolume));
      y += row;
      changed |= ui.sliderRow(rx, y, rw, "Engine", s.engineVolume, 0, 1, 0.05f, pct(s.engineVolume));
      y += row;
      changed |= ui.sliderRow(rx, y, rw, "Interface", s.uiVolume, 0, 1, 0.05f, pct(s.uiVolume));
      break;
    case 2: {
      changed |= ui.sliderRow(rx, y, rw, "Mouse sensitivity", s.mouseSensitivity, 0.25f, 3.0f, 0.05f,
                              std::format("{:.2f}x", s.mouseSensitivity));
      y += row;
      changed |= ui.toggleRow(rx, y, rw, "Invert mouse Y", s.invertY);
      y += row + 8;
      ui.label(rx + 16, y, "KEY BINDINGS", t.small, t.textDim);
      y += 28;
      const auto list = bindables();
      const int count = int(list.size());
      const int visible = std::max(3, int((py + ph - 96 - y) / row));
      const int first = ui.scrollFor(2, count, visible);  // two rows above the list
      const BindingLookup lookup = [&](const std::string& a) { return env.input.bindingNames(a); };
      const float listTop = y;
      for (int i = 0; i < count; ++i) {
        const Bindable& b = list[size_t(i)];
        if (i < first || i >= first + visible) {
          ui.skipFocusable();
          continue;
        }
        const auto keys = env.input.bindingNames(b.action);
        bool conflict = false;
        for (const auto& k : keys) conflict |= !bindingConflict(b.action, k, lookup).empty();
        const bool listening = listening_ == b.action;
        if (ui.keyRow(rx, y, rw - 24, b.label, joinKeys(keys), listening, conflict) && listening_.empty()) {
          listening_ = b.action;
          notice_.clear();
        }
        y += row;
      }
      if (count > visible) {  // scroll indicator
        const float sx = ui.s(px + pw - 40), sy0 = ui.s(listTop), sh = ui.s(row * float(visible));
        ui.draw().rect(sx, sy0, 2, sh, t.textDim.withAlpha(0.4f));
        ui.draw().rect(sx - 1, sy0 + sh * float(first) / float(count), 4, sh * float(visible) / float(count), t.text);
      }
      if (!notice_.empty()) ui.label(px + 270, py + ph - 58, notice_, t.small, noticeBad_ ? t.danger : t.ok);
      if (ui.button(px + pw - 300, py + ph - 70, 260, 44, "RESET KEYS")) {
        s.bindings.clear();
        notice_ = "Default keys restored";
        noticeBad_ = false;
        out.push_back({MenuAction::Type::ResetBindings, {}});
      }
      break;
    }
    case 3:
      changed |= ui.toggleRow(rx, y, rw, "Contract tracker on the HUD", s.showContractTracker);
      y += row;
      changed |= ui.toggleRow(rx, y, rw, "Control hints", s.showHints);
      y += row;
      changed |= ui.toggleRow(rx, y, rw, "Developer info overlay", s.showDevOverlay);
      break;
    default: break;
  }
  if (changed) {
    s.clamp();
    out.push_back({MenuAction::Type::SettingsChanged, {}});
  }
  if (ui.button(px + 40, py + ph - 70, 200, 44, "BACK")) {
    out.push_back({MenuAction::Type::SettingsClosed, {}});
    back();
  }
  ui.hintBar(listening_.empty() ? "TAB next tab    UP/DOWN select    LEFT/RIGHT adjust    ENTER change    WHEEL scroll    ESC back"
                                : "PRESS THE NEW KEY    ESC cancel");
}

void MenuSystem::slots(Ui& ui, MenuEnv& env, std::vector<MenuAction>& out, bool saving) {
  const UiTheme& t = ui.theme();
  ui.backdrop(root_ == Screen::Main ? 0.55f : 0.85f);
  const float W = ui.width() / ui.scale(), H = ui.height() / ui.scale();
  const float pw = std::min(980.0f, W - 80);
  int shown = 0;
  for (const auto& sl : env.slots) shown += saving ? sl.writable : 1;
  const float rowH = 74, ph = 190 + rowH * float(shown);
  const float px = (W - pw) * 0.5f, py = (H - ph) * 0.5f;
  ui.panel(px, py, pw, ph);
  ui.spacedTitle(px + 40, py + 28, saving ? "SAVE GAME" : "LOAD GAME", t.heading + 8, t.textBright);
  float y = py + 96;
  for (const auto& sl : env.slots) {
    if (saving && !sl.writable) continue;
    const std::string sub = sl.exists ? sl.summary : std::string(saving ? "Empty slot" : "No save");
    if (ui.listRow(px + 30, y, pw - 60, rowH - 6, sl.label, sl.exists ? sl.when : "", sub, saving || sl.exists))
      out.push_back({saving ? MenuAction::Type::Save : MenuAction::Type::Load, sl.path});
    y += rowH;
  }
  if (ui.button(px + 40, py + ph - 70, 200, 44, "BACK")) back();
  ui.hintBar(saving ? "ENTER save to slot (overwrites)    ESC back" : "ENTER load    ESC back");
}

void MenuSystem::confirm(Ui& ui, std::vector<MenuAction>& out, const char* question, MenuAction::Type yes) {
  const UiTheme& t = ui.theme();
  ui.backdrop(0.85f);
  const float W = ui.width() / ui.scale(), H = ui.height() / ui.scale();
  const float pw = 720, ph = 220, px = (W - pw) * 0.5f, py = (H - ph) * 0.5f;
  ui.panel(px, py, pw, ph);
  ui.label(px + pw * 0.5f, py + 44, question, t.body + 2, t.textBright, FontFace::Mono, TextAlign::Center);
  if (ui.button(px + pw * 0.5f - 230, py + ph - 84, 210, 48, "YES")) out.push_back({yes, {}});
  if (ui.button(px + pw * 0.5f + 20, py + ph - 84, 210, 48, "NO")) back();
}

}  // namespace fh
