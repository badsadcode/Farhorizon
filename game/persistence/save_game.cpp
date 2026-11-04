#include "game/persistence/save_game.h"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "engine/core/file_io.h"
#include "engine/core/log.h"
#include "game/session/game_session.h"

namespace fh {

using nlohmann::json;

namespace {
json vec(const DVec3& v) { return json::array({v.x, v.y, v.z}); }
json vec(const Vec3& v) { return json::array({v.x, v.y, v.z}); }
}  // namespace

std::string serializeSave(const SaveGame& s) {
  json j;
  j["schemaVersion"] = s.schemaVersion;
  j["sceneId"] = s.sceneId;
  j["worldTime"] = s.worldTime;
  j["player"] = {{"location", s.location},
                 {"docked", s.docked},
                 {"dockedStation", s.dockedStation},
                 {"dockedBay", s.dockedBay},
                 {"landedPlanet", s.landedPlanet},
                 {"landedSite", s.landedSite},
                 {"shipPosition", vec(s.shipPosition)},
                 {"shipOrientation", json::array({s.shipOrientation.x, s.shipOrientation.y, s.shipOrientation.z,
                                                  s.shipOrientation.w})},
                 {"shipVelocity", vec(s.shipVelocity)},
                 {"boostEnergy", s.boostEnergy},
                 {"credits", s.credits},
                 {"cargo", s.cargo}};
  json markets = json::object();
  for (const auto& [id, m] : s.markets)
    markets[id] = {{"lastTick", m.lastTick}, {"stocks", m.stocks}, {"carry", m.carry}};
  j["markets"] = markets;
  json asteroids = json::array();
  for (const auto& a : s.asteroids) asteroids.push_back({{"field", a.field}, {"index", a.index}, {"yield", a.yield}});
  j["asteroids"] = asteroids;
  return j.dump(2);
}

std::optional<SaveGame> deserializeSave(const std::string& text, std::string* error) {
  auto fail = [&](const std::string& m) -> std::optional<SaveGame> {
    if (error) *error = m;
    return std::nullopt;
  };
  json j = json::parse(text, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return fail("save file is not valid JSON (corrupted?)");
  const int version = j.value("schemaVersion", -1);
  // Migrations: each step upgrades the JSON in place to the next version.
  if (version == 1) {
    j["player"]["landedPlanet"] = "";  // v1 had no landings
    j["player"]["landedSite"] = "";
    j["schemaVersion"] = 2;
  }
  if (j.value("schemaVersion", -1) != SaveGame::kSchemaVersion)
    return fail("unsupported save version " + std::to_string(version) + " (this build reads version " +
                std::to_string(SaveGame::kSchemaVersion) + ")");
  try {
    SaveGame s;
    s.sceneId = j.at("sceneId").get<std::string>();
    s.worldTime = j.at("worldTime").get<double>();
    const json& p = j.at("player");
    s.location = p.at("location").get<std::string>();
    s.docked = p.at("docked").get<bool>();
    s.dockedStation = p.at("dockedStation").get<std::string>();
    s.dockedBay = p.at("dockedBay").get<std::uint32_t>();
    s.landedPlanet = p.at("landedPlanet").get<std::string>();
    s.landedSite = p.at("landedSite").get<std::string>();
    const auto& sp = p.at("shipPosition");
    s.shipPosition = {sp.at(0).get<double>(), sp.at(1).get<double>(), sp.at(2).get<double>()};
    const auto& so = p.at("shipOrientation");
    s.shipOrientation = normalize(Quat{so.at(0).get<float>(), so.at(1).get<float>(), so.at(2).get<float>(),
                                       so.at(3).get<float>()});
    const auto& sv = p.at("shipVelocity");
    s.shipVelocity = {sv.at(0).get<float>(), sv.at(1).get<float>(), sv.at(2).get<float>()};
    s.boostEnergy = p.at("boostEnergy").get<float>();
    s.credits = p.at("credits").get<std::int64_t>();
    s.cargo = p.at("cargo").get<std::map<std::string, int>>();
    for (const auto& [id, m] : j.at("markets").items()) {
      SaveGame::MarketState ms;
      ms.lastTick = m.at("lastTick").get<double>();
      ms.stocks = m.at("stocks").get<std::vector<int>>();
      ms.carry = m.value("carry", std::vector<double>{});
      s.markets[id] = ms;
    }
    for (const auto& a : j.at("asteroids"))
      s.asteroids.push_back({a.at("field").get<std::string>(), a.at("index").get<std::uint32_t>(),
                             a.at("yield").get<int>()});
    if (s.location != "cockpit" && s.location != "ship" && s.location != "station")
      return fail("save file has an unknown location '" + s.location + "'");
    const bool landed = !s.landedSite.empty();
    if (s.location != "cockpit" && !s.docked && !landed)
      return fail("save file is inconsistent: on foot but neither docked nor landed");
    if (s.docked && landed) return fail("save file is inconsistent: docked and landed at once");
    for (const auto& [id, n] : s.cargo)
      if (n < 0) return fail("save file has negative cargo for '" + id + "'");
    return s;
  } catch (const std::exception& e) {
    return fail(std::string("save file is missing data: ") + e.what());
  }
}

bool writeSaveFile(const std::filesystem::path& path, const SaveGame& s, std::string* error) {
  std::error_code ec;
  if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
  if (std::filesystem::exists(path)) {
    std::filesystem::path bak = path;
    bak += ".bak";
    std::filesystem::copy_file(path, bak, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) log::warn("save", "could not write backup '{}': {}", bak.string(), ec.message());
  }
  const std::string text = serializeSave(s);
  if (!writeFileAtomic(path, text.data(), text.size())) {
    if (error) *error = "could not write '" + path.string() + "'";
    return false;
  }
  return true;
}

std::optional<SaveGame> readSaveFile(const std::filesystem::path& path, std::string* error) {
  auto text = readTextFile(path);
  if (!text) {
    if (error) *error = "no save file at '" + path.string() + "'";
    return std::nullopt;
  }
  return deserializeSave(*text, error);
}

SaveGame captureSave(GameSession& session, const std::string& sceneId) {
  SpaceWorld& w = session.world();
  SaveGame s;
  s.sceneId = sceneId;
  s.worldTime = w.simulationTime();
  switch (session.location()) {
    case Location::Cockpit: s.location = "cockpit"; break;
    case Location::ShipInterior: s.location = "ship"; break;
    case Location::StationInterior: s.location = "station"; break;
  }
  const Docking& d = session.docking();
  // Only a fully clamped ship is saved as docked; any other docking phase saves as free flight.
  s.docked = d.state() == DockState::Docked && d.station();
  if (s.docked) {
    s.dockedStation = w.props().at(*d.station()).def.id;
    s.dockedBay = d.bay();
  }
  const Landing& ld = session.landing();
  // Mid-sequence landings save as landed (approach) or as back in orbit (liftoff/ascent);
  // the descent itself saves as the orbit start. Nobody reloads halfway through a corridor.
  std::optional<LandingSiteRef> landedAt;
  if (ld.site() && (ld.state() == LandState::Landed || ld.state() == LandState::SurfaceApproach)) landedAt = ld.site();
  const FlightState& f = w.player().state;
  s.shipPosition = w.frame().toSystem(f.position).metres;
  if (landedAt) {
    const Planet& pl = w.planets().at(landedAt->planet);
    s.landedPlanet = pl.def.id;
    s.landedSite = pl.def.sites.at(landedAt->site).id;
    s.shipPosition = pl.siteOrbit(landedAt->site);  // origin-independent (double)
  }
  if (s.docked) {
    // Origin-independent: station system position (double) + bay offset (small, rotated in float).
    const Prop& p = w.props().at(*d.station());
    const DockingBayDef& bay = p.def.docking->bays.at(d.bay());
    s.shipPosition = p.system.metres + DVec3(p.rotation.rotate(bay.position));
  }
  s.shipOrientation = f.orientation;
  s.shipVelocity = (s.docked || landedAt) ? Vec3{} : f.velocity;
  s.boostEnergy = f.boostEnergy;
  s.credits = session.credits();
  s.cargo = session.cargo().contents();
  for (auto& [id, m] : session.markets()) {
    m.advanceTo(s.worldTime);
    s.markets[id] = {m.lastTick(), m.stocks(), m.carry()};
  }
  for (const auto& a : w.asteroids())
    if (a.yield != a.initialYield) s.asteroids.push_back({w.fields().at(a.field).def.id, a.index, a.yield});
  return s;
}

bool applySave(GameSession& session, const SaveGame& s, std::string* error) {
  auto fail = [&](const std::string& m) {
    if (error) *error = m;
    return false;
  };
  SpaceWorld& w = session.world();
  w.setSimulationTime(s.worldTime);
  FlightState& f = w.player().state;
  f.position = w.frame().toLocal(SystemPosition{s.shipPosition});
  f.orientation = s.shipOrientation;
  f.velocity = s.shipVelocity;
  f.angularVelocity = {};
  f.boostEnergy = s.boostEnergy;
  w.player().previous = f;
  session.setCredits(s.credits);
  // Cargo restored as saved but clamped to this ship's capacity (never invents goods).
  session.cargo().setContents({});
  for (const auto& [id, n] : s.cargo) {
    if (!session.commodities().find(id)) return fail("save references unknown commodity '" + id + "'");
    const int fit = std::min(n, session.cargo().maxAddable(id));
    if (fit > 0) session.cargo().add(id, fit);
    if (fit < n) log::warn("save", "cargo '{}' clamped from {} to {} (capacity)", id, n, fit);
  }
  for (const auto& [id, ms] : s.markets) {
    auto it = session.markets().find(id);
    if (it == session.markets().end()) {
      log::warn("save", "market '{}' no longer exists; ignored", id);
      continue;
    }
    it->second.restore(ms.stocks, ms.lastTick, ms.carry);
  }
  for (const auto& as : s.asteroids) {
    for (std::uint32_t i = 0; i < w.asteroids().size(); ++i) {
      Asteroid& a = w.asteroid(i);
      if (a.index == as.index && w.fields().at(a.field).def.id == as.field) {
        a.yield = std::clamp(as.yield, 0, a.initialYield);
        break;
      }
    }
  }
  if (s.docked) {
    std::optional<std::uint32_t> prop;
    for (std::uint32_t i = 0; i < w.props().size(); ++i)
      if (w.props()[i].def.id == s.dockedStation) prop = i;
    if (!prop) return fail("save is docked at unknown station '" + s.dockedStation + "'");
    if (!session.docking().dockAt(w, *prop, s.dockedBay)) return fail("cannot restore docking at '" + s.dockedStation + "'");
    if (s.location != "cockpit") {
      if (!session.leaveSeat().ok) return fail("cannot restore on-foot location");
      if (s.location == "station" && !session.perform("exitShip").ok) return fail("cannot restore station location");
    }
  }
  if (!s.landedSite.empty()) {
    std::optional<LandingSiteRef> ref;
    for (std::uint32_t pi = 0; pi < w.planets().size(); ++pi)
      if (w.planets()[pi].def.id == s.landedPlanet)
        for (std::uint32_t si = 0; si < w.planets()[pi].def.sites.size(); ++si)
          if (w.planets()[pi].def.sites[si].id == s.landedSite) ref = LandingSiteRef{pi, si};
    if (!ref) return fail("save is landed at unknown site '" + s.landedPlanet + "/" + s.landedSite + "'");
    session.landing().landAt(w, *ref);
    if (s.location != "cockpit") {
      if (!session.leaveSeat().ok) return fail("cannot restore on-foot location");
      if (s.location == "station" && !session.perform("exitShip").ok) return fail("cannot restore surface location");
    }
  }
  return true;
}

}  // namespace fh
