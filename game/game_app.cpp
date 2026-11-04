#include "game/game_app.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <map>

#include "game/content/interior_def.h"
#include "game/session/world_builder.h"
#include "game/economy/economy.h"
#include "game/persistence/save_game.h"
#include "game/world/terrain.h"

#include "engine/assets/gltf_loader.h"
#include "engine/core/log.h"
#include "engine/platform/window.h"
#include "engine/render/gpu_resources.h"
#include "engine/render/renderer.h"
#include "engine/render/starfield.h"
#include "engine/render/vk_context.h"
#include "game/content/ship_def.h"
#include "game/hud.h"
#include "game/world/asteroid_gen.h"

namespace fh {

namespace {
struct LoadedMesh {
  MeshData data;
  Mat4 meshFix;
};
}  // namespace

GameApp::GameApp(AppOptions options) : options_(std::move(options)) {
  scripted_ = !options_.benchmarkId.empty() || options_.exitAfterFrames > 0;
  settingsPath_ = options_.settingsPath;
  if (!options_.uiScript.empty()) {
    std::string err;
    if (!uiScript_.load(options_.uiScript, &err)) throw std::runtime_error(err);
  }
  // Benchmarks and scripted runs use defaults (reproducible); players get their settings.json.
  if (!scripted_) settings_ = loadSettings(settingsPath_);
  if (scripted_ || !options_.devOverlay) settings_.showDevOverlay = scripted_ && options_.devOverlay;
  showDevOverlay_ = settings_.showDevOverlay;
  window_ = std::make_unique<Window>("FARHORIZON", options_.width, options_.height);
  if (!scripted_ && settings_.fullscreen && !window_->isFullscreen()) window_->toggleFullscreen();
  ctx_ = std::make_unique<VkContext>(*window_, VkContextOptions{options_.validation, options_.vsync && settings_.vsync});
  renderer_ = std::make_unique<Renderer>(*ctx_, options_.shaderDir);
  if (!loadContent(contentErrors_))
    throw std::runtime_error("initial content load failed:\n  " +
                             (contentErrors_.empty() ? std::string("unknown error") : contentErrors_.front()));
  {  // UI font atlas (TrueType -> one RGBA texture shared by HUD, terminal and menus)
    std::string err;
    if (font_.build({theme_.monoFont, theme_.sansFont}, &err)) {
      renderer_->setUiAtlas(font_.pixels().data(), font_.width(), font_.height());
      OverlayBuilder::setFont(&font_);
    } else {
      log::warn("ui", "font atlas failed ({}); using the debug font", err);
    }
  }
  if (benchmark_ && !benchmark_->startIn.empty()) options_.startIn = benchmark_->startIn;
  {
    std::vector<std::string> audioErrors;
    if (!audio_.load(*manifest_, "audio.config", &audioErrors))
      for (const auto& e : audioErrors) log::warn("audio", "{}", e);
    if (!benchmark_ && options_.exitAfterFrames == 0 && !uiScript_.active())
      audioDevice_.open(audio_.mixer());  // tests/benchmarks stay silent
  }
  if (benchmark_ && benchmark_->startLanded) options_.startLanded = true;
  if (options_.startIn == "surface") options_.startLanded = true;
  if (options_.startLanded && !world().planets().empty() && !world().planets()[0].def.sites.empty()) {
    session_->landing().landAt(world(), LandingSiteRef{0, 0});
    camera_.snap();
    if (options_.startIn == "surface" || options_.startIn == "ship") {
      session_->leaveSeat();
      if (options_.startIn == "surface") session_->perform("exitShip");
    }
  } else if (options_.startDocked || !options_.startIn.empty()) {
    session_->docking().forceDock(world());
    lastDockState_ = DockState::Docked;
    if (!options_.startIn.empty()) {
      session_->leaveSeat();
      if (options_.startIn == "station") session_->perform("exitShip");
    }
  }
  applySettings();
  const bool directStart = options_.startDocked || options_.startLanded || !options_.startIn.empty() ||
                           !options_.loadPath.empty() || options_.autofly;
  if (!scripted_ && !directStart) openMenu(MenuSystem::Screen::Main);
  else if (!scripted_) setMouseCapture(true);
}

// ---------------------------------------------------------------------------------------------
// Front end: settings, menus, save slots (Stage 7)

void GameApp::applyBindingOverrides() {
  for (const auto& [action, keys] : settings_.bindings)
    if (!input_.setBinding(action, keys)) log::warn("settings", "ignoring invalid binding for '{}'", action);
}

void GameApp::applySettings() {
  settings_.clamp();
  if (!settings_.look.empty())
    if (const int i = looks_.indexOf(settings_.look); i >= 0) activeLook_ = i;
  const bool vsync = options_.vsync && settings_.vsync;
  if (ctx_->vsync() != vsync) ctx_->setVsync(vsync);
  if (!scripted_ && window_->isFullscreen() != settings_.fullscreen) window_->toggleFullscreen();
  audio_.setVolumes(settings_.masterVolume, settings_.ambienceVolume, settings_.effectsVolume, settings_.engineVolume,
                    settings_.uiVolume);
  input_.setMouseOverrides(settings_.mouseSensitivity, settings_.invertY);
  showDevOverlay_ = settings_.showDevOverlay;
}

void GameApp::persistSettings() {
  if (scripted_) return;
  std::string err;
  if (!saveSettings(settingsPath_, settings_, &err)) log::warn("settings", "{}", err);
}

void GameApp::refreshSaveSlots() {
  struct Def {
    const char* label;
    const char* file;
    bool writable;
  };
  static constexpr Def defs[] = {{"Slot 1", "saves/slot1.json", true},
                                 {"Slot 2", "saves/slot2.json", true},
                                 {"Slot 3", "saves/slot3.json", true},
                                 {"Quicksave", "saves/quicksave.json", true},
                                 {"Autosave", "saves/autosave.json", false}};
  slots_.clear();
  for (const auto& d : defs) {
    SaveSlotInfo si;
    si.label = d.label;
    si.path = d.file;
    si.writable = d.writable;
    std::error_code ec;
    if (std::filesystem::exists(si.path, ec)) {
      si.exists = true;
      std::string err;
      if (auto s = readSaveFile(si.path, &err)) {
        std::string where = "In flight";
        for (const auto& p : scene_.props)
          if (s->docked && p.id == s->dockedStation) where = "Docked at " + p.name;
        for (const auto& pl : scene_.planets)
          for (const auto& site : pl.sites)
            if (!s->landedSite.empty() && site.id == s->landedSite) where = "Landed at " + site.name;
        std::string cr = std::to_string(s->credits);
        for (int i = int(cr.size()) - 3; i > 0; i -= 3) cr.insert(size_t(i), ",");
        si.summary = std::format("{}  ·  {} cr  ·  {} contract(s)", where, cr, s->contracts.size());
      } else {
        si.summary = "Unreadable: " + err;
      }
      const auto ft = std::filesystem::last_write_time(si.path, ec);
      if (!ec) {
        const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(ft);
        const std::time_t tt = std::chrono::system_clock::to_time_t(sys);
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &tt);
#else
        localtime_r(&tt, &tm);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
        si.when = buf;
      }
    }
    slots_.push_back(si);
  }
}

void GameApp::openMenu(MenuSystem::Screen s) {
  if (market_.isOpen()) market_.close();
  refreshSaveSlots();
  menus_.open(s);
  menuOpenedFrame_ = frameCount_;
  setMouseCapture(false);
  if (s == MenuSystem::Screen::Main) {  // the world turns slowly behind the title
    camera_.setMode(CameraMode::Orbit, world().playerRenderTransform(1.0f));
    if (!camera_.autoRotating()) camera_.toggleAutoRotate();
  }
}

void GameApp::resumeGame() {
  menus_.close();
  if (camera_.isDev()) camera_.setMode(CameraMode::Chase, world().playerRenderTransform(1.0f));
  camera_.snap();
  setMouseCapture(true);
}

void GameApp::startNewGame() {
  freshStart_ = true;
  std::vector<std::string> errors;
  const bool ok = loadContent(errors);
  freshStart_ = false;
  if (!ok) {
    contentErrors_ = errors;
    log::error("app", "new game failed to load content");
    return;
  }
  market_.close();
  miningMode_ = false;
  resumeGame();
  session_->notify("Kestrel traffic control: welcome to the Cinder Reach, pilot.");
  log::info("app", "new game");
}

UiInput GameApp::uiInput() {
  UiInput in;
  const float ps = window_->pixelScale();
  in.mouseX = input_.mouseX() * ps;
  in.mouseY = input_.mouseY() * ps;
  in.mouseMoved = in.mouseX != lastUiMouseX_ || in.mouseY != lastUiMouseY_;
  if (mouseSettleFrames_ > 0) {  // not a real move: it must not steal keyboard focus
    --mouseSettleFrames_;
    in.mouseMoved = false;
  }
  lastUiMouseX_ = in.mouseX;
  lastUiMouseY_ = in.mouseY;
  in.mouseDown = input_.mouseLeft();
  in.mousePressed = input_.mouseLeftPressed();
  in.mouseReleased = input_.mouseLeftReleased();
  in.wheel = input_.wheel();
  in.up = input_.pressedOrRepeat("ui.up");
  in.down = input_.pressedOrRepeat("ui.down");
  in.left = input_.pressedOrRepeat("ui.left");
  in.right = input_.pressedOrRepeat("ui.right");
  in.accept = input_.pressed("ui.accept");
  in.back = input_.pressed("ui.back");
  in.tabNext = input_.pressed("ui.tabNext");
  return in;
}

