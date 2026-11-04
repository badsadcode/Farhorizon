#include "game/content/scene_def.h"

#include "engine/core/file_io.h"
#include "game/content/json_util.h"

namespace fh {

namespace {
std::vector<std::string> parseServices(content::Reader& r, const nlohmann::json& j, const std::string& owner) {
  std::vector<std::string> out;
  if (!j.contains("services")) return out;
  if (!j["services"].is_array()) {
    r.error(owner + ": 'services' must be an array");
    return out;
  }
  for (const auto& v : j["services"]) {
    const std::string s = v.is_string() ? v.get<std::string>() : "";
    if (s != "repair" && s != "upgrades" && s != "contracts") r.error(owner + ": unknown service '" + s + "'");
    else out.push_back(s);
  }
  return out;
}
}  // namespace

namespace {
std::optional<std::string> readAsset(const AssetManifest& manifest, const AssetID& id, const char* type,
                                     std::vector<std::string>* errors) {
  auto path = manifest.resolve(id, type);
  if (!path) {
    if (errors) errors->push_back(std::string(type) + " '" + id + "': not in manifest or wrong type");
    return std::nullopt;
  }
  auto text = readTextFile(*path);
  if (!text && errors) errors->push_back(std::string(type) + " '" + id + "': cannot read " + path->string());
  return text;
}
}  // namespace

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
  s.startingCredits = static_cast<std::int64_t>(r.num(j, "startingCredits", false, 500.0));
  s.insuranceDeductible = static_cast<std::int64_t>(r.num(j, "insuranceDeductible", false, 250.0));

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
    s.ambient.fillDirection = normalize(r.vec3(j["ambient"], "fillDirection", false, s.ambient.fillDirection));
    s.ambient.fillColor = r.vec3(j["ambient"], "fillColor", false, s.ambient.fillColor);
    s.ambient.rimColor = r.vec3(j["ambient"], "rimColor", false, s.ambient.rimColor);
    s.ambient.rimPower = float(r.num(j["ambient"], "rimPower", false, s.ambient.rimPower));
  }
  if (j.contains("sky")) {
    const auto& k = j["sky"];
    s.sky.baseColor = r.vec3(k, "baseColor", false, s.sky.baseColor);
    s.sky.bandColor = r.vec3(k, "bandColor", false, s.sky.bandColor);
    s.sky.bandIntensity = float(r.num(k, "bandIntensity", false, s.sky.bandIntensity));
    s.sky.nebulaColor = r.vec3(k, "nebulaColor", false, s.sky.nebulaColor);
    s.sky.nebulaStrength = float(r.num(k, "nebulaStrength", false, s.sky.nebulaStrength));
  }
  if (j.contains("floatingOrigin"))
    s.rebaseDistance = r.num(j["floatingOrigin"], "rebaseDistance", false, s.rebaseDistance);
  if (s.rebaseDistance < 100.0 || s.rebaseDistance > 100000.0)
    r.error("floatingOrigin.rebaseDistance must be within [100, 100000] metres");

  if (j.contains("camera")) {
    const auto& c = j["camera"];
    s.camera.initialMode = r.str(c, "initialMode", false, s.camera.initialMode);
    s.camera.fovDeg = float(r.num(c, "fovDeg", false, s.camera.fovDeg));
    s.camera.orbitDistance = float(r.num(c, "orbitDistance", false, s.camera.orbitDistance));
    s.camera.yawDeg = float(r.num(c, "yawDeg", false, s.camera.yawDeg));
    s.camera.pitchDeg = float(r.num(c, "pitchDeg", false, s.camera.pitchDeg));
    s.camera.autoRotateDegPerSec = float(r.num(c, "autoRotateDegPerSec", false, s.camera.autoRotateDegPerSec));
    const auto& m = s.camera.initialMode;
    if (m != "chase" && m != "cockpit" && m != "orbit" && m != "free")
      r.error("camera.initialMode must be chase, cockpit, orbit or free");
  }

  if (j.contains("player") && j["player"].is_object()) {
    const auto& pl = j["player"];
    s.player.ship = r.str(pl, "ship");
    s.player.position.metres = r.dvec3(pl, "systemPosition");
    s.player.rotationDeg = r.vec3(pl, "rotationDeg", false);
  } else {
    r.error("missing object 'player'");
  }

  if (j.contains("props")) {
    if (!j["props"].is_array()) r.error("'props' must be an array");
    else
      for (const auto& pj : j["props"]) {
        PropDef p;
        p.id = r.str(pj, "id");
        p.name = r.str(pj, "name");
        p.mesh = r.str(pj, "mesh");
        p.scale = float(r.num(pj, "scale", false, 1.0));
        p.position.metres = r.dvec3(pj, "systemPosition");
        p.rotationDeg = r.vec3(pj, "rotationDeg", false);
        p.spinAxis = normalize(r.vec3(pj, "spinAxis", false, {0, 1, 0}));
        p.spinDegPerSec = float(r.num(pj, "spinDegPerSec", false, 0.0));
        p.collisionRadius = float(r.num(pj, "collisionRadius", false, 0.0));
        p.targetable = pj.value("targetable", true);
        p.marker = r.str(pj, "marker", false, p.marker);
        p.materialOverrides = r.materialOverrides(pj);
        p.lights = r.lights(pj);
        if (pj.contains("colliders")) {
          if (!pj["colliders"].is_array()) r.error("prop '" + p.id + "': 'colliders' must be an array");
          else
            for (const auto& cj : pj["colliders"]) {
              BoxColliderDef b;
              b.center = r.vec3(cj, "center");
              b.halfExtents = r.vec3(cj, "halfExtents");
              b.rotationDeg = r.vec3(cj, "rotationDeg", false);
              if (b.halfExtents.x <= 0 || b.halfExtents.y <= 0 || b.halfExtents.z <= 0)
                r.error("prop '" + p.id + "': collider halfExtents must be > 0");
              p.colliders.push_back(b);
            }
        }
        if (pj.contains("docking")) {
          const auto& dj = pj["docking"];
          DockingDef d;
          d.interior = r.str(dj, "interior");
          d.market = r.str(dj, "market", false);
          d.clearanceRange = float(r.num(dj, "clearanceRange", false, d.clearanceRange));
          d.captureDistance = float(r.num(dj, "captureDistance", false, d.captureDistance));
          d.captureSpeed = float(r.num(dj, "captureSpeed", false, d.captureSpeed));
          d.captureAngleDeg = float(r.num(dj, "captureAngleDeg", false, d.captureAngleDeg));
          d.services = parseServices(r, dj, "docking");
          d.approachHeight = float(r.num(dj, "approachHeight", false, d.approachHeight));
          if (dj.contains("bays") && dj["bays"].is_array())
            for (const auto& bj : dj["bays"]) {
              DockingBayDef b;
              b.id = r.str(bj, "id");
              b.name = r.str(bj, "name");
              b.position = r.vec3(bj, "position");
              b.rotationDeg = r.vec3(bj, "rotationDeg", false);
              d.bays.push_back(b);
            }
          if (d.bays.empty()) r.error("prop '" + p.id + "': docking needs at least one bay");
          p.docking = d;
        }
        if (p.scale <= 0) r.error("prop '" + p.id + "': scale must be > 0");
        s.props.push_back(p);
      }
  }

  if (j.contains("asteroidFields")) {
    if (!j["asteroidFields"].is_array()) r.error("'asteroidFields' must be an array");
    else
      for (const auto& fj : j["asteroidFields"]) {
        AsteroidFieldDef f;
        f.id = r.str(fj, "id");
        f.name = r.str(fj, "name");
        f.center.metres = r.dvec3(fj, "systemPosition");
        f.seed = static_cast<std::uint32_t>(r.num(fj, "seed"));
        f.count = static_cast<std::uint32_t>(r.num(fj, "count", false, f.count));
        f.radius = float(r.num(fj, "radius", false, f.radius));
        f.thickness = float(r.num(fj, "thickness", false, f.thickness));
        f.clusters = static_cast<std::uint32_t>(r.num(fj, "clusters", false, f.clusters));
        f.minScale = float(r.num(fj, "minScale", false, f.minScale));
        f.maxScale = float(r.num(fj, "maxScale", false, f.maxScale));
        f.variants = static_cast<std::uint32_t>(r.num(fj, "variants", false, f.variants));
        f.detail = int(r.num(fj, "detail", false, f.detail));
        f.roughness = float(r.num(fj, "roughness", false, f.roughness));
        f.color = r.vec3(fj, "color", false, f.color);
        if (fj.contains("resources") && fj["resources"].is_array())
          for (const auto& rj : fj["resources"])
            f.resources.push_back({r.str(rj, "commodity"), float(r.num(rj, "weight", false, 1.0))});
        f.yieldPerMetre = float(r.num(fj, "yieldPerMetre", false, f.yieldPerMetre));
        f.hardnessMin = float(r.num(fj, "hardnessMin", false, f.hardnessMin));
        f.hardnessMax = float(r.num(fj, "hardnessMax", false, f.hardnessMax));
        if (f.hardnessMin <= 0 || f.hardnessMax < f.hardnessMin) r.error("asteroid field '" + f.id + "': bad hardness");
        if (f.count > 20000) r.error("asteroid field '" + f.id + "': count must be <= 20000");
        if (f.variants < 1 || f.variants > 16) r.error("asteroid field '" + f.id + "': variants must be 1..16");
        if (f.detail < 0 || f.detail > 4) r.error("asteroid field '" + f.id + "': detail must be 0..4");
        if (f.minScale <= 0 || f.maxScale < f.minScale) r.error("asteroid field '" + f.id + "': bad scale range");
        s.asteroidFields.push_back(f);
      }
  }
  if (j.contains("planets") && j["planets"].is_array()) {
    for (const auto& pj : j["planets"]) {
      PlanetDef p;
      p.id = r.str(pj, "id");
      p.name = r.str(pj, "name");
      p.center.metres = r.dvec3(pj, "systemPosition");
      p.radius = r.num(pj, "radius");
      p.colorA = r.vec3(pj, "colorA", false, p.colorA);
      p.colorB = r.vec3(pj, "colorB", false, p.colorB);
      p.seed = float(r.num(pj, "seed", false, 1.0));
      p.craters = float(r.num(pj, "craters", false, p.craters));
      p.atmosphereColor = r.vec3(pj, "atmosphereColor", false, p.atmosphereColor);
      p.atmosphereThickness = float(r.num(pj, "atmosphereThickness", false, 0.0));
      p.orbitAltitude = r.num(pj, "orbitAltitude", false, p.orbitAltitude);
      if (p.radius <= 1000.0) r.error("planet '" + p.id + "': radius must be > 1 km");
      if (pj.contains("landingSites") && pj["landingSites"].is_array())
        for (const auto& sj : pj["landingSites"]) {
          LandingSiteDef site;
          site.id = r.str(sj, "id");
          site.name = r.str(sj, "name");
          site.latDeg = float(r.num(sj, "latDeg"));
          site.lonDeg = float(r.num(sj, "lonDeg"));
          site.interior = r.str(sj, "interior");
          site.market = r.str(sj, "market", false);
          site.services = parseServices(r, sj, "site '" + site.id + "'");
          p.sites.push_back(site);
        }
      s.planets.push_back(p);
    }
  }
  if (j.contains("encounters") && j["encounters"].is_array())
    for (const auto& ej : j["encounters"]) {
      EncounterDef e;
      e.id = r.str(ej, "id");
      e.ship = r.str(ej, "ship");
      e.center.metres = r.dvec3(ej, "systemPosition");
      e.radius = float(r.num(ej, "radius", false, e.radius));
      e.triggerRange = float(r.num(ej, "triggerRange", false, e.triggerRange));
      e.spawnMinDistance = float(r.num(ej, "spawnMinDistance", false, e.spawnMinDistance));
      e.count = int(r.num(ej, "count", false, e.count));
      e.respawnSeconds = float(r.num(ej, "respawnSeconds", false, e.respawnSeconds));
      if (e.count < 0 || e.count > 8) r.error("encounter '" + e.id + "': count must be 0..8");
      if (e.triggerRange <= e.radius) r.error("encounter '" + e.id + "': triggerRange must exceed radius");
      s.encounters.push_back(e);
    }
  if (j.contains("traffic") && j["traffic"].is_array())
    for (const auto& tj : j["traffic"]) {
      TrafficDef t;
      t.id = r.str(tj, "id");
      t.ship = r.str(tj, "ship");
      if (tj.contains("waypoints") && tj["waypoints"].is_array())
        for (const auto& wj : tj["waypoints"]) {
          if (!wj.is_array() || wj.size() != 3) {
            r.error("traffic '" + t.id + "': waypoints are [x,y,z] arrays");
            continue;
          }
          t.waypoints.push_back({DVec3{wj[0].get<double>(), wj[1].get<double>(), wj[2].get<double>()}});
        }
      if (t.waypoints.size() < 2) r.error("traffic '" + t.id + "': needs at least two waypoints");
      t.count = int(r.num(tj, "count", false, t.count));
      t.triggerRange = float(r.num(tj, "triggerRange", false, t.triggerRange));
      t.respawnSeconds = float(r.num(tj, "respawnSeconds", false, t.respawnSeconds));
      s.traffic.push_back(t);
    }
  if (!r.ok()) return std::nullopt;
  return s;
}

