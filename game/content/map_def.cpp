#include "game/content/map_def.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>

#include <nlohmann/json.hpp>

#include "engine/assets/gltf_loader.h"

namespace fh {

namespace {
using nlohmann::json;

std::string str(const json& j, const char* key, const std::string& fallback = {}) {
  return j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : fallback;
}
float num(const json& j, const char* key, float fallback) {
  return j.contains(key) && j[key].is_number() ? j[key].get<float>() : fallback;
}
Vec3 vec(const json& j, const char* key, const Vec3& fallback) {
  if (!j.contains(key) || !j[key].is_array() || j[key].size() != 3) return fallback;
  const json& a = j[key];
  if (!a[0].is_number() || !a[1].is_number() || !a[2].is_number()) return fallback;
  return {a[0].get<float>(), a[1].get<float>(), a[2].get<float>()};
}
// yaw 0 looks down -Z and positive turns right, as CharacterState does.
float yawOf(const Quat& rotation) {
  const Vec3 f = rotation.forward();
  return std::atan2(f.x, -f.z) * 180.0f / 3.14159265f;
}
}  // namespace

std::optional<LoadedMap> loadMap(const std::filesystem::path& path, std::vector<std::string>* errors) {
  auto fail = [&](const std::string& message) {
    if (errors) errors->push_back(path.string() + ": " + message);
    return std::nullopt;
  };
  GltfSceneInfo info;
  std::string err;
  auto mesh = loadGltfMesh(path, &err, &info);
  if (!mesh) return fail(err);

  LoadedMap map;
  map.mesh = std::move(*mesh);
  for (MaterialData& m : map.mesh.materials) m.proceduralDetail = false;  // Blender's material is the truth
  InteriorDef& d = map.def;
  d.id = "map";
  d.name = path.stem().string();
  d.kind = "station";
  d.lighting.keyColor = {0, 0, 0};  // a Sun in Blender turns it on
  d.lighting.ambientSky = {0.012f, 0.013f, 0.016f};
  d.lighting.ambientGround = {0.008f, 0.008f, 0.009f};
  d.lighting.fillColor = {0, 0, 0};

  int exits = 0;
  for (const GltfNodeInfo& n : info.nodes) {
    const json extras = n.extras.empty() ? json::object() : json::parse(n.extras, nullptr, false);
    if (extras.is_discarded() || !extras.is_object()) continue;
    const std::string type = str(extras, "fh_type");
    if (type.empty()) continue;
    const float yaw = yawOf(n.rotation);
    const float radius = num(extras, "fh_radius", 1.6f);
    const std::string prompt = str(extras, "fh_prompt");
    auto use = [&](const char* action, const char* defaultPrompt) {
      d.interactables.push_back({n.name, action, prompt.empty() ? defaultPrompt : prompt, n.position, radius});
    };
    if (type == "map_settings") {
      d.name = str(extras, "fh_name", d.name);
      d.gravity = num(extras, "fh_gravity", d.gravity);
      d.showSky = extras.value("fh_show_sky", 0) != 0;
      const Vec3 ambient = vec(extras, "fh_ambient", d.lighting.ambientSky);
      d.lighting.ambientSky = ambient;
      d.lighting.ambientGround = ambient * 0.6f;
      d.lighting.exposure = num(extras, "fh_exposure", d.lighting.exposure);
      d.lighting.fogColor = vec(extras, "fh_fog_color", d.lighting.fogColor);
      d.lighting.fogDensity = num(extras, "fh_fog_density", d.lighting.fogDensity);
    } else if (type == "player_start") {
      d.spawns["start"] = {n.position, yaw};
      d.spawns["fromShip"] = {n.position, yaw};  // where a fall out of the map returns you
    } else if (type == "exit") {
      use("leaveMap", "Leave");
      ++exits;
    } else if (type == "data_log") {
      const std::string text = str(extras, "fh_text");
      if (text.empty()) map.warnings.push_back("data log '" + n.name + "' has no text");
      d.interactables.push_back({n.name, "say:" + text, prompt.empty() ? "Read log" : prompt, n.position, radius});
    } else if (type == "terminal") {
      use("terminal", "Terminal");
    } else if (type == "seat") {
      use("takeSeat", "Take the pilot seat");
    } else if (type == "airlock") {
      use("exitShip", "Cycle the airlock");
    } else if (type == "reflection_probe") {
      // The box is the Empty's cube: its scale gives the half size, and boxes stay axis-aligned.
      if (d.reflectionProbes.size() >= kMaxReflectionProbes) {
        map.warnings.push_back("more than " + std::to_string(kMaxReflectionProbes) + " reflection probes: '" + n.name + "' is ignored");
      } else {
        d.reflectionProbes.push_back({n.position, n.position - n.scale, n.position + n.scale});
      }
    } else if (type == "enemy_spawn" || type == "loot") {
      const bool enemy = type == "enemy_spawn";
      map.markers.push_back({type, n.name, str(extras, "fh_kind"), n.position, yaw});
      // No enemies or pickups exist yet, so these show as small glows while maps are being built.
      LightDef marker;
      marker.position = n.position + Vec3{0, enemy ? 1.0f : 0.3f, 0};
      marker.color = enemy ? Vec3{3.0f, 0.3f, 0.2f} : Vec3{2.6f, 1.7f, 0.5f};
      marker.size = enemy ? 0.3f : 0.18f;
      marker.range = 0.0f;
      d.lights.push_back(marker);
    } else {
      map.warnings.push_back("unknown entity type '" + type + "' on '" + n.name + "'");
    }
  }
  if (!d.spawns.count("start")) return fail("the map has no Player Start");
  if (exits == 0) map.warnings.push_back("the map has no Exit, so a player can only leave through the menu");

  for (const GltfLightInfo& l : info.lights) {
    if (l.type == GltfLightInfo::Type::Sun) {
      d.lighting.keyDirection = -l.direction;  // key direction points toward the light
      const json sunExtras = l.extras.empty() ? json::object() : json::parse(l.extras, nullptr, false);
      const Vec3 sunColor = !sunExtras.is_discarded() && sunExtras.is_object() ? vec(sunExtras, "fh_color", l.color) : l.color;
      d.lighting.keyColor = sunColor * (l.intensity * 0.3f);
      d.keyCastsShadows = true;
      continue;
    }
    if (l.type == GltfLightInfo::Type::Spot)
      map.warnings.push_back("spot light '" + l.name + "' is drawn as a point light for now");
    const json extras = l.extras.empty() ? json::object() : json::parse(l.extras, nullptr, false);
    LightDef def;
    def.position = l.position;
    def.color = !extras.is_discarded() && extras.is_object() ? vec(extras, "fh_color", l.color) : l.color;
    def.intensity = l.intensity * kBlenderWattsToLight;
    const float brightness = def.intensity * std::max({def.color.x, def.color.y, def.color.z});
    def.range = l.range > 0.0f ? l.range : std::clamp(std::sqrt(brightness / 0.01f), 2.0f, 40.0f);
    def.size = 0.0f;  // fixtures are modelled; an emissive material is what makes them glow
    if (!extras.is_discarded() && extras.is_object()) def.flicker = num(extras, "fh_flicker", 0.0f);
    d.lights.push_back(def);
  }

  // Emissive surfaces light the room around them, in their own colour.
  for (const GltfEmitterInfo& e : info.emitters) {
    const float facing = length(e.normalArea);
    const bool flat = facing > 0.5f * e.area;
    const float effective = std::max(facing, e.area * 0.25f);  // a closed shape shows a quarter of its area
    LightDef def;
    def.color = e.radiance;
    def.intensity = effective * kEmissionLightGain;
    // Off the surface by about its own size, so it spreads like a panel rather than a point on a wall.
    def.position = flat ? e.centroid + (e.normalArea / facing) * (0.3f * std::sqrt(effective)) : e.centroid;
    const float brightness = def.intensity * std::max({def.color.x, def.color.y, def.color.z});
    if (brightness < 0.03f) continue;  // a tiny indicator: it glows, but lights nothing
    def.range = std::clamp(std::sqrt(brightness / 0.01f), 2.0f, 40.0f);
    def.size = 0.0f;
    d.lights.push_back(def);
  }

  if (!info.collision.indices.empty()) {
    auto collider = std::make_shared<TriangleCollider>();
    collider->build(info.collision.vertices, info.collision.indices);
    d.meshCollider = std::move(collider);
  } else {
    map.warnings.push_back("nothing in the map can be walked on");
  }
  return map;
}

}  // namespace fh
