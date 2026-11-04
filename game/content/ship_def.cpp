#include "game/content/ship_def.h"

#include "engine/core/file_io.h"
#include "game/content/json_util.h"

namespace fh {

std::optional<ShipDef> parseShipDef(const std::string& json, const std::string& context,
                                    std::vector<std::string>* errors) {
  content::Reader r(context, errors);
  auto j = nlohmann::json::parse(json, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    r.error("invalid JSON");
    return std::nullopt;
  }
  ShipDef d;
  d.id = r.str(j, "id");
  d.name = r.str(j, "name");
  d.mesh = r.str(j, "mesh");
  d.interior = r.str(j, "interior", false);
  d.meshScale = float(r.num(j, "meshScale", false, 1.0));
  d.meshRotationDeg = r.vec3(j, "meshRotationDeg", false);
  d.massTons = float(r.num(j, "massTons"));
  d.cargoCapacityTons = float(r.num(j, "cargoCapacityTons"));
  d.maxHull = float(r.num(j, "maxHull"));
  d.maxShield = float(r.num(j, "maxShield"));
  if (d.meshScale <= 0) r.error("'meshScale' must be > 0");
  if (d.massTons <= 0) r.error("'massTons' must be > 0");

  d.collisionRadius = float(r.num(j, "collisionRadius", false, 0.0));
  if (j.contains("flight") && j["flight"].is_object()) {
    const auto& f = j["flight"];
    FlightParams& fp = d.flight;
    fp.forwardAcceleration = float(r.num(f, "forwardAcceleration"));
    fp.lateralAcceleration = float(r.num(f, "lateralAcceleration"));
    fp.angularAcceleration = float(r.num(f, "angularAcceleration"));
    fp.maxCruiseSpeed = float(r.num(f, "maxCruiseSpeed"));
    fp.reverseAcceleration = float(r.num(f, "reverseAcceleration", false, fp.forwardAcceleration * 0.5));
    const Vec3 rates = r.vec3(f, "maxAngularRateDeg", false, {50.0f, 40.0f, 80.0f});
    fp.maxAngularRate = {radians(rates.x), radians(rates.y), radians(rates.z)};
    fp.strafeSpeedFraction = float(r.num(f, "strafeSpeedFraction", false, fp.strafeSpeedFraction));
    fp.maxReverseFraction = float(r.num(f, "maxReverseFraction", false, fp.maxReverseFraction));
    if (fp.forwardAcceleration <= 0 || fp.lateralAcceleration <= 0 || fp.angularAcceleration <= 0 ||
        fp.maxCruiseSpeed <= 0 || fp.reverseAcceleration <= 0)
      r.error("flight accelerations and maxCruiseSpeed must be > 0");
  } else {
    r.error("missing object 'flight'");
  }
  if (j.contains("precision") && j["precision"].is_object()) {
    const auto& pr = j["precision"];
    d.flight.precisionSpeedScale = float(r.num(pr, "speedScale", false, d.flight.precisionSpeedScale));
    d.flight.precisionAccelScale = float(r.num(pr, "accelerationScale", false, d.flight.precisionAccelScale));
    d.flight.precisionAngularScale = float(r.num(pr, "angularScale", false, d.flight.precisionAngularScale));
  }
  if (j.contains("boost") && j["boost"].is_object()) {
    const auto& b = j["boost"];
    d.flight.boostSpeedMultiplier = float(r.num(b, "speedMultiplier", false, d.flight.boostSpeedMultiplier));
    d.flight.boostAccelMultiplier = float(r.num(b, "accelerationMultiplier", false, d.flight.boostAccelMultiplier));
    d.flight.boostEnergyCapacity = float(r.num(b, "energyCapacity", false, d.flight.boostEnergyCapacity));
    d.flight.boostDrainPerSecond = float(r.num(b, "drainPerSecond", false, d.flight.boostDrainPerSecond));
    d.flight.boostRegenPerSecond = float(r.num(b, "regenPerSecond", false, d.flight.boostRegenPerSecond));
    d.flight.boostMinEnergyToStart = float(r.num(b, "minEnergyToStart", false, d.flight.boostMinEnergyToStart));
    if (d.flight.boostEnergyCapacity <= 0) r.error("boost.energyCapacity must be > 0");
  }
  if (j.contains("cameras") && j["cameras"].is_object()) {
    const auto& c = j["cameras"];
    if (c.contains("cockpit")) {
      d.cameras.cockpitSocket = r.str(c["cockpit"], "socket", false, d.cameras.cockpitSocket);
      d.cameras.cockpitFovDeg = float(r.num(c["cockpit"], "fovDeg", false, d.cameras.cockpitFovDeg));
    }
    if (c.contains("chase")) {
      const auto& ch = c["chase"];
      d.cameras.chaseDistance = float(r.num(ch, "distance", false, d.cameras.chaseDistance));
      d.cameras.chaseHeight = float(r.num(ch, "height", false, d.cameras.chaseHeight));
      d.cameras.chasePositionLag = float(r.num(ch, "positionLag", false, d.cameras.chasePositionLag));
      d.cameras.chaseRotationLag = float(r.num(ch, "rotationLag", false, d.cameras.chaseRotationLag));
      d.cameras.chaseFovDeg = float(r.num(ch, "fovDeg", false, d.cameras.chaseFovDeg));
    }
  }
  if (j.contains("sockets")) {
    if (!j["sockets"].is_object()) {
      r.error("'sockets' must be an object keyed by socket name");
    } else {
      for (const auto& [name, sj] : j["sockets"].items()) {
        SocketDef sd;
        sd.position = r.vec3(sj, "position");
        sd.rotationDeg = r.vec3(sj, "rotationDeg", false);
        d.sockets.emplace(name, sd);
      }
    }
  }

  if (j.contains("mounts")) {
    if (!j["mounts"].is_array()) r.error("'mounts' must be an array of strings");
    else
      for (const auto& m : j["mounts"])
        if (m.is_string()) d.mounts.push_back(m.get<std::string>());
        else r.error("'mounts' entries must be strings");
  }

  d.materialOverrides = r.materialOverrides(j);
  d.lights = r.lights(j);
  if (j.contains("engineGlow") && j["engineGlow"].is_object()) {
    const auto& g = j["engineGlow"];
    if (g.contains("sockets") && g["sockets"].is_array())
      for (const auto& sname : g["sockets"])
        if (sname.is_string()) d.engineGlow.sockets.push_back(sname.get<std::string>());
    d.engineGlow.color = r.vec3(g, "color", false, d.engineGlow.color);
    d.engineGlow.boostColor = r.vec3(g, "boostColor", false, d.engineGlow.boostColor);
    d.engineGlow.idleSize = float(r.num(g, "idleSize", false, d.engineGlow.idleSize));
    d.engineGlow.thrustSize = float(r.num(g, "thrustSize", false, d.engineGlow.thrustSize));
    d.engineGlow.idleIntensity = float(r.num(g, "idleIntensity", false, d.engineGlow.idleIntensity));
    d.engineGlow.thrustIntensity = float(r.num(g, "thrustIntensity", false, d.engineGlow.thrustIntensity));
  }
  if (!r.ok()) return std::nullopt;
  return d;
}

std::optional<ShipDef> loadShipDef(const AssetManifest& manifest, const AssetID& id,
                                   std::vector<std::string>* errors) {
  auto path = manifest.resolve(id, "ship");
  if (!path) {
    if (errors) errors->push_back("ship '" + id + "': not in manifest or wrong type");
    return std::nullopt;
  }
  auto text = readTextFile(*path);
  if (!text) {
    if (errors) errors->push_back("ship '" + id + "': cannot read " + path->string());
    return std::nullopt;
  }
  auto def = parseShipDef(*text, "ship '" + id + "'", errors);
  if (def && !def->interior.empty() && (!manifest.find(def->interior) || manifest.find(def->interior)->type != "interior")) {
    if (errors) errors->push_back("ship '" + id + "': interior '" + def->interior + "' not in manifest");
    return std::nullopt;
  }
  if (def && !manifest.find(def->mesh)) {
    if (errors) errors->push_back("ship '" + id + "': mesh asset '" + def->mesh + "' not in manifest");
    return std::nullopt;
  }
  return def;
}

}  // namespace fh