std::optional<SceneDef> loadSceneDef(const AssetManifest& manifest, const AssetID& id,
                                     std::vector<std::string>* errors) {
  auto text = readAsset(manifest, id, "scene", errors);
  if (!text) return std::nullopt;
  auto scene = parseSceneDef(*text, "scene '" + id + "'", errors);
  if (!scene) return std::nullopt;
  bool ok = true;
  auto need = [&](const AssetID& ref, const char* type, const std::string& who) {
    const AssetEntry* e = manifest.find(ref);
    if (!e || e->type != type) {
      if (errors) errors->push_back("scene '" + id + "': " + who + " references unknown " + type + " '" + ref + "'");
      ok = false;
    }
  };
  need(scene->player.ship, "ship", "player");
  for (const auto& p : scene->props) {
    need(p.mesh, "mesh", "prop '" + p.id + "'");
    if (p.docking) need(p.docking->interior, "interior", "prop '" + p.id + "' docking");
    if (p.docking && !p.docking->market.empty()) need(p.docking->market, "market", "prop '" + p.id + "' docking");
  }
  for (const auto& pl : scene->planets)
    for (const auto& site : pl.sites) {
      need(site.interior, "interior", "site '" + site.id + "'");
      if (!site.market.empty()) need(site.market, "market", "site '" + site.id + "'");
    }
  for (const auto& e : scene->encounters) need(e.ship, "ship", "encounter '" + e.id + "'");
  for (const auto& t : scene->traffic) need(t.ship, "ship", "traffic '" + t.id + "'");
  if (!ok) return std::nullopt;
  return scene;
}

