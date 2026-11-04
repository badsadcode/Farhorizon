#include "game/showcase_app.h"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <format>

#include "engine/assets/gltf_loader.h"
#include "engine/core/log.h"
#include "engine/platform/window.h"
#include "engine/render/gpu_resources.h"
#include "engine/render/renderer.h"
#include "engine/render/starfield.h"
#include "engine/render/vk_context.h"

namespace fh {

namespace {
Quat eulerDegToQuat(const Vec3& deg) {
  // Applied in X, then Y, then Z order (intrinsic), matching docs/data_schemas.
  return normalize(Quat::fromAxisAngle({0, 0, 1}, radians(deg.z)) * Quat::fromAxisAngle({0, 1, 0}, radians(deg.y)) *
                   Quat::fromAxisAngle({1, 0, 0}, radians(deg.x)));
}

void applyMaterialOverrides(MeshData& mesh, const ShipDef& def, std::vector<std::string>& warnings) {
  for (const auto& [name, o] : def.materialOverrides) {
    bool found = false;
    for (auto& m : mesh.materials) {
      if (m.name != name) continue;
      found = true;
      if (o.baseColor) m.baseColorFactor = *o.baseColor;
      if (o.metallic) m.metallic = *o.metallic;
      if (o.roughness) m.roughness = *o.roughness;
      if (o.emissive) m.emissive = *o.emissive;
    }
    if (!found) warnings.push_back("ship '" + def.id + "': materialOverride '" + name + "' matches no material");
  }
}

std::string formatDistance(double metres) {
  const double au = 149597870700.0;
  if (std::fabs(metres) >= 0.01 * au) return std::format("{:.4f} AU", metres / au);
  if (std::fabs(metres) >= 10000.0) return std::format("{:.1f} km", metres / 1000.0);
  return std::format("{:.1f} m", metres);
}
}  // namespace

ShowcaseApp::ShowcaseApp(AppOptions options) : options_(std::move(options)) {
  showOverlay_ = options_.overlay;
  window_ = std::make_unique<Window>("FARHORIZON - Stage 1", options_.width, options_.height);
  ctx_ = std::make_unique<VkContext>(*window_, VkContextOptions{options_.validation, options_.vsync});
  renderer_ = std::make_unique<Renderer>(*ctx_, options_.shaderDir);
  if (!loadContent(contentErrors_))
    throw std::runtime_error("initial content load failed:\n  " +
                             (contentErrors_.empty() ? std::string("unknown error") : contentErrors_.front()));
}

ShowcaseApp::~ShowcaseApp() {
  renderer_.reset();
  ctx_.reset();
  window_.reset();
}

bool ShowcaseApp::loadContent(std::vector<std::string>& errors) {
  errors.clear();
  auto manifest = AssetManifest::load(options_.contentDir, &errors);
  if (!manifest) return false;
  for (auto& p : manifest->validateFiles()) errors.push_back(p);
  if (!errors.empty()) return false;

  if (auto inputPath = manifest->resolve("config.input", "config"))
    input_.loadBindings(*inputPath, &errors);
  else
    errors.push_back("config.input missing from manifest");

  auto scene = loadSceneDef(*manifest, options_.sceneId, &errors);
  if (!scene) return false;

  // Load every referenced ship and mesh on the CPU first; only touch the GPU once all succeed.
  struct Pending {
    MeshData mesh;
    ShipDef def;
    SceneObjectDef placement;
  };
  std::vector<Pending> pending;
  for (const auto& obj : scene->objects) {
    auto def = loadShipDef(*manifest, obj.ship, &errors);
    if (!def) return false;
    auto meshPath = manifest->resolve(def->mesh, "mesh");
    if (!meshPath) {
      errors.push_back("ship '" + def->id + "': mesh '" + def->mesh + "' is not a mesh asset");
      return false;
    }
    std::string err;
    auto mesh = loadGltfMesh(*meshPath, &err);
    if (!mesh) {
      errors.push_back("mesh '" + def->mesh + "': " + err);
      return false;
    }
    applyMaterialOverrides(*mesh, *def, errors);
    pending.push_back({std::move(*mesh), std::move(*def), obj});
  }
  for (auto& w : errors) log::warn("content", "{}", w);  // non-fatal warnings so far

  // Commit.
  renderer_->clearMeshes();
  objects_.clear();
  meshTriangles_ = 0;
  const SystemPosition origin = scene->objects.empty() ? SystemPosition{} : scene->objects.front().position;
  frame_ = LocalFrame(origin, scene->rebaseDistance);
  for (auto& p : pending) {
    ActiveObject o;
    o.mesh = renderer_->uploadMesh(p.mesh);
    o.localPosition = frame_.toLocal(p.placement.position);
    o.rotation = eulerDegToQuat(p.placement.rotationDeg);
    const float s = p.def.meshScale;
    o.meshFix = Mat4::rotation(eulerDegToQuat(p.def.meshRotationDeg)) * Mat4::scale({s, s, s}) *
                Mat4::translation(-p.mesh.boundsCenter());
    meshTriangles_ += p.mesh.triangleCount();
    objects_.push_back(o);
  }

  StarfieldParams sp;
  sp.seed = scene->starfield.seed;
  sp.count = scene->starfield.count;
  sp.brightness = scene->starfield.brightness;
  sp.galacticPlaneTiltRadians = radians(scene->starfield.galacticPlaneTiltDeg);
  renderer_->setStarfield(generateStarfield(sp));

  const bool firstLoad = !manifest_.has_value();
  manifest_ = std::move(manifest);
  scene_ = std::move(*scene);
  if (firstLoad) {
    camera_.setOrbit(objects_.empty() ? Vec3{} : objects_.front().localPosition, scene_.camera.orbitDistance,
                     scene_.camera.yawDeg, scene_.camera.pitchDeg);
    camera_.setAutoRotate(scene_.camera.autoRotateDegPerSec);
  } else if (!objects_.empty()) {
    camera_.setOrbitTarget(objects_.front().localPosition);
  }
  log::info("content", "scene '{}' loaded: {} objects, {} stars, origin at {:.1f} km from star", scene_.id,
            objects_.size(), sp.count, length(origin.metres - scene_.star.position.metres) / 1000.0);
  return true;
}

void ShowcaseApp::requestScreenshot() {
  pendingScreenshot_ = std::filesystem::path("screenshots") / std::format("farhorizon_{:03}.png", screenshotCounter_++);
}

void ShowcaseApp::handleFrameInput(float dt) {
  if (input_.pressed("camera.toggleMode")) camera_.toggleMode();
  if (input_.pressed("camera.autoRotate")) camera_.toggleAutoRotate();
  if (input_.pressed("debug.overlay")) showOverlay_ = !showOverlay_;
  if (input_.pressed("window.fullscreen")) window_->toggleFullscreen();
  if (input_.pressed("debug.screenshot")) requestScreenshot();
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
  camera_.handleInput(input_, dt);
}

void ShowcaseApp::rebaseIfNeeded() {
  const Vec3 camPos = camera_.position();
  if (!frame_.needsRebase(camPos)) return;
  const Vec3 delta = frame_.rebase(frame_.toSystem(camPos));
  camera_.applyRebase(delta);
  for (auto& o : objects_) o.localPosition += delta;
  log::info("world", "floating origin rebased (generation {})", frame_.generation());
}

void ShowcaseApp::buildOverlay() {
  overlay_.clear();
  if (!showOverlay_) return;
  const FrameSummary s = stats_.windowSummary();
  const RenderStats& rs = renderer_->stats();
  const SystemPosition camSys = frame_.toSystem(camera_.position());
  const double starDist = length(camSys.metres - scene_.star.position.metres);
  const VkExtent2D ext = renderer_->extent();

  std::vector<std::pair<std::string, OverlayColor>> lines;
  const OverlayColor title{255, 196, 120, 255}, normal{215, 225, 235, 255}, dim{140, 150, 165, 255},
      bad{255, 110, 100, 255};
  lines.push_back({"FARHORIZON  stage 1 engine spike", title});
  lines.push_back({std::format("GPU  {}", ctx_->deviceName()), dim});
  lines.push_back({std::format("{}x{}  vsync {}  validation {}", ext.width, ext.height, ctx_->vsync() ? "on" : "off",
                               ctx_->validationEnabled() ? "on" : "off"),
                   dim});
  lines.push_back({std::format("FPS {:6.1f}   CPU {:5.2f} ms (max {:5.2f})", s.avgFps, s.avgCpuMs, s.maxCpuMs), normal});
  lines.push_back({rs.gpuMs >= 0 ? std::format("GPU frame {:5.2f} ms", rs.gpuMs) : std::string("GPU frame  n/a"), normal});
  lines.push_back({std::format("draws {}  tris {}  stars {}", rs.drawCalls, rs.triangles, rs.stars), normal});
  lines.push_back({std::format("GPU mem (engine allocs) {:.1f} MB", double(gpu::allocatedBytes()) / (1024.0 * 1024.0)),
                   normal});
  lines.push_back({std::format("camera {}  {}", camera_.mode() == DevCamera::Mode::Orbit ? "ORBIT" : "FREE",
                               camera_.mode() == DevCamera::Mode::Free ? std::format("{:.0f} m/s", camera_.freeSpeed())
                                                                       : std::string(camera_.autoRotate() ? "auto" : "")),
                   normal});
  lines.push_back({std::format("star distance {}", formatDistance(starDist)), normal});
  lines.push_back({std::format("origin gen {}  cam local {:.0f} m", frame_.generation(), length(camera_.position())), dim});
  const int vkMsgs = VkContext::validationMessageCount();
  lines.push_back({std::format("validation msgs {}  log warn {} err {}", vkMsgs, log::warningCount(), log::errorCount()),
                   vkMsgs > 0 ? bad : dim});
  for (const auto& e : contentErrors_) lines.push_back({e, bad});
  lines.push_back({"TAB orbit/free  LMB/RMB look  WASDQE move  wheel zoom/speed", dim});
  lines.push_back({"R auto-rotate  F1 overlay  F5 reload  F11 fullscreen  F12 shot", dim});

  const float scale = ext.width >= 2400 ? 3.0f : 2.0f;
  const float lineH = 11.0f * scale;
  float width = 0;
  for (auto& [t, c] : lines) width = std::max(width, OverlayBuilder::textWidth(t, scale));
  overlay_.rect(10, 10, width + 20, lineH * float(lines.size()) + 16, {8, 10, 16, 170});
  float y = 18;
  for (auto& [t, c] : lines) {
    overlay_.text(20, y, t, c, scale);
    y += lineH;
  }
}

int ShowcaseApp::run() {
  using Clock = std::chrono::steady_clock;
  auto last = Clock::now();
  const auto start = last;
  const bool benchmarking = options_.benchmarkSeconds > 0;
  if (benchmarking) {
    stats_.setRecording(true);
    camera_.setAutoRotate(10.0f);
    log::info("bench", "benchmark '{}' for {:.1f} s", options_.benchmarkOut.string(), options_.benchmarkSeconds);
  }

  bool running = true;
  while (running) {
    const auto now = Clock::now();
    const double dt = std::chrono::duration<double>(now - last).count();
    last = now;
    runTime_ = std::chrono::duration<double>(now - start).count();

    running = window_->pollEvents(input_);
    if (input_.pressed("app.quit")) running = false;
    if (window_->isMinimized()) continue;  // keep any pending resize until restored (no 0x0 swapchain)
    if (window_->consumeResized()) ctx_->recreateSwapchain();

    handleFrameInput(float(dt));
    const int steps = clock_.advance(dt);
    for (int i = 0; i < steps; ++i) camera_.fixedUpdate(float(clock_.step()));
    rebaseIfNeeded();

    // ---- Build render data (camera-relative) ----
    const Vec3 camPos = camera_.position();
    RenderFrameParams params;
    params.viewRotation = camera_.viewRotation();
    params.fovYRadians = radians(scene_.camera.fovDeg);
    params.nearPlane = 0.05f;
    const SystemPosition camSys = frame_.toSystem(camPos);
    params.sunDirection = Vec3(normalize(scene_.star.position.metres - camSys.metres));
    params.sunColor = scene_.star.color * scene_.star.intensity;
    params.sunAngularRadiusRadians = radians(scene_.star.angularDiameterDeg * 0.5f);
    params.ambientSky = scene_.ambient.skyColor;
    params.ambientGround = scene_.ambient.groundColor;
    params.exposure = scene_.exposure;
    params.timeSeconds = float(clock_.simulationTime());
    params.galacticTiltRadians = radians(scene_.starfield.galacticPlaneTiltDeg);

    std::vector<MeshDrawItem> items;
    items.reserve(objects_.size());
    for (const auto& o : objects_)
      items.push_back({o.mesh, Mat4::translation(o.localPosition - camPos) * Mat4::rotation(o.rotation) * o.meshFix});

    buildOverlay();

    const bool lastFrame = options_.exitAfterFrames > 0 && frameCount_ + 1 >= std::uint64_t(options_.exitAfterFrames);
    if (lastFrame && options_.screenshotOnExit) pendingScreenshot_ = options_.screenshotOnExit;
    const std::filesystem::path* shot = pendingScreenshot_ ? &*pendingScreenshot_ : nullptr;
    if (renderer_->render(params, items, overlay_.vertices(), shot)) {
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

    if (lastFrame) running = false;
    if (benchmarking && runTime_ >= options_.benchmarkSeconds) running = false;
  }

  if (benchmarking) {
    std::filesystem::create_directories(options_.benchmarkOut.parent_path().empty() ? "." : options_.benchmarkOut.parent_path());
    auto csv = options_.benchmarkOut;
    csv += ".csv";
    auto json = options_.benchmarkOut;
    json += ".json";
    stats_.exportCsv(csv);
    stats_.exportSummaryJson(json, "stage1_cockpit_empty_space", ctx_->deviceName());
    const FrameSummary s = stats_.recordingSummary();
    log::info("bench", "{} frames  avg {:.2f} ms ({:.1f} fps)  p99 {:.2f} ms  max {:.2f} ms  gpu avg {:.2f} ms",
              s.frames, s.avgCpuMs, s.avgFps, s.p99CpuMs, s.maxCpuMs, s.avgGpuMs);
    log::info("bench", "wrote {} and {}", csv.string(), json.string());
  }
  log::info("app", "exiting after {} frames, {} validation messages", frameCount_, VkContext::validationMessageCount());
  return VkContext::validationMessageCount() > 0 && options_.validation ? 2 : 0;
}

}  // namespace fh
