#pragma once
// Developer test driver (`--ui-script file`): feeds real SDL input events through the normal input
// path, so menus, rebinding and the terminal run exactly as they do for a player, and takes
// screenshots for review. One command per line; '#' starts a comment. Each command runs on its
// own frame.
//   key <name>               press and release a key (SDL scancode name: Down, Return, Left Ctrl)
//   hold <frames> <name>     hold a key down for N frames, with OS-style key repeat
//   move <x> <y>             move the mouse (window coordinates)
//   click <x> <y>            move there and click the left button
//   drag <x0> <y0> <x1> <y1> press at the start, move, release at the end
//   wheel <ticks>            scroll
//   wait <frames>            do nothing for N frames
//   shot <file>              screenshot
//   expect-binding <action> <key name>
//   expect-screen <name>     none | main | pause | options | load | save | confirm
//   expect-terminal <open|closed>
//   expect-speed-above <m/s>, expect-speed-below <m/s>, expect-spin-below <rad/s>   (player ship)
//   expect-location <cockpit|ship|station>
//   expect-docked
//   quit
// Failed expectations are logged and make the process exit with code 3.
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace fh {

struct UiScriptHost {
  std::function<void(const std::filesystem::path&)> screenshot;
  std::function<std::vector<std::string>(const std::string&)> bindingNames;
  std::function<std::string()> screenName;
  std::function<bool()> terminalOpen;
  std::function<float()> shipSpeed;
  std::function<float()> shipSpin;
  std::function<std::string()> location;
  std::function<bool()> docked;
  std::function<void()> quit;
  std::function<int()> enemiesAlive;
  std::function<float()> footHealth;
  std::function<int()> footMagazine;
  std::function<void()> aimAtEnemy;
};

class UiScript {
 public:
  bool load(const std::filesystem::path& path, std::string* error);
  // Call once per frame before polling events.
  void step(const UiScriptHost& host);
  bool active() const { return loaded_; }
  int failures() const { return failures_; }

 private:
  void fail(const std::string& message);

  std::vector<std::string> lines_;
  size_t next_ = 0;
  int waitFrames_ = 0;
  int heldKey_ = -1;
  int holdFrames_ = 0;
  bool releaseMouse_ = false;
  bool dragMove_ = false;
  float pendingDragX_ = 0, pendingDragY_ = 0;
  float mouseX_ = 0, mouseY_ = 0;
  int failures_ = 0;
  bool loaded_ = false;
};

}  // namespace fh
