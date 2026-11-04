#include "game/content/scene_def.h"

#include "engine/core/file_io.h"
#include "game/content/json_util.h"

namespace fh {

std::optional<SceneDef> parseSceneDef(const std::string& json, const std::string& context,
                                      std::vector<std::string>* errors) {
  content::Reader r(context, errors);
  auto j = nlohmann::json::parse(json, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    r.error("invalid JSON");
    return std::nullopt;
  }
  SceneDef s;
  s.id = r.str(j, "id");
  s.name = r.str(j, "name");
  s.exposure = float(r.num(j, "exposure", false, 1.0));

  if (j.contains("star")) {
    const auto& st = j["star"];
    s.star.position.metres = r.dvec3(st, "systemPosition", false);
    s.star.color = r.vec3(st, "color", false, s.star.color);
    s.star.intensity = float(r.num(st, "intensity", false, s.star.intensity));
    s.star.angularDiameterDeg = float(r.num(st, "angularDiameterDeg", false, s.star.angularDiameterDeg));
  } else {
    r.error("missing object 'star'");
  }
  if (j.contains("starfield")) {
    const auto& sf = j["starfield"];
    s.starfield.seed = static_cast<std::uint32_t>(r.num(sf, "seed"));
    s.starfield.count = static_cast<std::uint32_t>(r.num(sf, "count", false, s.starfield.count));
    s.starfield.brightness = float(r.num(sf, "brightness", false, s.starfield.brightness));
    s.starfield.galacticPlaneTiltDeg =
        float(r.num(sf, "galacticPlaneTiltDeg", false, s.starfield.galacticPlaneTiltDeg));
    if (s.starfield.count > 200000) r.error("starfield.count must be <= 200000");
  }
  if (j.contains("ambient")) {
    s.ambient.skyColor = r.vec3(j["ambient"], "skyColor", false, s.ambient.skyColor);
    s.ambient.groundColor = r.vec3(j["ambient"], "groundColor", false, s.ambient.groundColor);
  }
  if (j.contains("floatingOrigin"))
    s.rebaseDistance = r.num(j["floatingOrigin"], "rebaseDistance", false, s.rebaseDistance);
  if (j.contains("camera")) {
    const auto& c = j["camera"];
    s.camera.fovDeg = float(r.num(c, "fovDeg", false, s.camera.fovDeg));
    s.camera.orbitDistance = float(r.num(c, "orbitDistance", false, s.camera.orbitDistance));
    s.camera.yawDeg = float(r.num(c, "yawDeg", false, s.camera.yawDeg));
    s.camera.pitchDeg = float(r.num(c, "pitchDeg", false, s.camera.pitchDeg));
    s.camera.autoRotateDegPerSec = float(r.num(c, "autoRotateDegPerSec", false, s.camera.autoRotateDegPerSec));
  }
  if (j.contains("objects")) {
    if (!j["objects"].is_array()) {
      r.error("'objects' must be an array");
    } else {
      for (const auto& o : j["objects"]) {
        SceneObjectDef od;
        od.ship = r.str(o, "ship");
        od.position.metres = r.dvec3(o, "systemPosition");
        od.rotationDeg = r.vec3(o, "rotationDeg", false);
        s.objects.push_back(od);
      }
    }
  }
  if (!r.ok()) return std::nullopt;
  return s;
}

std::optional<SceneDef> loadSceneDef(const AssetManifest& manifest, const AssetID& id,
                                     std::vector<std::string>* errors) {
  auto path = manifest.resolve(id, "scene");
  if (!path) {
    if (errors) errors->push_back("scene '" + id + "': not in manifest or wrong type");
    return std::nullopt;
  }
  auto text = readTextFile(*path);
  if (!text) {
    if (errors) errors->push_back("scene '" + id + "': cannot read " + path->string());
    return std::nullopt;
  }
  return parseSceneDef(*text, "scene '" + id + "'", errors);
}

}  // namespace fh
