#pragma once
// Small helpers for reading content JSON with precise error messages.
#include <format>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/math/math.h"

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

 private:
  std::string ctx_;
  std::vector<std::string>* errors_;
  bool ok_ = true;
};

}  // namespace fh::content
