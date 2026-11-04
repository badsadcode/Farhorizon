#pragma once
// Action-based input. Bindings come from content (config/input.json), never from code.
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

union SDL_Event;

namespace fh {

struct MouseSettings {
  float sensitivity = 0.0022f;  // virtual-stick units per pixel of mouse motion
  bool invertY = false;
  float deadzone = 0.04f;       // fraction of full deflection
  float recenterPerSecond = 0.0f;  // 0 = stick stays where you leave it
};

class InputState {
 public:
  // Loads action -> key bindings. Unknown key names are reported and skipped.
  bool loadBindings(const std::filesystem::path& jsonPath, std::vector<std::string>* errors);

  bool down(const std::string& action) const;
  bool pressed(const std::string& action) const;  // went down this frame
  bool pressedOrRepeat(const std::string& action) const;  // pressed, or OS key repeat (menus)

  // Runtime rebinding (Options menu). Key names are SDL scancode names ("W", "Left Shift").
  bool setBinding(const std::string& action, const std::vector<std::string>& keyNames);
  std::vector<std::string> bindingNames(const std::string& action) const;
  int firstKeyPressed() const;                 // scancode pressed this frame, or -1
  static std::string keyName(int scancode);
  // Player multipliers on top of input.json's mouse settings.
  void setMouseOverrides(float sensitivityScale, bool invertY);
  bool mouseLeftReleased() const { return mouseLeftReleased_; }

  const MouseSettings& mouse() const { return mouse_; }
  float mouseDx() const { return mouseDx_; }
  float mouseDy() const { return mouseDy_; }
  float wheel() const { return wheel_; }
  bool mouseLeft() const { return mouseButtons_ & 1u; }
  bool mouseLeftPressed() const { return mouseLeftPressed_; }  // went down this frame
  float mouseX() const { return mouseX_; }  // window coordinates (multiply by pixel scale)
  float mouseY() const { return mouseY_; }
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
  bool mouseLeftPressed_ = false, mouseLeftReleased_ = false;
  std::vector<bool> keysRepeated_ = std::vector<bool>(512, false);
  MouseSettings baseMouse_;
  float sensitivityScale_ = 1.0f;
  bool invertOverride_ = false;
  float mouseX_ = 0, mouseY_ = 0;
  MouseSettings mouse_;
};

}  // namespace fh
