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
  if (j.contains("mouse") && j["mouse"].is_object()) {
    const auto& m = j["mouse"];
    MouseSettings ms;
    ms.sensitivity = m.value("sensitivity", ms.sensitivity);
    ms.invertY = m.value("invertY", ms.invertY);
    ms.deadzone = m.value("deadzone", ms.deadzone);
    ms.recenterPerSecond = m.value("recenterPerSecond", ms.recenterPerSecond);
    if (ms.sensitivity <= 0 || ms.deadzone < 0 || ms.deadzone >= 1) ok = fail("input mouse settings out of range");
    else baseMouse_ = ms;
  }
  setMouseOverrides(sensitivityScale_, invertOverride_);
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

bool InputState::pressedOrRepeat(const std::string& action) const {
  auto it = bindings_.find(action);
  if (it == bindings_.end()) return false;
  for (int sc : it->second)
    if (keysPressed_[size_t(sc)] || keysRepeated_[size_t(sc)]) return true;
  return false;
}

bool InputState::setBinding(const std::string& action, const std::vector<std::string>& keyNames) {
  std::vector<int> codes;
  for (const auto& n : keyNames) {
    const SDL_Scancode sc = SDL_GetScancodeFromName(n.c_str());
    if (sc == SDL_SCANCODE_UNKNOWN) return false;
    codes.push_back(int(sc));
  }
  bindings_[action] = codes;
  return true;
}

std::vector<std::string> InputState::bindingNames(const std::string& action) const {
  std::vector<std::string> out;
  if (auto it = bindings_.find(action); it != bindings_.end())
    for (int sc : it->second) out.push_back(keyName(sc));
  return out;
}

int InputState::firstKeyPressed() const {
  for (size_t i = 0; i < keysPressed_.size(); ++i)
    if (keysPressed_[i]) return int(i);
  return -1;
}

std::string InputState::keyName(int scancode) { return SDL_GetScancodeName(SDL_Scancode(scancode)); }

void InputState::setMouseOverrides(float sensitivityScale, bool invertY) {
  sensitivityScale_ = sensitivityScale;
  invertOverride_ = invertY;
  mouse_ = baseMouse_;
  mouse_.sensitivity *= sensitivityScale;
  mouse_.invertY = baseMouse_.invertY != invertY;
}

void InputState::beginFrame() {
  std::fill(keysPressed_.begin(), keysPressed_.end(), false);
  std::fill(keysRepeated_.begin(), keysRepeated_.end(), false);
  mouseDx_ = mouseDy_ = wheel_ = 0;
  mouseLeftPressed_ = mouseLeftReleased_ = false;
  typedText_ = std::move(injectedText_);
  injectedText_.clear();
}

bool InputState::scancodePressedOrRepeat(int scancode) const {
  return scancode >= 0 && size_t(scancode) < keysPressed_.size() && (keysPressed_[size_t(scancode)] || keysRepeated_[size_t(scancode)]);
}

void InputState::handleEvent(const SDL_Event& e) {
  switch (e.type) {
    case SDL_EVENT_TEXT_INPUT:
      if (e.text.text) typedText_ += e.text.text;
      break;
    case SDL_EVENT_KEY_DOWN:
      if (size_t(e.key.scancode) < keysDown_.size()) {
        if (!e.key.repeat) keysPressed_[size_t(e.key.scancode)] = true;
        else keysRepeated_[size_t(e.key.scancode)] = true;
        keysDown_[size_t(e.key.scancode)] = true;
      }
      break;
    case SDL_EVENT_KEY_UP:
      if (size_t(e.key.scancode) < keysDown_.size()) keysDown_[size_t(e.key.scancode)] = false;
      break;
    case SDL_EVENT_MOUSE_MOTION:
      mouseDx_ += e.motion.xrel;
      mouseDy_ += e.motion.yrel;
      mouseX_ = e.motion.x;
      mouseY_ = e.motion.y;
      break;
    case SDL_EVENT_MOUSE_WHEEL: wheel_ += e.wheel.y; break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      mouseButtons_ |= SDL_BUTTON_MASK(e.button.button);
      if (e.button.button == SDL_BUTTON_LEFT) mouseLeftPressed_ = true;
      mouseX_ = e.button.x;
      mouseY_ = e.button.y;
      break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
      mouseButtons_ &= ~SDL_BUTTON_MASK(e.button.button);
      if (e.button.button == SDL_BUTTON_LEFT) mouseLeftReleased_ = true;
      break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
      std::fill(keysDown_.begin(), keysDown_.end(), false);
      mouseButtons_ = 0;
      break;
    default: break;
  }
}

void InputState::endEvents() {}

}  // namespace fh