void GameApp::buildMenus() {
  const VkExtent2D ext = renderer_->extent();
  std::vector<std::string> lookNames, lookIds;
  for (const auto& p : looks_.profiles) {
    lookNames.push_back(p.name);
    lookIds.push_back(p.id);
  }
  if (settings_.look.empty() && !looks_.profiles.empty()) settings_.look = looks_.profiles[size_t(activeLook_)].id;
  bool canContinue = false;
  for (const auto& s : slots_) canContinue |= s.exists;
  MenuEnv env{settings_, input_, lookNames, lookIds, slots_, canContinue, "FARHORIZON 0.7  ·  test build"};
  UiInput in = uiInput();
  // Rebinding: this key press belongs to the capture. Just opened: the Esc that opened the pause
  // menu is still "pressed" this frame and must not close it again.
  if (menus_.capturingKey() || menuOpenedFrame_ == frameCount_) {
    UiInput neutral;
    neutral.mouseX = in.mouseX;
    neutral.mouseY = in.mouseY;
    in = neutral;
  }
  ui_.begin(overlay_, theme_, float(ext.width), float(ext.height), settings_.uiScale, in, runTime_);
  auto actions = menus_.update(ui_, env);
  ui_.end();
  if (ui_.focus() != lastMenuFocus_) {  // also lets the player hear the Interface volume setting
    if (lastMenuFocus_ >= 0) audio_.event("ui", 0.45f, 1.3f);
    lastMenuFocus_ = ui_.focus();
  }
  pendingMenuActions_.insert(pendingMenuActions_.end(), actions.begin(), actions.end());
}

void GameApp::handleMenuActions() {
  auto actions = std::exchange(pendingMenuActions_, {});
  for (const auto& a : actions) {
    using T = MenuAction::Type;
    switch (a.type) {
      case T::Resume: resumeGame(); break;
      case T::NewGame: startNewGame(); return;
      case T::Continue: {
        std::filesystem::path best;
        std::filesystem::file_time_type bestTime{};
        std::error_code ec;
        for (const auto& s : slots_) {
          if (!s.exists) continue;
          const auto t = std::filesystem::last_write_time(s.path, ec);
          if (!ec && (best.empty() || t > bestTime)) {
            best = s.path;
            bestTime = t;
          }
        }
        if (!best.empty() && loadGame(best)) resumeGame();
        return;
      }
      case T::Load:
        if (loadGame(a.path)) resumeGame();
        else refreshSaveSlots();
        return;
      case T::Save:
        saveGame(a.path);
        refreshSaveSlots();
        menus_.open(MenuSystem::Screen::Pause);
        break;
      case T::QuitToMenu: openMenu(MenuSystem::Screen::Main); break;
      case T::Quit: quit_ = true; break;
      case T::SettingsChanged: applySettings(); break;
      case T::BindingsChanged:
        applyBindingOverrides();
        persistSettings();
        break;
      case T::ResetBindings:
        if (manifest_)
          if (auto path = manifest_->resolve("config.input", "config")) {
            std::vector<std::string> errs;
            input_.loadBindings(*path, &errs);
          }
        applySettings();
        persistSettings();
        break;
      case T::SettingsClosed: persistSettings(); break;
    }
  }
}


GameApp::~GameApp() {
  audioDevice_.close();  // before SDL_Quit (Window dtor) and before the mixer goes away
  renderer_.reset();
  ctx_.reset();
  window_.reset();
}

void GameApp::setMouseCapture(bool on) {
  window_->setRelativeMouse(on);
  mouseCaptured_ = on;
}

