#pragma once
// SDL3 window wrapper. Thread affinity: main thread only.
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

struct SDL_Window;

namespace fh {

class InputState;

class Window {
 public:
  Window(const std::string& title, int width, int height);
  ~Window();
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  // Pumps SDL events into `input`. Returns false once the user asked to close the window.
  bool pollEvents(InputState& input);
  void toggleFullscreen();
  bool isFullscreen() const;
  bool isMinimized() const;
  bool hasFocus() const;
  // Relative mouse mode: hides the cursor and reports unbounded motion (mouse steering).
  void setRelativeMouse(bool on);
  bool relativeMouse() const;

  // Drawable size in pixels (may differ from window size on high-DPI displays).
  void pixelSize(int& w, int& h) const;
  float pixelScale() const;  // drawable pixels per window coordinate (high-DPI)
  bool consumeResized();  // true once after any size change

  std::vector<const char*> requiredVulkanInstanceExtensions() const;
  VkSurfaceKHR createSurface(VkInstance instance) const;

  SDL_Window* handle() const { return window_; }

 private:
  SDL_Window* window_ = nullptr;
  bool resized_ = false;
};

}  // namespace fh
