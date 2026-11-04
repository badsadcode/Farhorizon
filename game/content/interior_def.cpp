#include "game/content/interior_def.h"

#include "engine/core/file_io.h"
#include "game/content/json_util.h"

namespace fh {

std::vector<Aabb> InteriorDef::buildColliders() const {
  std::vector<Aabb> out;
  for (const auto& piece : pieces) {
    auto it = templates.find(piece.templ);
    if (it == templates.end()) continue;
    for (const Aabb& c : it->second.colliders) {
      Aabb r = rotateAabbYaw90(c, piece.yawQuarterTurns);
      r.min = r.min * piece.scale + piece.position;
      r.max = r.max * piece.scale + piece.position;
      out.push_back(r);
    }
  }
  out.insert(out.end(), colliders.begin(), colliders.end());
  return out;
}

std::optional<InteriorDef> parseInteriorDef(const std::string& json, const std::string& context,
                                            std::vector<std::string>* errors) {
  content::Reader r(context, errors);
  auto j = nlohmann::json::parse(json, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    r.error("invalid JSON");
    return std::nullopt;
  }
  InteriorDef d;
  d.id = r.str(j, "id");
  d.name = r.str(j, "name");
  d.kind = r.str(j, "kind", false, d.kind);
  d.gravity = float(r.num(j, "gravity", false, d.gravity));
  d.showSky = j.value("showSky", false);
  d.materialOverrides = r.materialOverrides(j);
  d.lights = r.lights(j);
  if (d.kind != "ship" && d.kind != "station") r.error("kind must be 'ship' or 'station'");

  auto aabb = [&](const nlohmann::json& a) {
    Aabb b{r.vec3(a, "min"), r.vec3(a, "max")};
    if (b.min.x > b.max.x || b.min.y > b.max.y || b.min.z > b.max.z) r.error("collider min must be <= max");
    return b;
  };
  if (j.contains("lighting")) {
    const auto& l = j["lighting"];
    auto& L = d.lighting;
    L.keyDirection = normalize(r.vec3(l, "keyDirection", false, L.keyDirection));
    L.keyColor = r.vec3(l, "keyColor", false, L.keyColor);
    L.ambientSky = r.vec3(l, "ambientSky", false, L.ambientSky);
    L.ambientGround = r.vec3(l, "ambientGround", false, L.ambientGround);
    L.fillDirection = normalize(r.vec3(l, "fillDirection", false, L.fillDirection));
    L.fillColor = r.vec3(l, "fillColor", false, L.fillColor);
    L.rimColor = r.vec3(l, "rimColor", false, L.rimColor);
    L.rimPower = float(r.num(l, "rimPower", false, L.rimPower));
    L.exposure = float(r.num(l, "exposure", false, L.exposure));
  }
  if (j.contains("templates") && j["templates"].is_object()) {
    for (const auto& [name, t] : j["templates"].items()) {
      InteriorPieceTemplate pt;
      pt.mesh = r.str(t, "mesh");
      if (t.contains("colliders") && t["colliders"].is_array())
        for (const auto& c : t["colliders"]) pt.colliders.push_back(aabb(c));
      d.templates.emplace(name, pt);
    }
  }
  if (j.contains("pieces") && j["pieces"].is_array()) {
    for (const auto& pj : j["pieces"]) {
      InteriorPiece p;
      p.templ = r.str(pj, "template");
      p.position = r.vec3(pj, "position");
      const float yaw = float(r.num(pj, "yawDeg", false, 0.0));
      p.yawQuarterTurns = int(std::lround(yaw / 90.0f));
      if (std::fabs(yaw - float(p.yawQuarterTurns) * 90.0f) > 0.01f) r.error("piece yawDeg must be a multiple of 90");
      p.scale = float(r.num(pj, "scale", false, 1.0));
      if (!d.templates.count(p.templ)) r.error("piece uses unknown template '" + p.templ + "'");
      d.pieces.push_back(p);
    }
  }
  if (j.contains("colliders") && j["colliders"].is_array())
    for (const auto& c : j["colliders"]) d.colliders.push_back(aabb(c));
  if (j.contains("spawns") && j["spawns"].is_object())
    for (const auto& [name, sj] : j["spawns"].items())
      d.spawns[name] = {r.vec3(sj, "position"), float(r.num(sj, "yawDeg", false, 0.0))};
  if (j.contains("interactables") && j["interactables"].is_array()) {
    for (const auto& ij : j["interactables"]) {
      Interactable it;
      it.id = r.str(ij, "id");
      it.action = r.str(ij, "action");
      it.prompt = r.str(ij, "prompt");
      it.position = r.vec3(ij, "position");
      it.radius = float(r.num(ij, "radius", false, it.radius));
      if (it.action != "takeSeat" && it.action != "exitShip" && it.action != "boardShip" && it.action != "terminal")
        r.error("interactable '" + it.id + "': unknown action '" + it.action + "'");
      d.interactables.push_back(it);
    }
  }
  if (j.contains("parkedShip")) {
    d.parkedShip = InteriorSpawn{r.vec3(j["parkedShip"], "position"), float(r.num(j["parkedShip"], "yawDeg", false, 0))};
    d.parkedShipColliderHalfExtents = r.vec3(j["parkedShip"], "colliderHalfExtents", false);
  }
  // Required spawns per kind.
  const char* needed = d.kind == "ship" ? "seat" : "fromShip";
  if (!d.spawns.count(needed)) r.error(std::string("missing spawn '") + needed + "'");
  if (d.kind == "ship" && !d.spawns.count("airlock")) r.error("missing spawn 'airlock'");
  if (!r.ok()) return std::nullopt;
  return d;
}

std::optional<InteriorDef> loadInteriorDef(const AssetManifest& manifest, const AssetID& id,
                                           std::vector<std::string>* errors) {
  auto path = manifest.resolve(id, "interior");
  if (!path) {
    if (errors) errors->push_back("interior '" + id + "': not in manifest or wrong type");
    return std::nullopt;
  }
  auto text = readTextFile(*path);
  if (!text) {
    if (errors) errors->push_back("interior '" + id + "': cannot read " + path->string());
    return std::nullopt;
  }
  auto def = parseInteriorDef(*text, "interior '" + id + "'", errors);
  if (!def) return std::nullopt;
  bool ok = true;
  for (const auto& [name, t] : def->templates) {
    const AssetEntry* e = manifest.find(t.mesh);
    if (!e || e->type != "mesh") {
      if (errors) errors->push_back("interior '" + id + "': template '" + name + "' mesh '" + t.mesh + "' unknown");
      ok = false;
    }
  }
  if (!ok) return std::nullopt;
  return def;
}

}  // namespace fh
