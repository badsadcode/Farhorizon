#include "game/game_app.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <map>

#include "game/content/interior_def.h"
#include "game/session/world_builder.h"

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
  showDevOverlay_ = options_.devOverlay;
  window_ = std::make_unique<Window>("FARHORIZON", options_.width, options_.height);
  ctx_ = std::make_unique<VkContext>(*window_, VkContextOptions{options_.validation, options_.vsync});
  renderer_ = std::make_unique<Renderer>(*ctx_, options_.shaderDir);
  if (!loadContent(contentErrors_))
    throw std::runtime_error("initial content load failed:\n  " +
                             (contentErrors_.empty() ? std::string("unknown error") : contentErrors_.front()));
  if (benchmark_ && !benchmark_->startIn.empty()) options_.startIn = benchmark_->startIn;
  if (options_.startDocked || !options_.startIn.empty()) {
    session_->docking().forceDock(world());
    lastDockState_ = DockState::Docked;
    if (!options_.startIn.empty()) {
      session_->leaveSeat();
      if (options_.startIn == "station") session_->perform("exitShip");
    }
  }
  const bool scripted = !options_.benchmarkId.empty() || options_.exitAfterFrames > 0;
  if (!scripted) setMouseCapture(true);
}

GameApp::~GameApp() {
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
  for (auto& w : errors) log::warn("content", "{}", w);  // only non-fatal warnings remain here

  // ---- Commit: preserve the player's pose (and docking) across hot reloads ----
  std::optional<SystemPosition> keepPos;
  std::optional<FlightState> keepState;
  std::optional<Docking> keepDocking;
  if (session_ && !bench) {
    keepPos = world().frame().toSystem(world().player().state.position);
    keepState = world().player().state;
    keepDocking = session_->docking();
  }

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
  if (keepState) {
    newSession->world().player().state = *keepState;
    newSession->world().player().state.position = {};
    newSession->world().player().previous = newSession->world().player().state;
  }
  if (keepDocking) newSession->docking() = *keepDocking;
  newSession->setShipInterior(shipDef->interior);
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
    newSession->addInterior(iid, std::move(def));
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
  benchmark_ = bench;
  scene_ = std::move(*scene);
  session_ = std::move(newSession);
  lastDockState_ = session_->docking().state();
  if (firstLoad) {
    camera_.configureDev(scene_.camera.fovDeg, scene_.camera.orbitDistance, scene_.camera.yawDeg,
                         scene_.camera.pitchDeg, scene_.camera.autoRotateDegPerSec);
    std::string mode = scene_.camera.initialMode;
    if (benchmark_) mode = benchmark_->camera;
    if (options_.cameraOverride) mode = *options_.cameraOverride;
    camera_.setMode(cameraModeFromString(mode), world().playerRenderTransform(1.0f));
    if (benchmark_ && !benchmark_->autoDock && benchmark_->startIn.empty())
      controller_.setAutofly(true, benchmark_->throttle, benchmark_->rotation);
    else if (options_.autofly) controller_.setAutofly(true, 0.4f, {0.0f, 0.08f, 0.0f});
  }
  camera_.snap();
  log::info("content", "scene '{}' loaded: {} props, {} asteroids in {} fields, {} interiors, {} stars", scene_.id,
            world().props().size(), asteroidCount, world().fields().size(), interiorPieces_.size(), sp.count);
  return true;
}

