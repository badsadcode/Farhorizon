#include "game/ui/ui_script.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <format>
#include <sstream>

#include "engine/core/file_io.h"
#include "engine/core/log.h"

namespace fh {

namespace {
void pushKey(int scancode, bool down, bool repeat = false) {
  SDL_Event e{};
  e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  e.key.scancode = SDL_Scancode(scancode);
  e.key.down = down;
  e.key.repeat = repeat;
  SDL_PushEvent(&e);
}
void pushMotion(float x, float y, float dx, float dy) {
  SDL_Event e{};
  e.type = SDL_EVENT_MOUSE_MOTION;
  e.motion.x = x;
  e.motion.y = y;
  e.motion.xrel = dx;
  e.motion.yrel = dy;
  SDL_PushEvent(&e);
}
void pushButton(float x, float y, bool down) {
  SDL_Event e{};
  e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
  e.button.button = SDL_BUTTON_LEFT;
  e.button.down = down;
  e.button.x = x;
  e.button.y = y;
  SDL_PushEvent(&e);
}
std::string rest(std::istringstream& in) {
  std::string s;
  std::getline(in, s);
  const size_t b = s.find_first_not_of(" \t");
  const size_t e = s.find_last_not_of(" \t\r");
  return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}
}  // namespace

bool UiScript::load(const std::filesystem::path& path, std::string* error) {
  auto text = readTextFile(path);
  if (!text) {
    if (error) *error = "cannot read UI script " + path.string();
    return false;
  }
  std::istringstream in(*text);
  std::string line;
  while (std::getline(in, line)) {
    const size_t b = line.find_first_not_of(" \t\r");
    if (b == std::string::npos || line[b] == '#') continue;
    lines_.push_back(line.substr(b));
  }
  loaded_ = true;
  // Only scripted input counts (real devices have a non-zero `which`): someone touching the mouse
  // or keyboard during a run must not change the result.
  SDL_SetEventFilter(
      [](void*, SDL_Event* e) -> bool {
        switch (e->type) {
          case SDL_EVENT_KEY_DOWN:
          case SDL_EVENT_KEY_UP: return e->key.which == 0;
          case SDL_EVENT_MOUSE_MOTION: return e->motion.which == 0;
          case SDL_EVENT_MOUSE_BUTTON_DOWN:
          case SDL_EVENT_MOUSE_BUTTON_UP: return e->button.which == 0;
          case SDL_EVENT_MOUSE_WHEEL: return e->wheel.which == 0;
          default: return true;
        }
      },
      nullptr);
  return true;
}

void UiScript::fail(const std::string& message) {
  ++failures_;
  log::error("ui-script", "line '{}': {}", next_ > 0 ? lines_[next_ - 1] : std::string(), message);
}

void UiScript::step(const UiScriptHost& host) {
  if (!loaded_) return;
  if (dragMove_) {
    pushMotion(pendingDragX_, pendingDragY_, 0, 0);
    dragMove_ = false;
    releaseMouse_ = true;
    return;
  }
  if (releaseMouse_) {  // a click spans two frames, like a real one
    pushButton(mouseX_, mouseY_, false);
    releaseMouse_ = false;
    return;
  }
  if (heldKey_ >= 0) {
    if (--holdFrames_ > 0) {
      pushKey(heldKey_, true, true);  // what the OS sends for a held key
      return;
    }
    pushKey(heldKey_, false);
    heldKey_ = -1;
    return;
  }
  if (waitFrames_ > 0) {
    --waitFrames_;
    return;
  }
  if (next_ >= lines_.size()) {
    host.quit();
    return;
  }
  std::istringstream in(lines_[next_++]);
  std::string cmd;
  in >> cmd;
  auto scancode = [&](const std::string& name) {
    const SDL_Scancode sc = SDL_GetScancodeFromName(name.c_str());
    if (sc == SDL_SCANCODE_UNKNOWN) fail("unknown key '" + name + "'");
    return sc == SDL_SCANCODE_UNKNOWN ? -1 : int(sc);
  };
  if (cmd == "key") {
    heldKey_ = scancode(rest(in));
    holdFrames_ = 1;
    if (heldKey_ >= 0) pushKey(heldKey_, true);
  } else if (cmd == "hold") {
    int frames = 1;
    in >> frames;
    heldKey_ = scancode(rest(in));
    holdFrames_ = std::max(1, frames);
    if (heldKey_ >= 0) pushKey(heldKey_, true);
  } else if (cmd == "move" || cmd == "click") {
    float x = 0, y = 0;
    in >> x >> y;
    pushMotion(x, y, x - mouseX_, y - mouseY_);
    mouseX_ = x;
    mouseY_ = y;
    if (cmd == "click") {
      pushButton(x, y, true);
      releaseMouse_ = true;
    }
  } else if (cmd == "drag") {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    in >> x0 >> y0 >> x1 >> y1;
    pushMotion(x0, y0, x0 - mouseX_, y0 - mouseY_);
    pushButton(x0, y0, true);
    pendingDragX_ = x1;
    pendingDragY_ = y1;
    dragMove_ = true;
    mouseX_ = x1;
    mouseY_ = y1;
  } else if (cmd == "wheel") {
    float ticks = 0;
    in >> ticks;
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_WHEEL;
    e.wheel.y = ticks;
    SDL_PushEvent(&e);
  } else if (cmd == "wait") {
    in >> waitFrames_;
  } else if (cmd == "shot") {
    host.screenshot(rest(in));
  } else if (cmd == "expect-binding") {
    std::string action;
    in >> action;
    const std::string want = rest(in);
    const auto keys = host.bindingNames(action);
    if (std::find(keys.begin(), keys.end(), want) == keys.end())
      fail(std::format("{} is not bound to '{}' (has {} key(s), first '{}')", action, want, keys.size(),
                       keys.empty() ? std::string() : keys.front()));
  } else if (cmd == "expect-screen") {
    const std::string want = rest(in), got = host.screenName();
    if (want != got) fail("screen is '" + got + "'");
  } else if (cmd == "expect-terminal") {
    const std::string want = rest(in);
    if ((want == "open") != host.terminalOpen())
      fail(std::string("terminal is ") + (host.terminalOpen() ? "open" : "closed"));
  } else if (cmd == "quit") {
    next_ = lines_.size();
    host.quit();
  } else {
    fail("unknown command");
  }
}

}  // namespace fh