bool GameApp::loadContent(std::vector<std::string>& errors) {
  errors.clear();
  auto manifest = AssetManifest::load(options_.contentDir, &errors);
  if (!manifest) return false;
  for (auto& p : manifest->validateFiles()) errors.push_back(p);
  if (!errors.empty()) return false;

  if (auto inputPath = manifest->resolve("config.input", "config")) input_.loadBindings(*inputPath, &errors);
  else errors.push_back("config.input missing from manifest");
  if (!scripted_) applyBindingOverrides();
  auto theme = loadUiTheme(*manifest, "ui.theme", &errors);
  if (!theme) return false;

  std::optional<BenchmarkDef> bench;
  std::string sceneId = options_.sceneId;
  if (!options_.benchmarkId.empty()) {
    bench = loadBenchmarkDef(*manifest, options_.benchmarkId, &errors);
    if (!bench) return false;
    sceneId = bench->scene;
  }
  auto scene = loadSceneDef(*manifest, sceneId, &errors);
  if (!scene) return false;
  auto looks = loadVisualProfiles(*manifest, "visuals.post_profiles", &errors);
  if (!looks) return false;
  if (bench && bench->playerOverride) {
    scene->player.position = bench->playerOverride->position;
    scene->player.rotationDeg = bench->playerOverride->rotationDeg;
  }

  // ---- CPU-side loading (no GPU work until everything succeeded) ----
  auto loadMesh = [&](const AssetID& id, const MaterialOverrides& overrides, const std::string& owner, float scale,
                      const Vec3& rotDeg, bool recenter, bool warnUnusedOverrides = true) -> std::optional<LoadedMesh> {
    auto path = manifest->resolve(id, "mesh");
    if (!path) {
      errors.push_back(owner + ": mesh '" + id + "' is not a mesh asset");
      return std::nullopt;
    }
    std::string err;
    auto mesh = loadGltfMesh(*path, &err);
    if (!mesh) {
      errors.push_back("mesh '" + id + "': " + err);
      return std::nullopt;
    }
    // Interior-wide overrides are shared by all pieces, so unmatched names are expected there.
    applyMaterialOverrides(*mesh, overrides, owner, warnUnusedOverrides ? &errors : nullptr);
    LoadedMesh lm;
    lm.meshFix = Mat4::rotation(eulerDegToQuat(rotDeg)) * Mat4::scale({scale, scale, scale}) *
                 (recenter ? Mat4::translation(-mesh->boundsCenter()) : Mat4::identity());
    lm.data = std::move(*mesh);
    return lm;
  };

  auto shipDef = loadShipDef(*manifest, scene->player.ship, &errors);
  if (!shipDef) return false;
  auto shipMesh = loadMesh(shipDef->mesh, shipDef->materialOverrides, "ship '" + shipDef->id + "'",
                           shipDef->meshScale, shipDef->meshRotationDeg, true);
  if (!shipMesh) return false;

  std::map<std::string, LoadedMesh> propMeshes;  // by prop id
  for (const auto& p : scene->props) {
    auto m = loadMesh(p.mesh, p.materialOverrides, "prop '" + p.id + "'", p.scale, {0, 0, 0}, true);
    if (!m) return false;
    propMeshes.emplace(p.id, std::move(*m));
  }

  // Interiors: the ship's cabin and every docking station's interior.
  std::map<AssetID, InteriorDef> interiors;
  std::vector<AssetID> interiorIds;
  if (!shipDef->interior.empty()) interiorIds.push_back(shipDef->interior);
  for (const auto& p : scene->props)
    if (p.docking) interiorIds.push_back(p.docking->interior);
  for (const auto& pl : scene->planets)
    for (const auto& site : pl.sites) interiorIds.push_back(site.interior);
  std::map<std::pair<AssetID, std::string>, LoadedMesh> pieceMeshes;  // (interior, template) -> mesh
  for (const auto& iid : interiorIds) {
    if (interiors.count(iid)) continue;
    auto def = loadInteriorDef(*manifest, iid, &errors);
    if (!def) return false;
    for (const auto& [tname, t] : def->templates) {
      auto m = loadMesh(t.mesh, def->materialOverrides, "interior '" + iid + "'", 1.0f, {0, 0, 0}, false, false);
      if (!m) return false;
      pieceMeshes.emplace(std::make_pair(iid, tname), std::move(*m));
    }
    interiors.emplace(iid, std::move(*def));
  }

  std::map<std::string, std::vector<MeshData>> fieldMeshes;  // by field id
  for (const auto& f : scene->asteroidFields) {
    auto& v = fieldMeshes[f.id];
    for (std::uint32_t i = 0; i < f.variants; ++i) {
      AsteroidMeshParams mp;
      mp.seed = (std::uint64_t(f.seed) << 8) + i;
      mp.subdivisions = f.detail;
      mp.roughness = f.roughness;
      mp.color = f.color;
      v.push_back(generateAsteroidMesh(mp));
    }
  }
  auto commodityDb = loadCommodities(*manifest, "economy.commodities", &errors);
  if (!commodityDb) return false;
  std::map<AssetID, MarketDef> marketDefs;
  for (const auto& p : scene->props)
    if (p.docking && !p.docking->market.empty() && !marketDefs.count(p.docking->market)) {
      auto md = loadMarketDef(*manifest, p.docking->market, *commodityDb, &errors);
      if (!md) return false;
      marketDefs.emplace(p.docking->market, std::move(*md));
    }
  for (const auto& pl : scene->planets)
    for (const auto& site : pl.sites)
      if (!site.market.empty() && !marketDefs.count(site.market)) {
        auto md = loadMarketDef(*manifest, site.market, *commodityDb, &errors);
        if (!md) return false;
        marketDefs.emplace(site.market, std::move(*md));
      }
  // Stage 6: weapons, NPC ship types, upgrades and contracts.
  auto progression = loadProgression(*manifest, *scene, *commodityDb, &errors);
  if (!progression) return false;
  auto newProgression = std::make_unique<ProgressionContent>(std::move(*progression));
  std::map<AssetID, LoadedMesh> npcMeshes;
  for (const auto& [nid, ndef] : newProgression->npcShips) {
    auto m = loadMesh(ndef.mesh, ndef.materialOverrides, "ship '" + nid + "'", ndef.meshScale, ndef.meshRotationDeg, true);
    if (!m) return false;
    npcMeshes.emplace(nid, std::move(*m));
  }
  for (auto& w : errors) log::warn("content", "{}", w);  // only non-fatal warnings remain here

  // ---- Commit. Hot reload and save loading share one path: snapshot -> rebuild -> apply. ----
  std::optional<SaveGame> restore;
  if (pendingSave_) restore = std::move(pendingSave_);
  else if (session_ && !bench && !freshStart_) restore = captureSave(*session_, scene_.id);
  pendingSave_.reset();
  std::optional<SystemPosition> keepPos;
  if (restore) keepPos = SystemPosition{restore->shipPosition};

  renderer_->clearMeshes();
  WorldVisuals visuals;
  visuals.ship = [&](const ShipDef&) {
    VisualInfo v;
    v.handle = renderer_->uploadMesh(shipMesh->data);
    v.meshFix = shipMesh->meshFix;
    v.boundsRadius = shipMesh->data.boundsRadius() * shipDef->meshScale;
    v.sockets = shipMesh->data.sockets;
    return v;
  };
  visuals.prop = [&](const PropDef& pd) {
    const LoadedMesh& m = propMeshes.at(pd.id);
    return VisualInfo{renderer_->uploadMesh(m.data), m.meshFix, m.data.boundsRadius() * pd.scale, {}};
  };
  visuals.asteroidVariants = [&](const AsteroidFieldDef& f) {
    std::vector<VisualHandle> h;
    for (const auto& m : fieldMeshes.at(f.id)) h.push_back(renderer_->uploadMesh(m));
    return h;
  };
  auto newSession = std::make_unique<GameSession>(buildSpaceWorld(*scene, *shipDef, visuals, keepPos));
  newSession->setShipInterior(shipDef->interior);
  newSession->setEconomy(*commodityDb, scene->startingCredits);
  for (const auto& [mid, md] : marketDefs) newSession->addMarket(mid, md);
  installProgression(*newSession, *scene, *newProgression, [&](const ShipDef& def, ShipType& t) {
    const LoadedMesh& m = npcMeshes.at(def.id);
    t.visual = renderer_->uploadMesh(m.data);
    t.meshFix = m.meshFix;
    if (def.collisionRadius <= 0) t.radius = m.data.boundsRadius() * def.meshScale;
  });
  interiorPieces_.clear();
  for (auto& [iid, def] : interiors) {
    std::map<std::string, std::uint32_t> handles;
    for (const auto& [tname, t] : def.templates) handles[tname] = renderer_->uploadMesh(pieceMeshes.at({iid, tname}).data);
    auto& list = interiorPieces_[iid];
    for (const auto& piece : def.pieces) {
      const Mat4 model = Mat4::translation(piece.position) *
                         Mat4::rotation(Quat::fromAxisAngle({0, 1, 0}, radians(90.0f * float(piece.yawQuarterTurns)))) *
                         Mat4::scale({piece.scale, piece.scale, piece.scale});
      list.push_back({handles.at(piece.templ), model});
    }
    if (def.terrain) list.push_back({renderer_->uploadMesh(HeightField(*def.terrain).buildMesh()), Mat4::identity()});
    newSession->addInterior(iid, std::move(def));
  }
  if (restore) {
    std::string err;
    if (!applySave(*newSession, *restore, &err)) {
      errors.push_back("save/restore failed: " + err);
      log::error("save", "{}", err);
    }
  }
  std::size_t asteroidCount = newSession->world().asteroids().size();

  StarfieldParams sp;
  sp.seed = scene->starfield.seed;
  sp.count = scene->starfield.count;
  sp.brightness = scene->starfield.brightness;
  sp.galacticPlaneTiltRadians = radians(scene->starfield.galacticPlaneTiltDeg);
  renderer_->setStarfield(generateStarfield(sp));

  const bool firstLoad = !manifest_.has_value();
  // Keep the active look across reloads (by id); otherwise use the override or the file default.
  const std::string lookId = !firstLoad && activeLook_ < int(looks_.profiles.size())
                                 ? looks_.profiles[size_t(activeLook_)].id
                                 : options_.look.value_or(looks->defaultId);
  looks_ = std::move(*looks);
  activeLook_ = std::max(0, looks_.indexOf(lookId));
  if (looks_.indexOf(lookId) < 0) log::warn("content", "look '{}' not found; using '{}'", lookId, looks_.profiles[0].id);
  manifest_ = std::move(manifest);
  theme_ = *theme;
  benchmark_ = bench;
  scene_ = std::move(*scene);
  session_ = std::move(newSession);   // the old session dies before the old content below
  progression_ = std::move(newProgression);
  lastDockState_ = session_->docking().state();
  if (firstLoad) {
    camera_.configureDev(scene_.camera.fovDeg, scene_.camera.orbitDistance, scene_.camera.yawDeg,
                         scene_.camera.pitchDeg, scene_.camera.autoRotateDegPerSec);
    std::string mode = scene_.camera.initialMode;
    if (benchmark_) mode = benchmark_->camera;
    if (options_.cameraOverride) mode = *options_.cameraOverride;
    camera_.setMode(cameraModeFromString(mode), world().playerRenderTransform(1.0f));
    if (benchmark_ && !benchmark_->autoDock && !benchmark_->mine && !benchmark_->combat && benchmark_->startIn.empty() &&
        !benchmark_->startLanded &&
        !benchmark_->autoLand)
      controller_.setAutofly(true, benchmark_->throttle, benchmark_->rotation);
    else if (options_.autofly) controller_.setAutofly(true, 0.4f, {0.0f, 0.08f, 0.0f});
  }
  camera_.snap();
  log::info("content", "scene '{}' loaded: {} props, {} asteroids in {} fields, {} interiors, {} stars", scene_.id,
            world().props().size(), asteroidCount, world().fields().size(), interiorPieces_.size(), sp.count);
  return true;
}

void GameApp::openMarket() {
  if (!MarketPanel::available(*session_) || market_.isOpen()) return;
  market_.open(*session_);
  ui_.resetFocus();
  mouseWasCaptured_ = mouseCaptured_;
  setMouseCapture(false);
}

void GameApp::closeMarket() {
  market_.close();
  setMouseCapture(mouseWasCaptured_);
}

void GameApp::saveGame(const std::filesystem::path& path, bool quiet) {
  std::string err;
  if (writeSaveFile(path, captureSave(*session_, scene_.id), &err)) {
    if (!quiet) session_->notify("Game saved");
    log::info("save", "saved to {}", path.string());
  } else {
    session_->notify("SAVE FAILED: " + err);
    log::error("save", "{}", err);
  }
}

bool GameApp::loadGame(const std::filesystem::path& path) {
  std::string err;
  auto save = readSaveFile(path, &err);
  if (!save) {
    if (session_) session_->notify("Load failed: " + err);
    log::error("save", "load failed: {}", err);
    return false;
  }
  if (save->sceneId != scene_.id && !scene_.id.empty()) options_.sceneId = save->sceneId;
  if (scene_.id.empty()) options_.sceneId = save->sceneId;
  pendingSave_ = std::move(save);
  std::vector<std::string> errors;
  if (!loadContent(errors)) {
    contentErrors_ = errors;
    log::error("save", "load failed while rebuilding the world");
    return false;
  }
  if (market_.isOpen()) closeMarket();
  camera_.snap();
  lastDockState_ = session_->docking().state();
  session_->notify("Game loaded");
  log::info("save", "loaded {}", path.string());
  return true;
}

