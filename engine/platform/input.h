#pragma once
// Action-based input. Bindings come from content (config/input.json), never from code.
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

union SDL_Event;

namespace fh {

class InputState {
 public:
  // Loads action -> key bindings. Unknown key names are reported and skipped.
  bool loadBindings(const std::filesystem::path& jsonPath, std::vector<std::string>* errors);

  bool down(const std::string& action) const;
  bool pressed(const std::string& action) const;  // went down this frame

  float mouseDx() const { return mouseDx_; }
  float mouseDy() const { return mouseDy_; }
  float wheel() const { return wheel_; }
  bool mouseLeft() const { return mouseButtons_ & 1u; }
  bool mouseRight() const { return mouseButtons_ & 4u; }

  // Called by Window.
  void beginFrame();
  void handleEvent(const SDL_Event& e);
  void endEvents();

 private:
  std::unordered_map<std::string, std::vector<int>> bindings_;  // action -> SDL_Scancode list
  std::vector<bool> keysDown_ = std::vector<bool>(512, false);
  std::vector<bool> keysPressed_ = std::vector<bool>(512, false);
  float mouseDx_ = 0, mouseDy_ = 0, wheel_ = 0;
  unsigned mouseButtons_ = 0;
};

}  // namespace fh
