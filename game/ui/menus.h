#pragma once
// Front-end screens: main menu, pause menu, options (graphics/audio/controls/gameplay), load and
// save slots, confirmations. Drawn with the Ui kit; returns actions for the app to perform.
#include <filesystem>
#include <string>
#include <vector>

#include "engine/platform/input.h"
#include "game/settings.h"
#include "game/ui/ui.h"

namespace fh {

struct SaveSlotInfo {
  std::string label;              // "Slot 1", "Quicksave", "Autosave"
  std::filesystem::path path;
  bool exists = false;
  bool writable = true;           // autosave is load-only
  std::string summary;            // "Docked at Kestrel Station · 1,240 cr"
  std::string when;               // file time, "2026-11-04 17:20"
};

struct MenuEnv {
  Settings& settings;
  InputState& input;
  const std::vector<std::string>& lookNames;  // display names, index = settings look order
  const std::vector<std::string>& lookIds;
  const std::vector<SaveSlotInfo>& slots;
  bool canContinue = false;
  std::string version;
  bool networked = false;   // in a networked game: saving and loading belong to the server
  bool connecting = false;  // the Join game screen is waiting for the server
  std::string netStatus;    // why the last attempt failed, if it did
};

struct MenuAction {
  enum class Type {
    Resume, NewGame, Continue, Load, Save, QuitToMenu, Quit, Join,
    SettingsChanged,  // apply live (graphics/audio/mouse)
    BindingsChanged,  // settings.bindings changed: re-apply
    ResetBindings,    // reload input.json defaults
    SettingsClosed    // persist settings.json
  };
  Type type;
  std::filesystem::path path;
};

class MenuSystem {
 public:
  enum class Screen { None, Main, Pause, Options, Load, Save, ConfirmQuit, ConfirmNew, ConfirmMenu, Join };

  void open(Screen s);
  void close() { screen_ = Screen::None; }
  Screen screen() const { return screen_; }
  bool active() const { return screen_ != Screen::None; }
  bool atMainMenu() const { return active() && root_ == Screen::Main; }
  // While capturing, the app passes the Ui a neutral input and ignores its own hotkeys.
  bool capturingKey() const { return !listening_.empty(); }
  bool typing() const { return screen_ == Screen::Join; }  // letters are text here, not menu keys
  const std::string& joinPassword() const { return joinPassword_; }
  // Draws the current screen and handles its input.
  std::vector<MenuAction> update(Ui& ui, MenuEnv& env);

 private:
  void back();
  void mainMenu(Ui& ui, MenuEnv& env, std::vector<MenuAction>& out);
  void pauseMenu(Ui& ui, MenuEnv& env, std::vector<MenuAction>& out);
  void options(Ui& ui, MenuEnv& env, std::vector<MenuAction>& out);
  void slots(Ui& ui, MenuEnv& env, std::vector<MenuAction>& out, bool saving);
  void joinMenu(Ui& ui, MenuEnv& env, std::vector<MenuAction>& out);
  void confirm(Ui& ui, std::vector<MenuAction>& out, const char* question, MenuAction::Type yes);

  Screen screen_ = Screen::None;
  Screen root_ = Screen::Main;      // Main or Pause: where Back from sub-screens returns
  Screen pendingFocusFor_ = Screen::None;
  int optionsTab_ = 0;
  std::string joinPassword_;
  std::string listening_;           // action being rebound
  std::string notice_;              // result of the last rebind, shown on the Controls tab
  bool noticeBad_ = false;
  float pendingUiScale_ = -1;       // applied on release: the layout must not move under a drag
};

}  // namespace fh
