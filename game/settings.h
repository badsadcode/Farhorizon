#pragma once
// Player settings (Options menu), stored in `settings.json` beside the game - separate from
// content and saves. Unknown or invalid values fall back to defaults (never fatal).
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fh {

struct Settings {
  // Graphics.
  std::string look;             // visual profile id; empty = content default
  bool vsync = true;
  bool fullscreen = false;
  float fovOffsetDeg = 0.0f;    // added to the ship cameras' field of view (-15..+20)
  float uiScale = 1.0f;         // 0.75..1.5, on top of the resolution scale
  bool cameraShake = true;
  bool shadows = true;
  bool ambientOcclusion = true;
  bool reflections = true;
  bool volumetricClouds = true;
  // Audio (0..1).
  float masterVolume = 0.9f;
  float ambienceVolume = 1.0f;
  float effectsVolume = 1.0f;
  float engineVolume = 1.0f;
  float uiVolume = 1.0f;
  // Controls.
  float mouseSensitivity = 1.0f;  // multiplier on input.json's sensitivity (0.25..3)
  bool invertY = false;
  std::map<std::string, std::vector<std::string>> bindings;  // action -> key names (overrides input.json)
  // Gameplay / HUD.
  bool showDevOverlay = false;
  bool showContractTracker = true;
  bool showHints = true;

  void clamp();
};

std::optional<Settings> parseSettings(const std::string& json, std::string* error);
std::string serializeSettings(const Settings& s);
Settings loadSettings(const std::filesystem::path& path);  // defaults when missing/corrupt
bool saveSettings(const std::filesystem::path& path, const Settings& s, std::string* error);

}  // namespace fh
