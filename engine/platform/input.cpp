#include "engine/platform/input.h"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include "engine/core/file_io.h"
#include "engine/core/log.h"

namespace fh {

bool InputState::loadBindings(const std::filesystem::path& jsonPath, std::vector<std::string>* errors) {
  auto text = readTextFile(jsonPath);
  auto fail = [&](const std::string& m) {
    log::error("input", "{}", m);
    if (errors) errors->push_back(m);
    return false;
  };
  if (!text) return fail("input bindings missing: " + jsonPath.string());
  auto j = nlohmann::json::parse(*text, nullptr, false);
  if (j.is_discarded() || !j.contains("bindings") || !j["bindings"].is_object())
    return fail("input bindings: invalid JSON or missing 'bindings'");
  std::unordered_map<std::string, std::vector<int>> parsed;
  bool ok = true;
  for (auto& [action, keys] : j["bindings"].items()) {
    if (!keys.is_array()) {
      ok = fail("input action '" + action + "': expected array of key names");
      continue;
    }
    for (const auto& k : keys) {
      const std::string name = k.is_string() ? k.get<std::string>() : std::string{};
      const SDL_Scancode sc = SDL_GetScancodeFromName(name.c_str());
      if (sc == SDL_SCANCODE_UNKNOWN) {
        ok = fail("input action '" + action + "': unknown key name '" + name + "'");
        continue;
      }
      parsed[action].push_back(static_cast<int>(sc));
    }
  }
  bindings_ = std::move(parsed);
  log::info("input", "loaded {} action bindings", bindings_.size());
  return ok;
}

bool InputState::down(const std::string& action) const {
  auto it = bindings_.find(action);
  if (it == bindings_.end()) return false;
  for (int sc : it->second)
    if (keysDown_[size_t(sc)]) return true;
  return false;
}

bool InputState::pressed(const std::string& action) const {
  auto it = bindings_.find(action);
  if (it == bindings_.end()) return false;
  for (int sc : it->second)
    if (keysPressed_[size_t(sc)]) return true;
  return false;
}

void InputState::beginFrame() {
  std::fill(keysPressed_.begin(), keysPressed_.end(), false);
  mouseDx_ = mouseDy_ = wheel_ = 0;
}

void InputState::handleEvent(const SDL_Event& e) {
  switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
      if (size_t(e.key.scancode) < keysDown_.size()) {
        if (!e.key.repeat) keysPressed_[size_t(e.key.scancode)] = true;
        keysDown_[size_t(e.key.scancode)] = true;
      }
      break;
    case SDL_EVENT_KEY_UP:
      if (size_t(e.key.scancode) < keysDown_.size()) keysDown_[size_t(e.key.scancode)] = false;
      break;
    case SDL_EVENT_MOUSE_MOTION:
      mouseDx_ += e.motion.xrel;
      mouseDy_ += e.motion.yrel;
      break;
    case SDL_EVENT_MOUSE_WHEEL: wheel_ += e.wheel.y; break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: mouseButtons_ |= SDL_BUTTON_MASK(e.button.button); break;
    case SDL_EVENT_MOUSE_BUTTON_UP: mouseButtons_ &= ~SDL_BUTTON_MASK(e.button.button); break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
      std::fill(keysDown_.begin(), keysDown_.end(), false);
      mouseButtons_ = 0;
      break;
    default: break;
  }
}

void InputState::endEvents() {}

}  // namespace fh