void GameApp::updateAudio(float dt, float engineDemand, float speedFraction) {
  AudioWatch& w = audioWatch_;
  const Docking& d = session_->docking();
  if (d.state() != w.dock) {
    if (d.state() == DockState::Capture) audio_.event("clamp");
    if (d.state() == DockState::Undocking) audio_.event("clamp", 0.8f, 0.85f);
    w.dock = d.state();
  }
  if (d.messageTime() != w.messageTime && !d.message().empty()) {
    audio_.event("radio");
    w.messageTime = d.messageTime();
  }
  const Location loc = session_->location();
  if (loc != w.location) {
    const bool airlock = (w.location == Location::ShipInterior && loc == Location::StationInterior) ||
                         (w.location == Location::StationInterior && loc == Location::ShipInterior);
    audio_.event(airlock ? "airlock" : "ui");
    w.location = loc;
  }
  const PlayerShip& p = world().player();
  if (p.collisions != w.collisions) {
    audio_.event("impact", std::min(1.0f, p.lastImpactSpeed / 30.0f + 0.3f), 0.6f);
    w.collisions = p.collisions;
  }
  for (int i = 0; i < session_->lastMiningEvents().collected; ++i) audio_.event("scoop", 0.8f, 0.9f + 0.05f * float(i));
  // Combat: positional one-shots (the cockpit "hears" what its sensors pick up), flashes, shake.
  {
    const Vec3 me = p.state.position;
    const Vec3 right = p.state.orientation.right();
    int shots = 0;
    for (const auto& e : session_->takeCombatEvents()) {
      const Vec3 rel = e.where - me;
      const float dist = length(rel);
      const float pan = dist > 1.0f ? std::clamp(dot(rel / dist, right) * 0.8f, -0.8f, 0.8f) : 0.0f;
      const float near = std::clamp(1.0f - dist / 2500.0f, 0.0f, 1.0f);
      switch (e.type) {
        case CombatEvent::Type::ShotFired:
          if (++shots > 6) break;  // a volley is one sound, not twelve
          if (e.byPlayer) audio_.event(e.sound, 0.8f, 0.95f + 0.1f * float(shots % 3) * 0.5f, pan * 0.3f);
          else if (near > 0) audio_.event(e.sound, 0.6f * near, 0.9f, pan);
          break;
        case CombatEvent::Type::PlayerHit:
          audio_.event(e.sound, 1.0f, 0.9f + 0.2f * float(e.amount > 0), pan);
          if (e.amount > 0) {
            damageFlashUntil_ = runTime_ + 0.35;
            combatShake_ = std::min(1.0f, combatShake_ + 0.25f + e.amount / 30.0f);
          } else {
            shieldFlashUntil_ = runTime_ + 0.25;
            combatShake_ = std::min(1.0f, combatShake_ + 0.08f);
          }
          break;
        case CombatEvent::Type::NpcHit:
          if (near > 0) audio_.event(e.sound, 0.5f * near, 1.15f, pan);
          break;
        case CombatEvent::Type::ShipDestroyed:
          audio_.event("explosion", 0.35f + 0.65f * near, 0.9f, pan);
          if (dist < 400.0f) combatShake_ = std::min(1.0f, combatShake_ + 0.4f * (1.0f - dist / 400.0f));
          break;
        case CombatEvent::Type::PlayerDestroyed:
          audio_.event("explosion", 1.0f, 0.7f);
          combatShake_ = 1.0f;
          break;
        case CombatEvent::Type::Engaged:
          audio_.event("alarm", 0.7f);
          audio_.event("radio");
          break;
        case CombatEvent::Type::Escaped: break;
      }
    }
    // Missile lock warning: an incoming homing rocket close by.
    bool incoming = false;
    for (const auto& pr : session_->combat().projectiles())
      incoming |= pr.homingTarget && *pr.homingTarget == kPlayerId && length(pr.pos - me) < 1200.0f;
    if (incoming && runTime_ > missileWarnUntil_) {
      audio_.event("warning", 0.8f, 1.3f);
      missileWarnUntil_ = runTime_ + 0.6;
    }
    combatShake_ = std::max(0.0f, combatShake_ - dt * 2.5f);
  }
  if (market_.isOpen() != w.marketOpen) {
    audio_.event("ui", 1.0f, market_.isOpen() ? 1.0f : 0.8f);
    w.marketOpen = market_.isOpen();
  }
  if (session_->credits() != w.credits) {
    if (market_.isOpen()) audio_.event("ui", 0.8f, 1.25f);
    w.credits = session_->credits();
  }
  AudioState st;
  const AssetID* surfaceId = session_->surfaceSceneId();
  st.location = loc == Location::Cockpit ? (surfaceId ? *surfaceId : std::string("space")) : session_->activeInteriorId();
  const Landing& ld = session_->landing();
  const TravelDrive& tr = session_->travel();
  st.inCockpit = loc == Location::Cockpit && d.state() != DockState::Docked && ld.state() != LandState::Landed;
  st.engineActive = st.inCockpit;
  st.engineDemand = engineDemand;
  st.speedFraction = speedFraction;
  if (tr.active()) {  // the drive howls with speed
    st.engineDemand = 1.0f;
    st.speedFraction = 1.0f + std::clamp(tr.speed() / world().player().def.travel.maxSpeed, 0.0f, 1.0f) * 1.5f;
  }
  if (ld.busy() && ld.state() != LandState::Landed) {  // re-entry roar, landing burn
    st.engineDemand = std::max(0.6f, ld.heat());
    st.speedFraction = 0.4f + ld.heat();
  }
  st.beamActive = session_->mining().beamActive();
  audio_.update(st, dt);
}

void GameApp::handleAppInput() {
  const Transform ship = world().playerRenderTransform(1.0f);
  const bool cockpit = session_->location() == Location::Cockpit;
  handleMenuActions();
  if (menus_.capturingKey()) return;  // F10, F11... are being offered to the rebind prompt
  if (input_.pressed("app.quit")) quit_ = true;
  if (menus_.active()) {  // menus own input; the simulation is paused (see run())
    if (input_.pressed("debug.screenshot"))
      pendingScreenshot_ = std::filesystem::path("screenshots") / std::format("farhorizon_{:03}.png", screenshotCounter_++);
    if (input_.pressed("window.fullscreen")) {
      window_->toggleFullscreen();
      settings_.fullscreen = window_->isFullscreen();
      persistSettings();
    }
    return;
  }
  if (market_.isOpen()) {  // the terminal owns the keyboard while open (MarketPanel::update)
    if (input_.pressed("debug.screenshot"))
      pendingScreenshot_ = std::filesystem::path("screenshots") / std::format("farhorizon_{:03}.png", screenshotCounter_++);
    return;
  }
  if (input_.pressed("game.save")) saveGame("saves/quicksave.json");
  if (input_.pressed("game.load")) {
    loadGame("saves/quicksave.json");
    return;
  }
  if (input_.pressed("app.menu") && !scripted_) {
    openMenu(MenuSystem::Screen::Pause);
    return;
  }
  if (input_.pressed("app.toggleMouse")) setMouseCapture(!mouseCaptured_);
  // Clicking into the game window takes the mouse back (the click is not a shot).
  if (!mouseCaptured_ && input_.mouseLeftPressed() && !scripted_ && !camera_.isDev()) {
    setMouseCapture(true);
    suppressFireUntilRelease_ = true;
  }
  if (input_.pressed("debug.overlay")) {
    showDevOverlay_ = !showDevOverlay_;
    settings_.showDevOverlay = showDevOverlay_;
    persistSettings();
  }
  if (input_.pressed("window.fullscreen")) {
    window_->toggleFullscreen();
    settings_.fullscreen = window_->isFullscreen();
    persistSettings();
  }
  if (input_.pressed("debug.screenshot"))
    pendingScreenshot_ = std::filesystem::path("screenshots") / std::format("farhorizon_{:03}.png", screenshotCounter_++);
  if (input_.pressed("visual.cycleLook") && !looks_.profiles.empty()) {
    activeLook_ = (activeLook_ + 1) % int(looks_.profiles.size());
    lookShownUntil_ = runTime_ + 2.0;
    settings_.look = looks_.profiles[size_t(activeLook_)].id;
    persistSettings();
    log::info("render", "look: {}", looks_.profiles[size_t(activeLook_)].name);
  }
  if (input_.pressed("interact")) session_->interact();
  if (session_->consumeMarketRequest()) openMarket();
  if (input_.pressed("ui.market") && cockpit) {
    if (MarketPanel::available(*session_)) openMarket();
    else session_->notify("Terminals are available while docked or landed");
  }
  if (cockpit) {
    if (input_.pressed("camera.cycle")) camera_.cycleShipCamera(ship);
    if (input_.pressed("camera.dev")) camera_.toggleDev(ship);
    if (input_.pressed("camera.autoRotate") && camera_.mode() == CameraMode::Orbit) camera_.toggleAutoRotate();  // O
    if (input_.pressed("target.next")) world().cycleTarget();
    if (input_.pressed("target.nearestAsteroid")) world().targetNearestAsteroid();
    if (input_.pressed("target.clear")) world().clearTarget();
    if (input_.pressed("target.nearestHostile") && !session_->combat().targetNearestHostile(world()))
      session_->notify("No ships on scope");
    if (input_.pressed("weapon.guns") && miningMode_) {
      miningMode_ = false;
      session_->notify("Weapons: PULSE LASERS");
    }
    if (input_.pressed("weapon.mining") && !miningMode_) {
      miningMode_ = true;
      session_->notify("Weapons: MINING LASER");
    }
    if (input_.pressed("debug.autofly")) controller_.setAutofly(!controller_.autofly(), 0.4f, {0.0f, 0.08f, 0.0f});
    if (input_.pressed("nav.next")) {
      const auto dests = world().navDestinations();
      if (!dests.empty()) {
        size_t next = 0;
        if (const auto& t = world().target())
          for (size_t i = 0; i < dests.size(); ++i)
            if (dests[i] == *t) next = (i + 1) % dests.size();
        world().setTarget(dests[next]);
        if (auto info = world().targetInfo()) session_->notify("NAV: " + info->name);
      }
    }
    if (input_.pressed("travel.engage")) {
      if (session_->travel().active()) session_->travel().cancel(world());
      else session_->engageTravel();
    }
    if (input_.pressed("dock.request")) {  // L: contextual dock / land / launch / undock
      Docking& d = session_->docking();
      Landing& ld = session_->landing();
      if (ld.state() == LandState::Landed) {
        session_->launch();
      } else if (!ld.busy()) {
        switch (d.state()) {
          case DockState::Undocked: {
            // Prefer a landing when a site's orbit marker is near; otherwise dock.
            bool siteNear = false;
            const DVec3 me = world().frame().toSystem(world().player().state.position).metres;
            for (const auto& pl : world().planets())
              for (std::uint32_t si = 0; si < pl.def.sites.size(); ++si)
                siteNear = siteNear || length(pl.siteOrbit(si) - me) <= Landing::kRequestRange;
            if (siteNear) session_->requestLanding();
            else d.request(world());
            break;
          }
          case DockState::RequestingClearance:
          case DockState::ApproachAuthorized: d.cancel(); break;
          case DockState::Docked: d.undock(world()); break;
          default: break;
        }
      }
    }
    if (input_.pressed("dock.autopilot")) session_->docking().toggleAutopilot();
    if (input_.pressed("debug.forceDock")) session_->docking().forceDock(world());
    if (input_.pressed("ship.scan")) {
      const auto& t = world().target();
      if (t && t->kind == TargetRef::Kind::Asteroid) session_->mining().startScan(t->index, world().simulationTime());
      else session_->notify("Target an asteroid to scan");
    }
  }
  if (input_.pressed("debug.reloadAssets")) {
    std::vector<std::string> errors;
    if (!cockpit) {
      contentErrors_ = {"Reload is only available from the pilot seat"};
    } else if (loadContent(errors)) {
      log::info("content", "hot reload OK");
      contentErrors_ = errors;  // may contain warnings
    } else {
      log::error("content", "hot reload failed; keeping previous content");
      contentErrors_ = errors;
      contentErrors_.insert(contentErrors_.begin(), "RELOAD FAILED - previous content kept:");
    }
  }
}

