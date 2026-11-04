#include "game/content/visual_profile.h"

#include "engine/core/file_io.h"
#include "game/content/json_util.h"

namespace fh {

int VisualProfileSet::indexOf(const std::string& id) const {
  for (size_t i = 0; i < profiles.size(); ++i)
    if (profiles[i].id == id) return int(i);
  return -1;
}

std::optional<VisualProfileSet> parseVisualProfiles(const std::string& json, const std::string& context,
                                                    std::vector<std::string>* errors) {
  content::Reader r(context, errors);
  auto j = nlohmann::json::parse(json, nullptr, false);
  if (j.is_discarded() || !j.is_object() || !j.contains("profiles") || !j["profiles"].is_array()) {
    r.error("invalid JSON or missing 'profiles' array");
    return std::nullopt;
  }
  VisualProfileSet set;
  set.defaultId = r.str(j, "default");
  auto colour = [&](const nlohmann::json& o, const char* key, Vec4 def) {
    if (!o.contains(key)) return def;
    const auto& a = o[key];
    if (!a.is_array() || (a.size() != 3 && a.size() != 4)) {
      r.error(std::string("hud.") + key + " must be [r,g,b] or [r,g,b,a] (0..255)");
      return def;
    }
    for (const auto& v : a)
      if (!v.is_number()) {
        r.error(std::string("hud.") + key + " entries must be numbers");
        return def;
      }
    return Vec4(a[0].get<float>() / 255.0f, a[1].get<float>() / 255.0f, a[2].get<float>() / 255.0f,
                a.size() == 4 ? a[3].get<float>() / 255.0f : def.w);
  };
  for (const auto& pj : j["profiles"]) {
    VisualProfile p;
    p.id = r.str(pj, "id");
    p.name = r.str(pj, "name");
    p.bloomStrength = float(r.num(pj, "bloomStrength", false, p.bloomStrength));
    p.bloomThreshold = float(r.num(pj, "bloomThreshold", false, p.bloomThreshold));
    p.bloomKnee = float(r.num(pj, "bloomKnee", false, p.bloomKnee));
    p.bloomRadius = float(r.num(pj, "bloomRadius", false, p.bloomRadius));
    p.bloomIterations = int(r.num(pj, "bloomIterations", false, p.bloomIterations));
    p.shadowTint = r.vec3(pj, "shadowTint", false, p.shadowTint);
    p.highlightTint = r.vec3(pj, "highlightTint", false, p.highlightTint);
    p.saturation = float(r.num(pj, "saturation", false, p.saturation));
    p.contrast = float(r.num(pj, "contrast", false, p.contrast));
    p.vignette = float(r.num(pj, "vignette", false, p.vignette));
    p.scanlines = float(r.num(pj, "scanlines", false, p.scanlines));
    p.scanlinePeriod = float(r.num(pj, "scanlinePeriod", false, p.scanlinePeriod));
    p.chromaticAberration = float(r.num(pj, "chromaticAberration", false, p.chromaticAberration));
    p.grain = float(r.num(pj, "grain", false, p.grain));
    if (pj.contains("hud")) {
      const auto& h = pj["hud"];
      p.hud.primary = colour(h, "primary", p.hud.primary);
      p.hud.nav = colour(h, "nav", p.hud.nav);
      p.hud.field = colour(h, "field", p.hud.field);
      p.hud.warn = colour(h, "warn", p.hud.warn);
    }
    if (p.bloomStrength < 0 || p.bloomIterations < 1 || p.bloomIterations > 4)
      r.error("profile '" + p.id + "': bloomStrength must be >= 0 and bloomIterations 1..4");
    if (p.scanlines < 0 || p.scanlines > 1 || p.vignette < 0 || p.vignette > 1)
      r.error("profile '" + p.id + "': scanlines and vignette must be within [0,1]");
    set.profiles.push_back(p);
  }
  if (set.profiles.empty()) r.error("no profiles defined");
  if (r.ok() && set.indexOf(set.defaultId) < 0) r.error("default profile '" + set.defaultId + "' not found");
  if (!r.ok()) return std::nullopt;
  return set;
}

std::optional<VisualProfileSet> loadVisualProfiles(const AssetManifest& manifest, const AssetID& id,
                                                   std::vector<std::string>* errors) {
  auto path = manifest.resolve(id, "visuals");
  if (!path) {
    if (errors) errors->push_back("visuals '" + id + "': not in manifest or wrong type");
    return std::nullopt;
  }
  auto text = readTextFile(*path);
  if (!text) {
    if (errors) errors->push_back("visuals '" + id + "': cannot read " + path->string());
    return std::nullopt;
  }
  return parseVisualProfiles(*text, "visuals '" + id + "'", errors);
}

}  // namespace fh