std::optional<BenchmarkDef> loadBenchmarkDef(const AssetManifest& manifest, const AssetID& id,
                                             std::vector<std::string>* errors) {
  auto text = readAsset(manifest, id, "benchmark", errors);
  if (!text) return std::nullopt;
  content::Reader r("benchmark '" + id + "'", errors);
  auto j = nlohmann::json::parse(*text, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    r.error("invalid JSON");
    return std::nullopt;
  }
  BenchmarkDef b;
  b.id = r.str(j, "id");
  b.scene = r.str(j, "scene");
  b.durationSeconds = float(r.num(j, "durationSeconds", false, b.durationSeconds));
  b.camera = r.str(j, "camera", false, b.camera);
  if (j.contains("player")) {
    PlayerStartDef p;
    p.position.metres = r.dvec3(j["player"], "systemPosition");
    p.rotationDeg = r.vec3(j["player"], "rotationDeg", false);
    b.playerOverride = p;
  }
  if (j.contains("autofly")) {
    b.throttle = float(r.num(j["autofly"], "throttle", false, 0.0));
    b.rotation = r.vec3(j["autofly"], "rotation", false);
  }
  b.startIn = r.str(j, "startIn", false);
  b.autoDock = j.value("autoDock", false);
  b.mine = j.value("mine", false);
  b.startLanded = j.value("startLanded", false);
  b.autoLand = j.value("autoLand", false);
  b.combat = j.value("combat", false);
  if (!b.startIn.empty() && b.startIn != "ship" && b.startIn != "station" && b.startIn != "surface") b.startIn = "?";
  b.lookTurnDegPerSec = float(r.num(j, "lookTurnDegPerSec", false, 0.0));
  if (b.startIn == "?") r.error("startIn must be ship, station or surface");
  if (b.durationSeconds <= 0) r.error("durationSeconds must be > 0");
  if (!r.ok()) return std::nullopt;
  return b;
}

}  // namespace fh
