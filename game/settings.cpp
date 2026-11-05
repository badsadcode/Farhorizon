#include "game/settings.h"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "engine/core/file_io.h"
#include "engine/core/log.h"

namespace fh {

using nlohmann::json;

void Settings::clamp() {
  fovOffsetDeg = std::clamp(fovOffsetDeg, -15.0f, 20.0f);
  uiScale = std::clamp(uiScale, 0.75f, 1.5f);
  renderScale = std::clamp(renderScale, 0.5f, 1.0f);
  for (float* v : {&masterVolume, &ambienceVolume, &effectsVolume, &engineVolume, &uiVolume})
    *v = std::clamp(*v, 0.0f, 1.0f);
  mouseSensitivity = std::clamp(mouseSensitivity, 0.25f, 3.0f);
}

std::optional<Settings> parseSettings(const std::string& text, std::string* error) {
  json j = json::parse(text, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    if (error) *error = "settings file is not valid JSON";
    return std::nullopt;
  }
  Settings s;
  auto num = [&](const json& o, const char* k, float& v) {
    if (o.contains(k) && o[k].is_number()) v = o[k].get<float>();
  };
  auto flag = [&](const json& o, const char* k, bool& v) {
    if (o.contains(k) && o[k].is_boolean()) v = o[k].get<bool>();
  };
  if (auto g = j.value("graphics", json::object()); g.is_object()) {
    if (g.contains("look") && g["look"].is_string()) s.look = g["look"].get<std::string>();
    flag(g, "vsync", s.vsync);
    flag(g, "fullscreen", s.fullscreen);
    num(g, "fovOffsetDeg", s.fovOffsetDeg);
    num(g, "uiScale", s.uiScale);
    flag(g, "cameraShake", s.cameraShake);
    flag(g, "shadows", s.shadows);
    flag(g, "ambientOcclusion", s.ambientOcclusion);
    flag(g, "reflections", s.reflections);
    flag(g, "volumetricClouds", s.volumetricClouds);
    flag(g, "lowSpec", s.lowSpec);
    num(g, "renderScale", s.renderScale);
  }
  if (auto a = j.value("audio", json::object()); a.is_object()) {
    num(a, "master", s.masterVolume);
    num(a, "ambience", s.ambienceVolume);
    num(a, "effects", s.effectsVolume);
    num(a, "engine", s.engineVolume);
    num(a, "ui", s.uiVolume);
  }
  if (auto c = j.value("controls", json::object()); c.is_object()) {
    num(c, "mouseSensitivity", s.mouseSensitivity);
    flag(c, "invertY", s.invertY);
    if (c.contains("bindings") && c["bindings"].is_object())
      for (const auto& [action, keys] : c["bindings"].items()) {
        if (!keys.is_array()) continue;
        std::vector<std::string> names;
        for (const auto& k : keys)
          if (k.is_string()) names.push_back(k.get<std::string>());
        s.bindings[action] = names;
      }
  }
  if (auto n = j.value("network", json::object()); n.is_object()) {
    if (n.contains("joinAddress") && n["joinAddress"].is_string()) s.joinAddress = n["joinAddress"].get<std::string>();
    if (n.contains("joinName") && n["joinName"].is_string()) s.joinName = n["joinName"].get<std::string>();
  }
  if (auto h = j.value("gameplay", json::object()); h.is_object()) {
    flag(h, "showDevOverlay", s.showDevOverlay);
    flag(h, "showContractTracker", s.showContractTracker);
    flag(h, "showHints", s.showHints);
  }
  s.clamp();
  return s;
}

std::string serializeSettings(const Settings& s) {
  json j;
  j["schemaVersion"] = 1;
  j["graphics"] = {{"look", s.look},          {"vsync", s.vsync},     {"fullscreen", s.fullscreen},
                   {"fovOffsetDeg", s.fovOffsetDeg}, {"uiScale", s.uiScale}, {"cameraShake", s.cameraShake},
                   {"shadows", s.shadows},   {"ambientOcclusion", s.ambientOcclusion},
                   {"reflections", s.reflections}, {"volumetricClouds", s.volumetricClouds},
                   {"lowSpec", s.lowSpec},     {"renderScale", s.renderScale}};
  j["audio"] = {{"master", s.masterVolume},
                {"ambience", s.ambienceVolume},
                {"effects", s.effectsVolume},
                {"engine", s.engineVolume},
                {"ui", s.uiVolume}};
  json binds = json::object();
  for (const auto& [a, k] : s.bindings) binds[a] = k;
  j["controls"] = {{"mouseSensitivity", s.mouseSensitivity}, {"invertY", s.invertY}, {"bindings", binds}};
  j["network"] = {{"joinAddress", s.joinAddress}, {"joinName", s.joinName}};
  j["gameplay"] = {{"showDevOverlay", s.showDevOverlay},
                   {"showContractTracker", s.showContractTracker},
                   {"showHints", s.showHints}};
  return j.dump(2);
}

Settings loadSettings(const std::filesystem::path& path) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) return Settings{};  // first run
  auto text = readTextFile(path);
  if (!text) return Settings{};
  std::string err;
  if (auto s = parseSettings(*text, &err)) return *s;
  log::warn("settings", "{} ({}); using defaults", err, path.string());
  return Settings{};
}

bool saveSettings(const std::filesystem::path& path, const Settings& s, std::string* error) {
  const std::string text = serializeSettings(s);
  if (!writeFileAtomic(path, text.data(), text.size())) {
    if (error) *error = "could not write '" + path.string() + "'";
    return false;
  }
  return true;
}

}  // namespace fh
