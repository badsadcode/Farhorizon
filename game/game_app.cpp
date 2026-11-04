#include "game/game_app.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <map>

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
Quat eulerDegToQuat(const Vec3& deg) {
  // Applied X, then Y, then Z (see docs/data_schemas).
  return normalize(Quat::fromAxisAngle({0, 0, 1}, radians(deg.z)) * Quat::fromAxisAngle({0, 1, 0}, radians(deg.y)) *
                   Quat::fromAxisAngle({1, 0, 0}, radians(deg.x)));
}

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
                      const Vec3& rotDeg) -> std::optional<LoadedMesh> {
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
    applyMaterialOverrides(*mesh, overrides, owner, &errors);
    LoadedMesh lm;
    lm.meshFix = Mat4::rotation(eulerDegToQuat(rotDeg)) * Mat4::scale({scale, scale, scale}) *
                 Mat4::translation(-mesh->boundsCenter());
    lm.data = std::move(*mesh);
    return lm;
  };

  auto shipDef = loadShipDef(*manifest, scene->player.ship, &errors);
  if (!shipDef) return false;
  auto shipMesh = loadMesh(shipDef->mesh, shipDef->materialOverrides, "ship '" + shipDef->id + "'",
                           shipDef->meshScale, shipDef->meshRotationDeg);
  if (!shipMesh) return false;

  std::vector<LoadedMesh> propMeshes;
  for (const auto& p : scene->props) {
    auto m = loadMesh(p.mesh, p.materialOverrides, "prop '" + p.id + "'", p.scale, {0, 0, 0});
    if (!m) return false;
    propMeshes.push_back(std::move(*m));
  }

  struct FieldData {
    std::vector<MeshData> variants;
    std::vector<AsteroidInstance> instances;
  };
  std::vector<FieldData> fieldData;
  for (const auto& f : scene->asteroidFields) {
    FieldData fd;
    for (std::uint32_t v = 0; v < f.variants; ++v) {
      AsteroidMeshParams mp;
      mp.seed = (std::uint64_t(f.seed) << 8) + v;
      mp.subdivisions = f.detail;
      mp.roughness = f.roughness;
      mp.color = f.color;
      fd.variants.push_back(generateAsteroidMesh(mp));
    }
    AsteroidFieldParams ap;
    ap.seed = f.seed;
    ap.count = f.count;
    ap.radius = f.radius;
    ap.thickness = f.thickness;
    ap.clusters = f.clusters;
    ap.minScale = f.minScale;
    ap.maxScale = f.maxScale;
    ap.variants = f.variants;
    fd.instances = generateAsteroidField(ap);
    fieldData.push_back(std::move(fd));
  }
  for (auto& w : errors) log::warn("content", "{}", w);  // only non-fatal warnings remain here

  // ---- Commit: preserve the player's pose across hot reloads ----
  std::optional<SystemPosition> keepPos;
  std::optional<FlightState> keepState;
  if (world_ && !bench) {
    keepPos = world_->frame().toSystem(world_->player().state.position);
    keepState = world_->player().state;
  }

  renderer_->clearMeshes();
  auto world = std::make_unique<SpaceWorld>();
  const SystemPosition start = keepPos ? *keepPos : scene->player.position;
  world->setOrigin(start, scene->rebaseDistance);

  PlayerShip& player = world->player();
  player.def = *shipDef;
  player.visual = renderer_->uploadMesh(shipMesh->data);
  player.meshFix = shipMesh->meshFix;
  player.collisionRadius =
      shipDef->collisionRadius > 0 ? shipDef->collisionRadius : shipMesh->data.boundsRadius() * shipDef->meshScale;
  // Sockets: mesh nodes win; ship JSON provides fallbacks (both end up in ship space).
  const Quat fixRot = quatFromMatrix(shipMesh->meshFix);
  for (const auto& s : shipMesh->data.sockets)
    player.sockets[s.name] = {shipMesh->meshFix.transformPoint(s.position), normalize(fixRot * s.rotation)};
  for (const auto& [name, s] : shipDef->sockets)
    if (!player.sockets.count(name)) player.sockets[name] = {s.position, eulerDegToQuat(s.rotationDeg)};
  if (keepState) {
    player.state = *keepState;
    player.state.position = {};
  } else {
    player.state = FlightState{};
    player.state.orientation = eulerDegToQuat(scene->player.rotationDeg);
    player.state.boostEnergy = shipDef->flight.boostEnergyCapacity;
  }
  player.previous = player.state;

  for (size_t i = 0; i < scene->props.size(); ++i) {
    const PropDef& pd = scene->props[i];
    Prop p;
    p.def = pd;
    p.system = pd.position;
    p.rotation = eulerDegToQuat(pd.rotationDeg);
    p.visual = renderer_->uploadMesh(propMeshes[i].data);
    p.meshFix = propMeshes[i].meshFix;
    p.collisionRadius = pd.collisionRadius > 0 ? pd.collisionRadius : propMeshes[i].data.boundsRadius() * pd.scale;
    world->addProp(std::move(p));
  }
  std::size_t asteroidCount = 0;
  for (size_t fi = 0; fi < scene->asteroidFields.size(); ++fi) {
    AsteroidFieldRuntime rt;
    rt.def = scene->asteroidFields[fi];
    for (const auto& v : fieldData[fi].variants) rt.variants.push_back(renderer_->uploadMesh(v));
    const std::uint32_t fieldIndex = world->addAsteroidField(rt);
    const auto& variants = world->fields()[fieldIndex].variants;
    for (std::uint32_t ai = 0; ai < fieldData[fi].instances.size(); ++ai) {
      const AsteroidInstance& inst = fieldData[fi].instances[ai];
      Asteroid a;
      a.system = {rt.def.center.metres + DVec3(inst.offset)};
      a.rotation = inst.rotation;
      a.spinAxis = inst.spinAxis;
      a.spinRate = inst.spinRate;
      a.radius = inst.radius;
      a.visual = variants[inst.variant];
      a.field = fieldIndex;
      a.index = ai;
      world->addAsteroid(a);
      ++asteroidCount;
    }
  }

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
  world_ = std::move(world);
  if (firstLoad) {
    camera_.configureDev(scene_.camera.fovDeg, scene_.camera.orbitDistance, scene_.camera.yawDeg,
                         scene_.camera.pitchDeg, scene_.camera.autoRotateDegPerSec);
    std::string mode = scene_.camera.initialMode;
    if (benchmark_) mode = benchmark_->camera;
    if (options_.cameraOverride) mode = *options_.cameraOverride;
    camera_.setMode(cameraModeFromString(mode), world_->playerRenderTransform(1.0f));
    if (benchmark_) controller_.setAutofly(true, benchmark_->throttle, benchmark_->rotation);
    else if (options_.autofly) controller_.setAutofly(true, 0.4f, {0.0f, 0.08f, 0.0f});
  }
  camera_.snap();
  log::info("content", "scene '{}' loaded: {} props, {} asteroids in {} fields, {} stars", scene_.id,
            world_->props().size(), asteroidCount, world_->fields().size(), sp.count);
  return true;
}

