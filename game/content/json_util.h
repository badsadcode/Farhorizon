#pragma once
// Small helpers for reading content JSON with precise error messages.
#include <format>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/math/math.h"
#include "game/content/light_def.h"
#include "game/content/material_override.h"

namespace fh::content {

class Reader {
 public:
  Reader(std::string context, std::vector<std::string>* errors) : ctx_(std::move(context)), errors_(errors) {}

  void error(const std::string& msg) {
    ok_ = false;
    if (errors_) errors_->push_back(ctx_ + ": " + msg);
  }
  bool ok() const { return ok_; }

  std::string str(const nlohmann::json& j, const char* key, bool required = true, std::string def = {}) {
    if (!j.contains(key)) {
      if (required) error(std::format("missing string '{}'", key));
      return def;
    }
    if (!j[key].is_string()) {
      error(std::format("'{}' must be a string", key));
      return def;
    }
    return j[key].get<std::string>();
  }
  double num(const nlohmann::json& j, const char* key, bool required = true, double def = 0.0) {
    if (!j.contains(key)) {
      if (required) error(std::format("missing number '{}'", key));
      return def;
    }
    if (!j[key].is_number()) {
      error(std::format("'{}' must be a number", key));
      return def;
    }
    return j[key].get<double>();
  }
  DVec3 dvec3(const nlohmann::json& j, const char* key, bool required = true, DVec3 def = {}) {
    if (!j.contains(key)) {
      if (required) error(std::format("missing [x,y,z] '{}'", key));
      return def;
    }
    const auto& a = j[key];
    if (!a.is_array() || a.size() != 3 || !a[0].is_number() || !a[1].is_number() || !a[2].is_number()) {
      error(std::format("'{}' must be an array of 3 numbers", key));
      return def;
    }
    return {a[0].get<double>(), a[1].get<double>(), a[2].get<double>()};
  }
  Vec3 vec3(const nlohmann::json& j, const char* key, bool required = true, Vec3 def = {}) {
    return Vec3(dvec3(j, key, required, DVec3(def)));
  }

  // "lights": [ { position, color, size, blinkHz, phase, duty } ]
  std::vector<LightDef> lights(const nlohmann::json& j) {
    std::vector<LightDef> out;
    if (!j.contains("lights")) return out;
    if (!j["lights"].is_array()) {
      error("'lights' must be an array");
      return out;
    }
    for (const auto& l : j["lights"]) {
      LightDef d;
      d.position = vec3(l, "position");
      d.color = vec3(l, "color", false, d.color);
      d.size = float(num(l, "size", false, d.size));
      d.blinkHz = float(num(l, "blinkHz", false, d.blinkHz));
      d.phase = float(num(l, "phase", false, d.phase));
      d.duty = float(num(l, "duty", false, d.duty));
      if (d.size <= 0 || d.blinkHz < 0 || d.duty <= 0 || d.duty > 1) error("light: size > 0, blinkHz >= 0, duty in (0,1]");
      out.push_back(d);
    }
    return out;
  }

  // "materialOverrides": { "<material name>": { baseColor, metallic, roughness, emissive } }
  MaterialOverrides materialOverrides(const nlohmann::json& j) {
    MaterialOverrides out;
    if (!j.contains("materialOverrides")) return out;
    if (!j["materialOverrides"].is_object()) {
      error("'materialOverrides' must be an object keyed by material name");
      return out;
    }
    for (const auto& [matName, o] : j["materialOverrides"].items()) {
      if (!o.is_object()) {
        error("materialOverrides." + matName + " must be an object");
        continue;
      }
      MaterialOverride mo;
      if (o.contains("baseColor")) {
        const auto& c = o["baseColor"];
        bool ok = c.is_array() && (c.size() == 3 || c.size() == 4);
        for (size_t i = 0; ok && i < c.size(); ++i) ok = c[i].is_number();
        if (ok)
          mo.baseColor = Vec4(c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                              c.size() == 4 ? c[3].get<float>() : 1.0f);
        else
          error("materialOverrides." + matName + ".baseColor must be [r,g,b] or [r,g,b,a]");
      }
      if (o.contains("metallic")) mo.metallic = float(num(o, "metallic"));
      if (o.contains("roughness")) mo.roughness = float(num(o, "roughness"));
      if (o.contains("emissive")) mo.emissive = vec3(o, "emissive");
      out.emplace(matName, mo);
    }
    return out;
  }

 private:
  std::string ctx_;
  std::vector<std::string>* errors_;
  bool ok_ = true;
};

}  // namespace fh::content
