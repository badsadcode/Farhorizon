#include "engine/platform/window.h"

#include <stdexcept>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "engine/core/log.h"
#include "engine/platform/input.h"

namespace fh {

Window::Window(const std::string& title, int width, int height) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
  window_ = SDL_CreateWindow(title.c_str(), width, height,
                             SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window_) throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
  log::info("platform", "SDL {}.{}.{} window {}x{} (video driver: {})", SDL_MAJOR_VERSION, SDL_MINOR_VERSION,
            SDL_MICRO_VERSION, width, height, SDL_GetCurrentVideoDriver());
}

Window::~Window() {
  if (window_) SDL_DestroyWindow(window_);
  SDL_Quit();
}

bool Window::pollEvents(InputState& input) {
  input.beginFrame();
  bool running = true;
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    switch (e.type) {
      case SDL_EVENT_QUIT:
      case SDL_EVENT_WINDOW_CLOSE_REQUESTED: running = false; break;
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
      case SDL_EVENT_WINDOW_RESIZED: resized_ = true; break;
      default: input.handleEvent(e); break;
    }
  }
  input.endEvents();
  return running;
}

void Window::toggleFullscreen() {
  const bool fs = isFullscreen();
  SDL_SetWindowFullscreen(window_, !fs);  // borderless desktop fullscreen by default in SDL3
  log::info("platform", "fullscreen {}", fs ? "off" : "on");
  resized_ = true;
}

bool Window::isFullscreen() const { return (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0; }
bool Window::isMinimized() const { return (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) != 0; }

bool Window::hasFocus() const { return (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) != 0; }
void Window::setRelativeMouse(bool on) { SDL_SetWindowRelativeMouseMode(window_, on); }
bool Window::relativeMouse() const { return SDL_GetWindowRelativeMouseMode(window_); }

void Window::pixelSize(int& w, int& h) const { SDL_GetWindowSizeInPixels(window_, &w, &h); }
float Window::pixelScale() const {
  int w = 0, h = 0, pw = 0, ph = 0;
  SDL_GetWindowSize(window_, &w, &h);
  SDL_GetWindowSizeInPixels(window_, &pw, &ph);
  return w > 0 ? float(pw) / float(w) : 1.0f;
}

bool Window::consumeResized() {
  const bool r = resized_;
  resized_ = false;
  return r;
}

std::vector<const char*> Window::requiredVulkanInstanceExtensions() const {
  Uint32 count = 0;
  const char* const* exts = SDL_Vulkan_GetInstanceExtensions(&count);
  if (!exts) throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions: ") + SDL_GetError());
  return {exts, exts + count};
}

VkSurfaceKHR Window::createSurface(VkInstance instance) const {
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (!SDL_Vulkan_CreateSurface(window_, instance, nullptr, &surface))
    throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface: ") + SDL_GetError());
  return surface;
}

}  // namespace fh