void GameApp::handleFootInput(float /*dt*/) {
  CharacterInput& ci = session_->characterInput();
  auto axis = [&](const char* neg, const char* pos) {
    return (input_.down(pos) ? 1.0f : 0.0f) - (input_.down(neg) ? 1.0f : 0.0f);
  };
  ci.move = {axis("foot.left", "foot.right"), axis("foot.back", "foot.forward")};
  ci.run = input_.down("foot.run");
  if (input_.pressed("foot.jump")) ci.jump = true;
  if (mouseCaptured_) {
    const float sens = 0.0025f;
    ci.yawDelta += input_.mouseDx() * sens;
    ci.pitchDelta -= input_.mouseDy() * sens * (input_.mouse().invertY ? -1.0f : 1.0f);
  }
}

void GameApp::buildDevOverlay() {
  const FrameSummary s = stats_.windowSummary();
  const RenderStats& rs = renderer_->stats();
  const PlayerShip& p = world().player();
  const SystemPosition sys = world().frame().toSystem(p.state.position);
  const VkExtent2D ext = renderer_->extent();
  const OverlayColor normal{205, 215, 225, 230}, dim{140, 150, 165, 230}, bad{255, 110, 100, 255};
  std::vector<std::pair<std::string, OverlayColor>> lines;
  lines.push_back({std::format("FARHORIZON 0.7  {}  {}x{}  vsync {}  val {}", ctx_->deviceName(), ext.width, ext.height,
                               ctx_->vsync() ? "on" : "off", ctx_->validationEnabled() ? "on" : "off"),
                   dim});
  lines.push_back({std::format("FPS {:5.1f}  CPU {:5.2f} ms (max {:5.2f})  GPU {}", s.avgFps, s.avgCpuMs, s.maxCpuMs,
                               rs.gpuMs >= 0 ? std::format("{:5.2f} ms", rs.gpuMs) : std::string("n/a")),
                   normal});
  lines.push_back({std::format("draws {}  tris {}  instances {}  asteroids vis {}/{}", rs.drawCalls, rs.triangles,
                               rs.meshInstances, visibleAsteroids_, world().asteroids().size()),
                   normal});
  lines.push_back({std::format("GPU mem {:.1f} MB   origin gen {} ({} this run)  local {:.0f} m",
                               double(gpu::allocatedBytes()) / (1024.0 * 1024.0), world().frame().generation(), rebases_,
                               length(p.state.position)),
                   normal});
  lines.push_back({std::format("star dist {}   impacts {}", formatDistance(length(sys.metres - scene_.star.position.metres)),
                               p.collisions),
                   normal});
  const int vkMsgs = VkContext::validationMessageCount();
  lines.push_back({std::format("validation {}  log warn {} err {}", vkMsgs, log::warningCount(), log::errorCount()),
                   vkMsgs > 0 ? bad : dim});
  for (const auto& e : contentErrors_) lines.push_back({e, bad});
  lines.push_back({"W/S throttle  mouse/arrows steer  A/D Space/Ctrl strafe  Q/E roll  Shift boost", dim});
  lines.push_back({"LMB fire/mine  RMB rocket  1/2 guns/mining  Y nearest hostile  Alt mine  R scan", dim});
  lines.push_back({"M terminal  L dock  K autopilot  F use  F7 save  F8 load", dim});
  lines.push_back({"Z assist  B precision  C recenter  T target  G nearest rock  V camera  F2 dev cam", dim});
  lines.push_back({"Esc menu  ` mouse  F1 info  F3 autofly  F4 look  F5 reload  F11 fullscreen  F12 shot  F10 quit", dim});

  const float scale = 1.5f;
  const float lineH = 11.0f * scale;
  float width = 0;
  for (auto& [t, c] : lines) width = std::max(width, OverlayBuilder::textWidth(t, scale));
  overlay_.rect(8, 40, width + 16, lineH * float(lines.size()) + 12, {8, 10, 16, 150});
  float y = 46;
  for (auto& [t, c] : lines) {
    overlay_.text(16, y, t, c, scale);
    y += lineH;
  }
}

void GameApp::buildOverlay(const CameraView& view, const Mat4& viewProj, const Transform& ship) {
  overlay_.clear();
  const VkExtent2D ext = renderer_->extent();
  HudContext hc{world(), controller_, world().player().input, ship, view, camera_.mode(), viewProj,
                float(ext.width), float(ext.height), mouseCaptured_, runTime_};
  const VisualProfile& look = looks_.profiles[size_t(activeLook_)];
  hc.palette = look.hud;
  hc.lookName = look.name;
  hc.lookShownUntil = lookShownUntil_;
  hc.session = session_.get();
  hc.miningMode = miningMode_;
  hc.damageFlash = float(std::clamp((damageFlashUntil_ - runTime_) / 0.35, 0.0, 1.0));
  hc.shieldFlash = float(std::clamp((shieldFlashUntil_ - runTime_) / 0.25, 0.0, 1.0)) * 0.6f;
  hc.missileWarning = runTime_ < missileWarnUntil_ + 0.3;
  hc.showContractTracker = settings_.showContractTracker;
  hc.showHints = settings_.showHints;
  hc.uiScale = settings_.uiScale;
  hc.theme = &theme_;
  hc.bindings = &input_;
  if (menus_.active()) {  // the world holds still behind the menu; the HUD would only compete with it
    buildMenus();
    return;
  }
  if (market_.isOpen()) {  // the terminal takes the whole screen's attention
    ui_.begin(overlay_, theme_, float(ext.width), float(ext.height), settings_.uiScale, uiInput(), runTime_);
    market_.update(ui_, input_, *session_);
    ui_.end();
    if (!market_.isOpen()) closeMarket();
    return;
  }
  if (session_->location() == Location::Cockpit && session_->surfaceScene()) {
    InteriorHudContext sc{*session_, float(ext.width), float(ext.height), mouseCaptured_, runTime_, look.hud,
                          settings_.uiScale, &theme_, &input_, settings_.showContractTracker, settings_.showHints};
    drawSurfaceHud(overlay_, sc);
  } else if (session_->location() == Location::Cockpit) {
    drawHud(overlay_, hc);
  } else {
    InteriorHudContext ic{*session_, float(ext.width), float(ext.height), mouseCaptured_, runTime_, look.hud,
                          settings_.uiScale, &theme_, &input_, settings_.showContractTracker, settings_.showHints};
    drawInteriorHud(overlay_, ic);
  }
  if (showDevOverlay_) buildDevOverlay();
}