void GameApp::handleAppInput() {
  const Transform ship = world().playerRenderTransform(1.0f);
  const bool cockpit = session_->location() == Location::Cockpit;
  if (input_.pressed("app.quit")) quit_ = true;
  if (input_.pressed("app.toggleMouse")) setMouseCapture(!mouseCaptured_);
  if (input_.pressed("debug.overlay")) showDevOverlay_ = !showDevOverlay_;
  if (input_.pressed("window.fullscreen")) window_->toggleFullscreen();
  if (input_.pressed("debug.screenshot"))
    pendingScreenshot_ = std::filesystem::path("screenshots") / std::format("farhorizon_{:03}.png", screenshotCounter_++);
  if (input_.pressed("visual.cycleLook") && !looks_.profiles.empty()) {
    activeLook_ = (activeLook_ + 1) % int(looks_.profiles.size());
    lookShownUntil_ = runTime_ + 2.0;
    log::info("render", "look: {}", looks_.profiles[size_t(activeLook_)].name);
  }
  if (input_.pressed("interact")) session_->interact();
  if (cockpit) {
    if (input_.pressed("camera.cycle")) camera_.cycleShipCamera(ship);
    if (input_.pressed("camera.dev")) camera_.toggleDev(ship);
    if (input_.pressed("camera.autoRotate") && camera_.mode() == CameraMode::Orbit) camera_.toggleAutoRotate();
    if (input_.pressed("target.next")) world().cycleTarget();
    if (input_.pressed("target.nearestAsteroid")) world().targetNearestAsteroid();
    if (input_.pressed("target.clear")) world().clearTarget();
    if (input_.pressed("debug.autofly")) controller_.setAutofly(!controller_.autofly(), 0.4f, {0.0f, 0.08f, 0.0f});
    if (input_.pressed("dock.request")) {
      Docking& d = session_->docking();
      switch (d.state()) {
        case DockState::Undocked: d.request(world()); break;
        case DockState::RequestingClearance:
        case DockState::ApproachAuthorized: d.cancel(); break;
        case DockState::Docked: d.undock(world()); break;
        default: break;
      }
    }
    if (input_.pressed("dock.autopilot")) session_->docking().toggleAutopilot();
    if (input_.pressed("debug.forceDock")) session_->docking().forceDock(world());
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
  lines.push_back({std::format("FARHORIZON 0.2  {}  {}x{}  vsync {}  val {}", ctx_->deviceName(), ext.width, ext.height,
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
  lines.push_back({"Z assist  B precision  C recenter  T target  G nearest rock  V camera  F2 dev cam", dim});
  lines.push_back({"Esc mouse  F1 info  F3 autofly  F4 look  F5 reload  F11 fullscreen  F12 shot  F10 quit", dim});

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
  if (session_->location() == Location::Cockpit) {
    drawHud(overlay_, hc);
  } else {
    InteriorHudContext ic{*session_, float(ext.width), float(ext.height), mouseCaptured_, runTime_, look.hud};
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

    if (!window_->pollEvents(input_)) break;
    if (window_->isMinimized()) continue;  // keep pending resize until restored (no 0x0 swapchain)
    if (window_->consumeResized()) ctx_->recreateSwapchain();
    if (mouseCaptured_ && !window_->hasFocus()) setMouseCapture(false);

    handleAppInput();
    const bool cockpit = session_->location() == Location::Cockpit;
    const bool devCam = camera_.isDev();
    const bool freeCam = camera_.mode() == CameraMode::Free;
    if (cockpit) {
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

    const int steps = clock_.advance(dt);
    for (int i = 0; i < steps; ++i) {
      session_->fixedUpdate(float(clock_.step()));
      const DockState ds = session_->docking().state();
      if (ds != lastDockState_ && ds == DockState::Docked) controller_.setThrottle(0.0f);  // no surprise launch
      if (benchmark_ && benchmark_->autoDock) {
        Docking& d = session_->docking();
        if (ds == DockState::Undocked && d.message().empty()) d.request(world());
        if (ds == DockState::ApproachAuthorized && !d.autopilot()) d.toggleAutopilot();
      }
      lastDockState_ = ds;
      camera_.fixedUpdate(float(clock_.step()));
      if (auto delta = world().rebaseIfNeeded()) {
        camera_.applyRebase(*delta);
        ++rebases_;
      }
    }

    // ---- Camera and render data (camera-relative) ----
    const Transform ship = world().playerRenderTransform(float(clock_.alpha()));
    CameraView view = camera_.update(ship, world().player(), input_, float(dt), devCam || !mouseCaptured_);
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
          if (!frustum.sphereVisible(wp, l.size)) continue;
          glows.push_back({Vec4(wp, l.size), Vec4(l.color * k, 0)});
        }
    }

    // ---- On foot: render the active interior instead of space ----
    if (!cockpit) {
      if (const InteriorDef* idef = session_->activeInterior()) {
        const CharacterState& ch = session_->character();
        view.position = ch.eye(session_->characterParams());
        view.rotation = ch.viewRotation();
        view.fovDeg = 75.0f;
        params.viewRotation = view.viewRotation();
        params.fovYRadians = radians(view.fovDeg);
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
        items.clear();
        glows.clear();
        for (const auto& pi : interiorPieces_[session_->activeInteriorId()])
          items.push_back({pi.mesh, Mat4::translation(-view.position) * pi.model});
        if (idef->parkedShip) {
          const auto& ps = *idef->parkedShip;
          items.push_back({player.visual, Mat4::translation(ps.position - view.position) *
                                              Mat4::rotation(Quat::fromAxisAngle({0, 1, 0}, radians(ps.yawDeg))) *
                                              player.meshFix});
        }
        const double t = world().simulationTime();
        for (const auto& l : idef->lights) {
          const float k = l.intensityAt(t);
          if (k > 0) glows.push_back({Vec4(l.position - view.position, l.size), Vec4(l.color * k, 0)});
        }
      }
    }

    buildOverlay(view, viewProj, ship);

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
  return VkContext::validationMessageCount() > 0 && options_.validation ? 2 : 0;
}

}  // namespace fh
