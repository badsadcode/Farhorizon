#include "game/net/dedicated_server.h"

#include <format>
#include <fstream>
#include <set>
#include <sstream>

#include "engine/assets/asset_manifest.h"
#include "engine/core/log.h"
#include "game/content/interior_def.h"
#include "game/economy/economy.h"
#include "game/persistence/save_game.h"
#include "game/session/progression_setup.h"
#include "game/session/world_builder.h"

namespace fh::net {

namespace {
constexpr double kStep = 1.0 / 60.0;
constexpr int kMaxStepsPerUpdate = 8;  // after a stall, catch up gently instead of freezing further
}  // namespace

DedicatedServer::DedicatedServer(std::filesystem::path contentDir) : contentDir_(std::move(contentDir)) {}
DedicatedServer::~DedicatedServer() { stop(); }

bool DedicatedServer::start(const ServerConfig& config, const std::filesystem::path& savePath, std::string* error) {
  auto fail = [&](const std::string& why) {
    if (error) *error = why;
    log::error("server", "{}", why);
    session_.reset();
    link_.reset();
    return false;
  };
  if (running()) return fail("The server is already running");
  if (config.password.empty()) return fail("Set a password first");
  if (config.maxPlayers < 1 || config.maxPlayers > GameSession::kMaxPlayers) return fail("Players must be from 1 to 4");

  std::vector<std::string> errors;
  auto first = [&] { return errors.empty() ? std::string("unknown problem") : errors.front(); };
  auto manifest = AssetManifest::load(contentDir_, &errors);
  if (!manifest) return fail("Could not read the game's content: " + first());
  auto scene = loadSceneDef(*manifest, sceneId_, &errors);
  if (!scene) return fail("Could not load the world: " + first());
  auto ship = loadShipDef(*manifest, scene->player.ship, &errors);
  auto db = loadCommodities(*manifest, "economy.commodities", &errors);
  if (!ship || !db) return fail("Could not load the world: " + first());
  auto progression = loadProgression(*manifest, *scene, *db, &errors);
  if (!progression) return fail("Could not load the world: " + first());
  progression_ = std::make_unique<ProgressionContent>(std::move(*progression));

  auto session = std::make_unique<GameSession>(buildSpaceWorld(*scene, *ship, {}, std::nullopt));
  session->setShipInterior(ship->interior);
  std::set<AssetID> interiors{ship->interior};
  std::set<AssetID> markets;
  for (const auto& p : scene->props)
    if (p.docking) {
      interiors.insert(p.docking->interior);
      if (!p.docking->market.empty()) markets.insert(p.docking->market);
    }
  for (const auto& planet : scene->planets)
    for (const auto& site : planet.sites) {
      interiors.insert(site.interior);
      if (!site.market.empty()) markets.insert(site.market);
    }
  for (const AssetID& id : interiors) {
    auto def = loadInteriorDef(*manifest, id, &errors);
    if (!def) return fail("Could not load '" + id + "': " + first());
    session->addInterior(id, std::move(*def));
  }
  session->setEconomy(*db, scene->startingCredits);
  for (const AssetID& id : markets)
    if (auto md = loadMarketDef(*manifest, id, session->commodities(), &errors)) session->addMarket(id, *md);
  installProgression(*session, *scene, *progression_);

  savePath_ = savePath;
  std::error_code ec;
  if (!savePath_.empty() && std::filesystem::exists(savePath_, ec)) {
    std::ifstream in(savePath_, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    std::string why;
    const auto save = deserializeSave(text.str(), &why);
    if (!save) return fail("The saved world could not be read: " + why);
    if (!applySave(*session, *save, &why)) return fail("The saved world could not be loaded: " + why);
    log::info("server", "continuing the world saved in {}", savePath_.string());
  }
  session->setDedicated(true);

  auto link = std::make_unique<HostLink>(*session);
  link->setPassword(config.password);
  link->setMaxPlayers(config.maxPlayers);
  if (!link->start(config.port)) return fail(std::format("Could not open UDP port {}. Is something else using it?", config.port));
  session_ = std::move(session);
  link_ = std::move(link);
  accumulator_ = 0;
  log::info("server", "'{}' is running on UDP port {}", config.name, config.port);
  return true;
}

bool DedicatedServer::saveNow(std::string* error) {
  if (!session_ || savePath_.empty()) return false;
  std::error_code ec;
  if (savePath_.has_parent_path()) std::filesystem::create_directories(savePath_.parent_path(), ec);
  const std::filesystem::path temporary = savePath_.string() + ".tmp";
  {
    std::ofstream out(temporary, std::ios::binary);
    out << serializeSave(captureSave(*session_, sceneId_));
    if (!out) {
      if (error) *error = "Could not write " + temporary.string();
      return false;
    }
  }
  std::filesystem::rename(temporary, savePath_, ec);  // a crash while writing leaves the last good save
  if (ec) {
    if (error) *error = ec.message();
    return false;
  }
  log::info("server", "world saved to {}", savePath_.string());
  return true;
}

void DedicatedServer::stop() {
  if (!session_) return;
  std::string why;
  if (!saveNow(&why) && !why.empty()) log::error("server", "the world was not saved: {}", why);
  link_.reset();
  session_.reset();
  progression_.reset();
  log::info("server", "stopped");
}

void DedicatedServer::update(double seconds) {
  if (!running()) return;
  link_->pump();
  for (const auto& [who, name] : link_->takeHelmCommands()) session_->helmCommand(name, who);
  if (!simulating()) {  // an empty world holds still and costs nothing
    accumulator_ = 0;
    return;
  }
  accumulator_ += seconds;
  int steps = 0;
  while (accumulator_ >= kStep && steps < kMaxStepsPerUpdate) {
    accumulator_ -= kStep;
    ++steps;
    session_->world().player().input = FlightInput{};  // the pilot's controls, if there is a pilot, replace this
    link_->preStep();
    session_->fixedUpdate(float(kStep));
    link_->postStep();
    for (const auto& [who, name] : link_->takeHelmCommands()) session_->helmCommand(name, who);
  }
  if (steps == kMaxStepsPerUpdate) accumulator_ = 0;
}

std::vector<ServerPlayer> DedicatedServer::players() const {
  std::vector<ServerPlayer> out;
  if (!session_) return out;
  const auto& crew = session_->crew();
  for (size_t i = 1; i < crew.size(); ++i) {  // the first entry is the server itself
    const CrewMember& m = crew[i];
    std::string where = "In a seat";
    if (m.location == Location::Cockpit && m.pilot) where = "At the helm";
    else if (m.location == Location::ShipInterior) where = "Walking the ship";
    else if (m.location == Location::StationInterior) where = "Outside the ship";
    out.push_back({m.id, m.name, where, m.location == Location::Cockpit && m.pilot});
  }
  return out;
}

void DedicatedServer::kick(PlayerId id) {
  if (link_) link_->kick(id, "The server's owner removed you");
}

double DedicatedServer::worldTime() const { return session_ ? session_->world().simulationTime() : 0; }

}  // namespace fh::net