void GameApp::handleAppInput() {
  const Transform ship = world_->playerRenderTransform(1.0f);
  if (input_.pressed("app.quit")) quit_ = true;
  if (input_.pressed("app.toggleMouse")) setMouseCapture(!mouseCaptured_);
  if (input_.pressed("debug.overlay")) showDevOverlay_ = !showDevOverlay_;
  if (input_.pressed("window.fullscreen")) window_->toggleFullscreen();
  if (input_.pressed("debug.screenshot"))
    pendingScreenshot_ = std::filesystem::path("screenshots") / std::format("farhorizon_{:03}.png", screenshotCounter_++);
  if (input_.pressed("camera.cycle")) camera_.cycleShipCamera(ship);
  if (input_.pressed("camera.dev")) camera_.toggleDev(ship);
  if (input_.pressed("camera.autoRotate") && camera_.mode() == CameraMode::Orbit) camera_.toggleAutoRotate();
  if (input_.pressed("target.next")) world_->cycleTarget();
  if (input_.pressed("target.nearestAsteroid")) world_->targetNearestAsteroid();
  if (input_.pressed("target.clear")) world_->clearTarget();
  if (input_.pressed("visual.cycleLook") && !looks_.profiles.empty()) {
    activeLook_ = (activeLook_ + 1) % int(looks_.profiles.size());
    lookShownUntil_ = runTime_ + 2.0;
    log::info("render", "look: {}", looks_.profiles[size_t(activeLook_)].name);
  }
  if (input_.pressed("debug.autofly")) controller_.setAutofly(!controller_.autofly(), 0.4f, {0.0f, 0.08f, 0.0f});
  if (input_.pressed("debug.reloadAssets")) {
    std::vector<std::string> errors;
    if (loadContent(errors)) {
      log::info("content", "hot reload OK");
      contentErrors_ = errors;  // may contain warnings
    } else {
      log::error("content", "hot reload failed; keeping previous content");
      contentErrors_ = errors;
      contentErrors_.insert(contentErrors_.begin(), "RELOAD FAILED - previous content kept:");
    }
  }
}

