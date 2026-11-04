#pragma once
// Stage 1 showcase: loads a data-defined scene, renders the star field, the star and the
// imported ship, and lets the developer orbit or free-fly the camera.
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/assets/asset_manifest.h"
#include "engine/core/clock.h"
#include "engine/debug/frame_stats.h"
#include "engine/debug/overlay.h"
#include "engine/math/frames.h"
#include "engine/platform/input.h"
#include "game/camera.h"
#include "game/content/scene_def.h"
#include "game/content/ship_def.h"

namespace fh {

class Window;
class VkContext;
class Renderer;
using MeshHandle = std::uint32_t;

struct AppOptions {
  std::filesystem::path contentDir;
  std::filesystem::path shaderDir;
  std::string sceneId = "scene.stage1_showcase";
  int width = 1600;
  int height = 900;
  bool validation = false;
  bool vsync = true;
  bool overlay = true;
  int exitAfterFrames = 0;                       // 0 = run until closed
  std::optional<std::filesystem::path> screenshotOnExit;
  double benchmarkSeconds = 0;                   // > 0: record frame stats, export, exit
  std::filesystem::path benchmarkOut = "benchmarks/stage1_cockpit_empty_space";
};

class ShowcaseApp {
 public:
  explicit ShowcaseApp(AppOptions options);
  ~ShowcaseApp();
  int run();

 private:
  struct ActiveObject {
    MeshHandle mesh = 0;
    Vec3 localPosition;      // relative to the floating origin
    Quat rotation;
    Mat4 meshFix;            // data-driven correction: rotation * scale * recentre
  };

  bool loadContent(std::vector<std::string>& errors);  // all-or-nothing; keeps old state on failure
  void handleFrameInput(float dt);
  void rebaseIfNeeded();
  void buildOverlay();
  void requestScreenshot();

  AppOptions options_;
  std::unique_ptr<Window> window_;
  std::unique_ptr<VkContext> ctx_;
  std::unique_ptr<Renderer> renderer_;
  InputState input_;
  std::optional<AssetManifest> manifest_;
  SceneDef scene_;
  LocalFrame frame_;
  std::vector<ActiveObject> objects_;
  DevCamera camera_;
  FixedStepClock clock_;
  FrameStats stats_;
  OverlayBuilder overlay_;
  bool showOverlay_ = true;
  std::vector<std::string> contentErrors_;
  std::optional<std::filesystem::path> pendingScreenshot_;
  int screenshotCounter_ = 0;
  std::uint64_t frameCount_ = 0;
  std::uint64_t meshTriangles_ = 0;
  double runTime_ = 0;
};

}  // namespace fh