int GameApp::run() {
  using Clock = std::chrono::steady_clock;
  auto last = Clock::now();
  const auto start = last;
  const double benchSeconds = benchmark_ ? benchmark_->durationSeconds : 0.0;
  if (benchmark_) {
    stats_.setRecording(true);
    log::info("bench", "benchmark '{}' for {:.1f} s", benchmark_->id, benchSeconds);
  }

  while (!quit_) {
    const auto now = Clock::now();
    const double dt = std::min(0.25, std::chrono::duration<double>(now - last).count());
    last = now;
    runTime_ = std::chrono::duration<double>(now - start).count();

    if (uiScript_.active()) {
      uiScript_.step({[this](const std::filesystem::path& p) { pendingScreenshot_ = p; },
                      [this](const std::string& a) { return input_.bindingNames(a); },
                      [this] {
                        using S = MenuSystem::Screen;
                        switch (menus_.screen()) {
                          case S::None: return std::string("none");
                          case S::Main: return std::string("main");
                          case S::Pause: return std::string("pause");
                          case S::Options: return std::string("options");
                          case S::Load: return std::string("load");
                          case S::Save: return std::string("save");
                          default: return std::string("confirm");
                        }
                      },
                      [this] { return market_.isOpen(); }, [this] { quit_ = true; }});
    }
    if (!window_->pollEvents(input_)) break;
    if (window_->isMinimized()) continue;  // keep pending resize until restored (no 0x0 swapchain)
    if (window_->consumeResized()) {
      ctx_->recreateSwapchain();
      mouseSettleFrames_ = 8;
    }
    if (mouseCaptured_ && !window_->hasFocus()) setMouseCapture(false);

    handleAppInput();
    const bool simPaused = menus_.active();
    const bool cockpit = session_->location() == Location::Cockpit;
    const bool devCam = camera_.isDev();
    const bool freeCam = camera_.mode() == CameraMode::Free;
    if (simPaused) {
      // Menus: the world holds still behind them.
    } else if (cockpit) {
      controller_.update(input_, float(dt), mouseCaptured_ && !devCam, world().player().state.boostEnergy,
                         world().player().input);
    } else {
      handleFootInput(float(dt));
      if (benchmark_ && benchmark_->lookTurnDegPerSec != 0.0f)
        session_->characterInput().yawDelta += radians(benchmark_->lookTurnDegPerSec) * float(dt);
      world().player().input = FlightInput{};  // nobody at the controls
    }
    if (cockpit && freeCam) {  // WASD drives the free camera; the ship holds its set-speed and stops turning
      FlightInput hold;
      hold.throttle = controller_.throttle();
      hold.assist = controller_.assist();
      hold.mode = controller_.mode();
      world().player().input = hold;
    }

    {  // Triggers: LMB fires the selected system (guns or mining laser), RMB rockets.
      const bool controls = cockpit && !market_.isOpen() && !devCam;
      if (!input_.mouseLeft()) suppressFireUntilRelease_ = false;
      const bool lmb = mouseCaptured_ && input_.mouseLeft() && !suppressFireUntilRelease_;
      const bool rmb = mouseCaptured_ && input_.mouseRight();
      session_->setMiningTrigger(controls && ((miningMode_ && lmb) || input_.down("ship.mine")));
      session_->setFireTriggers(controls && ((!miningMode_ && lmb) || input_.down("ship.firePrimary")),
                                controls && (rmb || input_.down("ship.fireSecondary")));
    }
    if (benchmark_ && benchmark_->mine && cockpit) {  // scripted mining run: face the nearest rock and cut
      if (!world().target()) world().targetNearestAsteroid();
      if (const auto& t = world().target(); t && t->kind == TargetRef::Kind::Asteroid) {
        auto& st = world().player().state;
        const Vec3 f = normalize(world().asteroids()[t->index].local - st.position);
        const Vec3 axis = cross(Vec3{0, 0, -1}, f);
        const float ang = std::acos(std::clamp(dot(Vec3{0, 0, -1}, f), -1.0f, 1.0f));
        st.orientation = length(axis) > 1e-5f ? Quat::fromAxisAngle(normalize(axis), ang) : Quat{};
        st.angularVelocity = {};
        if (!session_->mining().isScanned(t->index) && !session_->mining().scanning())
          session_->mining().startScan(t->index, world().simulationTime());
      }
      session_->setMiningTrigger(true);
    }
    if (benchmark_ && benchmark_->combat && cockpit) {  // scripted skirmish: perfect aim at the nearest hostile
      Combat& cb = session_->combat();
      const auto& t = world().target();
      if (!t || t->kind != TargetRef::Kind::Ship || !cb.npc(t->index)) cb.targetNearestHostile(world());
      bool fire = false, rockets = false;
      if (const auto& t2 = world().target(); t2 && t2->kind == TargetRef::Kind::Ship)
        if (auto lead = cb.leadPoint(world(), t2->index)) {
          auto& st = world().player().state;
          const Vec3 f = normalize(*lead - st.position);
          const Vec3 axis = cross(Vec3{0, 0, -1}, f);
          const float ang = std::acos(std::clamp(dot(Vec3{0, 0, -1}, f), -1.0f, 1.0f));
          st.orientation = length(axis) > 1e-5f ? Quat::fromAxisAngle(normalize(axis), ang) : Quat{};
          st.angularVelocity = {};
          const float d = length(*lead - st.position);
          fire = d < 1000.0f && !cb.playerArms().overheated;
          rockets = d < 1400.0f;
          controller_.setThrottle(d > 700.0f ? 0.6f : 0.0f);  // close in, then hold
        }
      session_->setFireTriggers(fire, rockets);
    }
    const int steps = simPaused ? 0 : clock_.advance(dt);
    for (int i = 0; i < steps; ++i) {
      const auto delta = session_->fixedUpdate(float(clock_.step()));
      if (delta) {
        camera_.applyRebase(*delta);
        ++rebases_;
      }
      const DockState ds = session_->docking().state();
      if (ds != lastDockState_ && ds == DockState::Docked) {
        controller_.setThrottle(0.0f);  // no surprise launch
        if (!benchmark_) saveGame("saves/autosave.json", true);
      }
      if (benchmark_ && benchmark_->autoLand && !session_->landing().busy() && session_->landing().message().empty())
        session_->requestLanding();
      if (benchmark_ && benchmark_->autoDock) {
        Docking& d = session_->docking();
        if (ds == DockState::Undocked && d.message().empty()) d.request(world());
        if (ds == DockState::ApproachAuthorized && !d.autopilot()) d.toggleAutopilot();
      }
      lastDockState_ = ds;
      camera_.fixedUpdate(float(clock_.step()));
    }

    // ---- Camera and render data (camera-relative) ----
    Transform ship = world().playerRenderTransform(float(clock_.alpha()));
    const InteriorDef* surface = cockpit ? session_->surfaceScene() : nullptr;
    if (surface && surface->parkedShip) {  // landing approach / landed / liftoff happen in the site scene
      const auto& ps = *surface->parkedShip;
      const Quat padRot = Quat::fromAxisAngle({0, 1, 0}, radians(ps.yawDeg));
      const Transform off = session_->landing().surfaceOffset();
      ship = {ps.position + padRot.rotate(off.position), normalize(padRot * off.rotation)};
    }
    CameraView view = camera_.update(ship, world().player(), input_, float(dt),
                                     (devCam || !mouseCaptured_) && !menus_.active());
    if (camera_.mode() == CameraMode::Chase || camera_.mode() == CameraMode::Cockpit)
      view.fovDeg = std::clamp(view.fovDeg + settings_.fovOffsetDeg, 40.0f, 110.0f);
    {  // re-entry / touchdown shake and transit field-of-view stretch
      const float heat = session_->landing().heat();
      const bool approach = session_->landing().state() == LandState::SurfaceApproach;
      const float shake = (heat * 0.012f + (approach ? 0.004f : 0.0f) + combatShake_ * combatShake_ * 0.02f) *
                          (settings_.cameraShake ? 1.0f : 0.0f);
      if (shake > 0) {
        const float t = float(runTime_);
        view.rotation = normalize(view.rotation * Quat::fromAxisAngle({1, 0, 0}, shake * std::sin(t * 41.0f)) *
                                  Quat::fromAxisAngle({0, 1, 0}, shake * std::sin(t * 29.0f + 1.3f)));
      }
      const TravelState ts = session_->travel().state();
      if (ts == TravelState::Transit || ts == TravelState::Decelerating)
        view.fovDeg += 14.0f * std::clamp(session_->travel().speed() / world().player().def.travel.maxSpeed, 0.0f, 1.0f);
    }
    const VkExtent2D ext = renderer_->extent();
    RenderFrameParams params;
    params.viewRotation = view.viewRotation();
    params.fovYRadians = radians(view.fovDeg);
    params.nearPlane = 0.05f;
    const SystemPosition camSys = world().frame().toSystem(view.position);
    params.sunDirection = Vec3(normalize(scene_.star.position.metres - camSys.metres));
    params.sunColor = scene_.star.color * scene_.star.intensity;
    params.sunAngularRadiusRadians = radians(scene_.star.angularDiameterDeg * 0.5f);
    params.ambientSky = scene_.ambient.skyColor;
    params.ambientGround = scene_.ambient.groundColor;
    params.exposure = scene_.exposure;
    params.timeSeconds = float(world().simulationTime());
    params.galacticTiltRadians = radians(scene_.starfield.galacticPlaneTiltDeg);
    params.skyBase = scene_.sky.baseColor;
    params.skyBand = scene_.sky.bandColor;
    params.skyBandIntensity = scene_.sky.bandIntensity;
    params.skyNebula = scene_.sky.nebulaColor;
    params.skyNebulaStrength = scene_.sky.nebulaStrength;
    params.fillDirection = scene_.ambient.fillDirection;
    params.fillColor = scene_.ambient.fillColor;
    params.rimColor = scene_.ambient.rimColor;
    params.rimPower = scene_.ambient.rimPower;
    {
      const VisualProfile& v = looks_.profiles[size_t(activeLook_)];
      PostProcessParams& pp = params.post;
      pp.bloomStrength = v.bloomStrength;
      pp.bloomThreshold = v.bloomThreshold;
      pp.bloomKnee = v.bloomKnee;
      pp.bloomRadius = v.bloomRadius;
      pp.bloomIterations = v.bloomIterations;
      pp.shadowTint = v.shadowTint;
      pp.highlightTint = v.highlightTint;
      pp.saturation = v.saturation;
      pp.contrast = v.contrast;
      pp.vignette = v.vignette;
      pp.scanlines = v.scanlines;
      pp.scanlinePeriod = v.scanlinePeriod * (ext.height >= 1440 ? 2.0f : 1.0f);  // keep lines visible but fine
      pp.chromaticAberration = v.chromaticAberration;
      pp.grain = v.grain;
      pp.fadeColor = {0.78f, 0.62f, 0.48f};  // dust and cloud
      pp.fadeAmount = session_->landing().fade();
      pp.heat = session_->landing().heat();
    }
    for (const auto& pl : world().planets()) {
      PlanetDraw pd;
      pd.center = world().frame().toLocal(pl.def.center) - view.position;
      pd.radius = float(pl.def.radius);
      pd.colorA = pl.def.colorA;
      pd.colorB = pl.def.colorB;
      pd.seed = pl.def.seed;
      pd.craters = pl.def.craters;
      pd.atmosphereColor = pl.def.atmosphereColor;
      pd.atmosphereThickness = pl.def.atmosphereThickness;
      params.planets.push_back(pd);
    }
    const Mat4 viewProj = Mat4::perspectiveReversedZInfinite(params.fovYRadians,
                                                             float(ext.width) / float(std::max(1u, ext.height)),
                                                             params.nearPlane) *
                          params.viewRotation;
    const Frustum frustum = Frustum::fromViewProj(viewProj);

    std::vector<MeshDrawItem> items;
    items.reserve(world().asteroids().size() + world().props().size() + 1);
    const PlayerShip& player = world().player();
    items.push_back({player.visual, Mat4::translation(ship.position - view.position) * Mat4::rotation(ship.rotation) *
                                        player.meshFix});
    for (const auto& p : world().props()) {
      const Vec3 rel = p.local - view.position;
      if (!frustum.sphereVisible(rel, p.collisionRadius * 1.5f)) continue;
      items.push_back({p.visual, Mat4::translation(rel) * Mat4::rotation(p.rotation) * p.meshFix});
    }
    visibleAsteroids_ = 0;
    for (const auto& a : world().asteroids()) {
      const Vec3 rel = a.local - view.position;
      if (!frustum.sphereVisible(rel, a.radius)) continue;
      ++visibleAsteroids_;
      items.push_back({a.visual, Mat4::translation(rel) * Mat4::rotation(a.rotation) *
                                     Mat4::scale({a.radius, a.radius, a.radius})});
    }

    float engineDemandForAudio = 0, speedFractionForAudio = 0;
    // ---- Glow sprites: engine exhaust + running lights ----
    std::vector<GlowSprite> glows;
    {
      const double t = world().simulationTime();
      const EngineGlowDef& eg = player.def.engineGlow;
      // Exhaust brightness follows forward thrust demand (set-speed error or direct thrust) and boost.
      const FlightInput& fin = player.input;
      const float fwdSpeed = dot(player.state.velocity, ship.rotation.forward());
      const float cap = flightSpeedCap(player.def.flight, fin, player.state.boosting);
      float demand = fin.assist ? std::clamp((fin.throttle * cap - fwdSpeed) / (0.15f * cap) + fin.throttle * 0.6f, 0.0f, 1.0f)
                                : std::max(0.0f, fin.thrust.z);
      if (player.state.boosting) demand = 1.0f;
      engineDemandForAudio = demand;
      speedFractionForAudio = std::clamp(length(player.state.velocity) / std::max(1.0f, cap), 0.0f, 2.0f);
      const Vec3 col = player.state.boosting ? eg.boostColor : eg.color;
      const float size = eg.idleSize + (eg.thrustSize - eg.idleSize) * demand * (player.state.boosting ? 1.4f : 1.0f);
      const float intensity = eg.idleIntensity + (eg.thrustIntensity - eg.idleIntensity) * demand;
      for (const auto& sname : eg.sockets)
        if (auto it = player.sockets.find(sname); it != player.sockets.end()) {
          const Vec3 wp = ship.position + ship.rotation.rotate(it->second.position) - view.position;
          glows.push_back({Vec4(wp, size), Vec4(col * intensity, 0)});
        }
      for (const auto& l : player.def.lights) {
        const float k = l.intensityAt(t);
        if (k <= 0) continue;
        const Vec3 wp = ship.position + ship.rotation.rotate(l.position) - view.position;
        glows.push_back({Vec4(wp, l.size), Vec4(l.color * k, 0)});
      }
      for (const auto& p : world().props())
        for (const auto& l : p.def.lights) {
          const float k = l.intensityAt(t);
          if (k <= 0) continue;
          const Vec3 wp = p.local + p.rotation.rotate(l.position) - view.position;
          if (l.range > 0) params.pointLights.push_back({wp, l.range, l.color * (k * l.intensity)});
          if (!frustum.sphereVisible(wp, l.size)) continue;
          glows.push_back({Vec4(wp, l.size), Vec4(l.color * k, 0)});
        }
    }

    // ---- Mining beam, impact flare and ore chunks ----
    if (cockpit) {
      const Mining& mining = session_->mining();
      const double t = world().simulationTime();
      if (mining.beamActive()) {
        const Vec3 end = mining.beamEnd();
        for (const auto& mount : player.def.mounts) {
          auto it = player.sockets.find(mount);
          if (it == player.sockets.end()) continue;
          const Vec3 beamStart = ship.position + ship.rotation.rotate(it->second.position);
          const float len = length(end - beamStart);
          const int n = std::clamp(int(len / 1.2f), 8, 400);
          for (int k = 0; k <= n; ++k) {
            const float f = float(k) / float(n);
            const float pulse = 0.75f + 0.25f * std::sin(float(t) * 40.0f - f * 30.0f);
            glows.push_back({Vec4(lerp(beamStart, end, f) - view.position, 0.35f + 0.25f * f),
                             Vec4(Vec3{3.2f, 1.1f, 0.35f} * pulse, 0)});
          }
        }
        const float flicker = 0.7f + 0.3f * std::sin(float(t) * 53.0f) * std::sin(float(t) * 17.0f);
        glows.push_back({Vec4(end - view.position, 5.0f * flicker), Vec4(Vec3{4.0f, 1.6f, 0.5f} * flicker, 0)});
        // The cutting point lights the rock face (and the ship's nose when close).
        params.pointLights.push_back({end - view.position, 70.0f, Vec3{900.0f, 330.0f, 90.0f} * flicker});
      }
      const VisualHandle chunkMesh = world().fields().empty() || world().fields()[0].variants.empty()
                                         ? player.visual
                                         : world().fields()[0].variants[0];
      for (const auto& c : mining.chunks()) {
        const Vec3 rel = c.local - view.position;
        if (!frustum.sphereVisible(rel, 2.0f)) continue;
        const Quat spin = Quat::fromAxisAngle(normalize(Vec3{0.3f, 1.0f, 0.2f}), float(t - c.spawnTime) * 1.7f);
        items.push_back({chunkMesh, Mat4::translation(rel) * Mat4::rotation(spin) * Mat4::scale({0.8f, 0.8f, 0.8f})});
        const bool salvage = c.commodity.rfind("goods.", 0) == 0;  // canisters from wrecks blink cyan
        const float blink = salvage ? 0.6f + 0.4f * std::sin(float(t) * 6.0f + float(c.spawnTime)) : 1.0f;
        glows.push_back({Vec4(rel, salvage ? 1.4f : 1.0f),
                         salvage ? Vec4(Vec3{0.3f, 1.3f, 1.6f} * blink, 0) : Vec4(1.4f, 0.7f, 0.25f, 0)});
      }
    }

    // ---- Combat: other ships, bolts, rockets, explosions and debris ----
    if (cockpit && !surface) {
      const Combat& combat = session_->combat();
      const float alpha = float(clock_.alpha());
      const double t = world().simulationTime();
      for (const auto& n : combat.npcs()) {
        const ShipType* type = combat.shipType(n.shipDef);
        if (!type) continue;
        const Vec3 pos = lerp(n.previous.position, n.state.position, alpha);
        const Quat rot = nlerp(n.previous.orientation, n.state.orientation, alpha);
        const Vec3 rel = pos - view.position;
        if (!frustum.sphereVisible(rel, type->radius * 2.0f)) continue;
        if (type->visual) items.push_back({type->visual, Mat4::translation(rel) * Mat4::rotation(rot) * type->meshFix});
        const EngineGlowDef& eg = type->def.engineGlow;
        const float demand = n.state.boosting ? 1.0f : std::clamp(n.input.throttle, 0.0f, 1.0f);
        const Vec3 col = n.state.boosting ? eg.boostColor : eg.color;
        for (const auto& sname : eg.sockets)
          if (auto it = type->def.sockets.find(sname); it != type->def.sockets.end())
            glows.push_back({Vec4(rel + rot.rotate(it->second.position), eg.idleSize + (eg.thrustSize - eg.idleSize) * demand),
                             Vec4(col * (eg.idleIntensity + (eg.thrustIntensity - eg.idleIntensity) * demand), 0)});
        for (const auto& l : type->def.lights) {
          const float k = l.intensityAt(t + double(n.id) * 0.37);
          if (k > 0) glows.push_back({Vec4(rel + rot.rotate(l.position), l.size), Vec4(l.color * k, 0)});
        }
      }
      for (const auto& pr : combat.projectiles()) {
        const Vec3 rel = pr.pos - view.position;
        if (!frustum.sphereVisible(rel, pr.def->boltLength + 2.0f)) continue;
        const float speed = length(pr.vel);
        const Vec3 back = speed > 1e-3f ? -pr.vel / speed : Vec3{0, 0, 1};
        if (pr.def->homing > 0) {  // rocket: hot head + fading exhaust trail
          glows.push_back({Vec4(rel, 1.6f), Vec4(pr.def->color * 1.5f, 0)});
          for (int k = 1; k <= 8; ++k)
            glows.push_back({Vec4(rel + back * (float(k) * 2.2f), 0.9f + 0.18f * float(k)),
                             Vec4(Vec3{1.2f, 0.8f, 0.5f} * (0.5f / float(k)), 0)});
          params.pointLights.push_back({rel, 40.0f, Vec3{300.0f, 200.0f, 120.0f}});
        } else {  // bolt: a short bright streak
          const int n = 6;
          for (int k = 0; k < n; ++k) {
            const float f = float(k) / float(n - 1);
            glows.push_back({Vec4(rel + back * (f * pr.def->boltLength), 0.55f - 0.25f * f),
                             Vec4(pr.def->color * (1.0f - 0.6f * f), 0)});
          }
        }
      }
      for (const auto& e : combat.explosions()) {
        const float age = float(t - e.time);
        if (age < 0) continue;
        const float fade = std::exp(-age * 2.2f);
        const Vec3 rel = e.pos - view.position;
        if (!frustum.sphereVisible(rel, e.size * 3.0f)) continue;
        glows.push_back({Vec4(rel, e.size * (0.35f + age * 1.2f)), Vec4(Vec3{5.0f, 2.2f, 0.7f} * fade, 0)});
        glows.push_back({Vec4(rel, e.size * 0.25f), Vec4(Vec3{6.0f, 5.0f, 4.0f} * std::exp(-age * 6.0f), 0)});
        params.pointLights.push_back({rel, e.size * 10.0f, Vec3{2500.0f, 1100.0f, 350.0f} * (fade * e.size / 20.0f)});
        if (e.size >= 8.0f) {  // hull debris: sparks flung outward, cooling to red
          std::uint32_t h = std::uint32_t(e.time * 1000.0) * 2654435761u;
          for (int k = 0; k < 18; ++k) {
            auto rnd = [&] {
              h ^= h << 13;
              h ^= h >> 17;
              h ^= h << 5;
              return float(h & 0xFFFF) / 65535.0f - 0.5f;
            };
            const Vec3 dir = normalize(Vec3{rnd(), rnd(), rnd()} + Vec3{1e-3f, 0, 0});
            const float speed = e.size * (1.5f + 2.5f * (rnd() + 0.5f));
            const float cool = std::exp(-age * 1.1f);
            glows.push_back({Vec4(rel + dir * (speed * age), 0.5f + 0.4f * cool),
                             Vec4(Vec3{3.0f * cool + 0.4f, 1.1f * cool * cool, 0.25f * cool * cool * cool} * cool, 0)});
          }
        }
      }
    }

    std::sort(params.pointLights.begin(), params.pointLights.end(), [](const PointLight& a, const PointLight& b) {
      return dot(a.position, a.position) < dot(b.position, b.position);
    });

    // ---- On foot (or in the cockpit on a planet's surface): render the local scene instead ----
    if (!cockpit || surface) {
      const InteriorDef* idef = surface ? surface : session_->activeInterior();
      const AssetID sceneId = surface ? *session_->surfaceSceneId() : session_->activeInteriorId();
      if (idef) {
        if (!surface) {
          const CharacterState& ch = session_->character();
          view.position = ch.eye(session_->characterParams());
          view.rotation = ch.viewRotation();
          view.fovDeg = 75.0f;
        }
        params.viewRotation = view.viewRotation();
        params.fovYRadians = radians(view.fovDeg);
        params.planets.clear();
        const InteriorLighting& L = idef->lighting;
        params.sunDirection = L.keyDirection;
        params.sunColor = L.keyColor;
        params.sunGlare = 0.0f;
        params.ambientSky = L.ambientSky;
        params.ambientGround = L.ambientGround;
        params.fillDirection = L.fillDirection;
        params.fillColor = L.fillColor;
        params.rimColor = L.rimColor;
        params.rimPower = L.rimPower;
        params.exposure = L.exposure;
        params.drawSky = idef->showSky;
        params.drawStars = idef->showSky;
        if (!idef->showSky) params.skyBase = {0.004f, 0.004f, 0.006f};
        if (idef->surfaceSky) {
          params.atmosphereSky = true;
          params.atmoZenith = idef->surfaceSky->zenith;
          params.atmoHorizon = idef->surfaceSky->horizon;
          params.atmoGround = idef->surfaceSky->ground;
          params.atmoHaze = idef->surfaceSky->haze;
          params.starVisibility = idef->surfaceSky->starVisibility;
          params.sunGlare = 1.0f;
          params.sunAngularRadiusRadians = radians(0.8f);
        }
        items.clear();
        glows.clear();
        for (const auto& pi : interiorPieces_[sceneId]) items.push_back({pi.mesh, Mat4::translation(-view.position) * pi.model});
        if (idef->parkedShip) {
          const auto& ps = *idef->parkedShip;
          const Transform shipPose = surface ? ship
                                             : Transform{ps.position, Quat::fromAxisAngle({0, 1, 0}, radians(ps.yawDeg))};
          items.push_back({player.visual,
                           Mat4::translation(shipPose.position - view.position) * Mat4::rotation(shipPose.rotation) *
                               player.meshFix});
          if (surface) {  // engines burn while the ship moves
            const LandState ls = session_->landing().state();
            const float burn = ls == LandState::Landed ? 0.0f : 1.0f;
            for (const auto& sname : player.def.engineGlow.sockets)
              if (auto it = player.sockets.find(sname); it != player.sockets.end() && burn > 0)
                glows.push_back({Vec4(shipPose.position + shipPose.rotation.rotate(it->second.position) - view.position,
                                      player.def.engineGlow.thrustSize),
                                 Vec4(player.def.engineGlow.color * player.def.engineGlow.thrustIntensity, 0)});
          }
        }
        params.fogColor = L.fogColor;
        params.fogDensity = L.fogDensity;
        params.pointLights.clear();
        const double t = world().simulationTime();
        for (const auto& l : idef->lights) {
          const float k = l.intensityAt(t);
          if (k > 0) glows.push_back({Vec4(l.position - view.position, l.size), Vec4(l.color * k, 0)});
          if (l.range > 0 && k > 0) params.pointLights.push_back({l.position - view.position, l.range, l.color * (k * l.intensity)});
        }
        // Nearest lamps win the 8 shading slots.
        std::sort(params.pointLights.begin(), params.pointLights.end(), [](const PointLight& a, const PointLight& b) {
          return dot(a.position, a.position) < dot(b.position, b.position);
        });
      }
    }

    // Interior "key" lights are fake sky light, so only open scenes shadow them.
    const bool onFootOrSurface = !cockpit || surface;
    const InteriorDef* localScene = onFootOrSurface ? (surface ? surface : session_->activeInterior()) : nullptr;
    params.cameraPosition = view.position;
    params.sunShadows = settings_.shadows && (!localScene || localScene->surfaceSky.has_value());
    params.pointShadows = settings_.shadows && localScene != nullptr;
    params.ambientOcclusion = settings_.ambientOcclusion;
    params.reflections = settings_.reflections ? 1.0f : 0.0f;
    params.post.surfaceDetail = looks_.profiles[size_t(activeLook_)].surfaceDetail;

    buildOverlay(view, viewProj, ship);
    updateAudio(float(dt), engineDemandForAudio, speedFractionForAudio);

    const bool lastFrame = options_.exitAfterFrames > 0 && frameCount_ + 1 >= std::uint64_t(options_.exitAfterFrames);
    if (lastFrame && options_.screenshotOnExit) pendingScreenshot_ = options_.screenshotOnExit;
    const std::filesystem::path* shot = pendingScreenshot_ ? &*pendingScreenshot_ : nullptr;
    if (renderer_->render(params, items, glows, overlay_.vertices(), shot)) {
      pendingScreenshot_.reset();
      ++frameCount_;
    }

    const RenderStats& rs = renderer_->stats();
    FrameSample sample;
    sample.cpuMs = dt * 1000.0;
    sample.gpuMs = rs.gpuMs;
    sample.drawCalls = rs.drawCalls;
    sample.triangles = rs.triangles;
    if (frameCount_ > 2) stats_.push(sample);  // skip start-up frames

    if (lastFrame) quit_ = true;
    if (benchmark_ && runTime_ >= benchSeconds) quit_ = true;
    if (benchmark_ && benchmark_->autoDock && session_->docking().state() == DockState::Docked &&
        session_->docking().stateTime() > 2.0)
      quit_ = true;  // docking benchmark ends once clamped
  }

  if (benchmark_) {
    std::filesystem::path out = options_.benchmarkOut.empty() ? std::filesystem::path("benchmarks") / benchmark_->id
                                                              : options_.benchmarkOut;
    if (out.has_parent_path()) std::filesystem::create_directories(out.parent_path());
    auto csv = out;
    csv += ".csv";
    auto json = out;
    json += ".json";
    stats_.exportCsv(csv);
    stats_.exportSummaryJson(json, benchmark_->id, ctx_->deviceName());
    const FrameSummary s = stats_.recordingSummary();
    log::info("bench", "{}: {} frames  avg {:.2f} ms ({:.1f} fps)  p99 {:.2f} ms  max {:.2f} ms  gpu avg {:.2f} ms",
              benchmark_->id, s.frames, s.avgCpuMs, s.avgFps, s.p99CpuMs, s.maxCpuMs, s.avgGpuMs);
    log::info("bench", "wrote {} and {}", csv.string(), json.string());
  }
  log::info("app", "exiting after {} frames, {} rebases, {} impacts, {} validation messages", frameCount_, rebases_,
            world().player().collisions, VkContext::validationMessageCount());
  if (uiScript_.failures() > 0) {
    log::error("ui-script", "{} expectation(s) failed", uiScript_.failures());
    return 3;
  }
  return VkContext::validationMessageCount() > 0 && options_.validation ? 2 : 0;
}

}  // namespace fh