void GameApp::buildDevOverlay() {
  const FrameSummary s = stats_.windowSummary();
  const RenderStats& rs = renderer_->stats();
  const PlayerShip& p = world_->player();
  const SystemPosition sys = world_->frame().toSystem(p.state.position);
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
                               rs.meshInstances, visibleAsteroids_, world_->asteroids().size()),
                   normal});
  lines.push_back({std::format("GPU mem {:.1f} MB   origin gen {} ({} this run)  local {:.0f} m",
                               double(gpu::allocatedBytes()) / (1024.0 * 1024.0), world_->frame().generation(), rebases_,
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
  HudContext hc{*world_, controller_, world_->player().input, ship, view, camera_.mode(), viewProj,
                float(ext.width), float(ext.height), mouseCaptured_, runTime_};
  const VisualProfile& look = looks_.profiles[size_t(activeLook_)];
  hc.palette = look.hud;
  hc.lookName = look.name;
  hc.lookShownUntil = lookShownUntil_;
  drawHud(overlay_, hc);
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
    const bool devCam = camera_.isDev();
    const bool freeCam = camera_.mode() == CameraMode::Free;
    controller_.update(input_, float(dt), mouseCaptured_ && !devCam, world_->player().state.boostEnergy,
                       world_->player().input);
    if (freeCam) {  // WASD drives the free camera; the ship holds its set-speed and stops turning
      FlightInput hold;
      hold.throttle = controller_.throttle();
      hold.assist = controller_.assist();
      hold.mode = controller_.mode();
      world_->player().input = hold;
    }

    const int steps = clock_.advance(dt);
    for (int i = 0; i < steps; ++i) {
      world_->fixedUpdate(float(clock_.step()));
      camera_.fixedUpdate(float(clock_.step()));
      if (auto delta = world_->rebaseIfNeeded()) {
        camera_.applyRebase(*delta);
        ++rebases_;
      }
    }

    // ---- Camera and render data (camera-relative) ----
    const Transform ship = world_->playerRenderTransform(float(clock_.alpha()));
    const CameraView view = camera_.update(ship, world_->player(), input_, float(dt), devCam || !mouseCaptured_);
    const VkExtent2D ext = renderer_->extent();
    RenderFrameParams params;
    params.viewRotation = view.viewRotation();
    params.fovYRadians = radians(view.fovDeg);
    params.nearPlane = 0.05f;
    const SystemPosition camSys = world_->frame().toSystem(view.position);
    params.sunDirection = Vec3(normalize(scene_.star.position.metres - camSys.metres));
    params.sunColor = scene_.star.color * scene_.star.intensity;
    params.sunAngularRadiusRadians = radians(scene_.star.angularDiameterDeg * 0.5f);
    params.ambientSky = scene_.ambient.skyColor;
    params.ambientGround = scene_.ambient.groundColor;
    params.exposure = scene_.exposure;
    params.timeSeconds = float(world_->simulationTime());
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
    items.reserve(world_->asteroids().size() + world_->props().size() + 1);
    const PlayerShip& player = world_->player();
    items.push_back({player.visual, Mat4::translation(ship.position - view.position) * Mat4::rotation(ship.rotation) *
                                        player.meshFix});
    for (const auto& p : world_->props()) {
      const Vec3 rel = p.local - view.position;
      if (!frustum.sphereVisible(rel, p.collisionRadius * 1.5f)) continue;
      items.push_back({p.visual, Mat4::translation(rel) * Mat4::rotation(p.rotation) * p.meshFix});
    }
    visibleAsteroids_ = 0;
    for (const auto& a : world_->asteroids()) {
      const Vec3 rel = a.local - view.position;
      if (!frustum.sphereVisible(rel, a.radius)) continue;
      ++visibleAsteroids_;
      items.push_back({a.visual, Mat4::translation(rel) * Mat4::rotation(a.rotation) *
                                     Mat4::scale({a.radius, a.radius, a.radius})});
    }

    // ---- Glow sprites: engine exhaust + running lights ----
    std::vector<GlowSprite> glows;
    {
      const double t = world_->simulationTime();
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
      for (const auto& p : world_->props())
        for (const auto& l : p.def.lights) {
          const float k = l.intensityAt(t);
          if (k <= 0) continue;
          const Vec3 wp = p.local + p.rotation.rotate(l.position) - view.position;
          if (!frustum.sphereVisible(wp, l.size)) continue;
          glows.push_back({Vec4(wp, l.size), Vec4(l.color * k, 0)});
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
            world_->player().collisions, VkContext::validationMessageCount());
  return VkContext::validationMessageCount() > 0 && options_.validation ? 2 : 0;
}

}  // namespace fh
