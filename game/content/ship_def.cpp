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
  d.meshScale = float(r.num(j, "meshScale", false, 1.0));
  d.meshRotationDeg = r.vec3(j, "meshRotationDeg", false);
  d.massTons = float(r.num(j, "massTons"));
  d.cargoCapacityTons = float(r.num(j, "cargoCapacityTons"));
  d.maxHull = float(r.num(j, "maxHull"));
  d.maxShield = float(r.num(j, "maxShield"));
  if (d.meshScale <= 0) r.error("'meshScale' must be > 0");
  if (d.massTons <= 0) r.error("'massTons' must be > 0");

  if (j.contains("flight") && j["flight"].is_object()) {
    const auto& f = j["flight"];
    d.flight.forwardAcceleration = float(r.num(f, "forwardAcceleration"));
    d.flight.lateralAcceleration = float(r.num(f, "lateralAcceleration"));
    d.flight.angularAcceleration = float(r.num(f, "angularAcceleration"));
    d.flight.maxCruiseSpeed = float(r.num(f, "maxCruiseSpeed"));
  } else {
    r.error("missing object 'flight'");
  }

  if (j.contains("mounts")) {
    if (!j["mounts"].is_array()) r.error("'mounts' must be an array of strings");
    else
      for (const auto& m : j["mounts"])
        if (m.is_string()) d.mounts.push_back(m.get<std::string>());
        else r.error("'mounts' entries must be strings");
  }

  if (j.contains("materialOverrides")) {
    if (!j["materialOverrides"].is_object()) {
      r.error("'materialOverrides' must be an object keyed by material name");
    } else {
      for (const auto& [matName, o] : j["materialOverrides"].items()) {
        if (!o.is_object()) {
          r.error("materialOverrides." + matName + " must be an object");
          continue;
        }
        MaterialOverride mo;
        if (o.contains("baseColor")) {
          const auto& c = o["baseColor"];
          if (c.is_array() && (c.size() == 3 || c.size() == 4)) {
            mo.baseColor = Vec4(c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                                c.size() == 4 ? c[3].get<float>() : 1.0f);
          } else {
            r.error("materialOverrides." + matName + ".baseColor must be [r,g,b] or [r,g,b,a]");
          }
        }
        if (o.contains("metallic")) mo.metallic = float(r.num(o, "metallic"));
        if (o.contains("roughness")) mo.roughness = float(r.num(o, "roughness"));
        if (o.contains("emissive")) mo.emissive = r.vec3(o, "emissive");
        d.materialOverrides.emplace(matName, mo);
      }
    }
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
  if (def && !manifest.find(def->mesh)) {
    if (errors) errors->push_back("ship '" + id + "': mesh asset '" + def->mesh + "' not in manifest");
    return std::nullopt;
  }
  return def;
}

}  // namespace fh
