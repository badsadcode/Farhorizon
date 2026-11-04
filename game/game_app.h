#pragma once
// FARHORIZON application: loads a data-defined space scene and runs the Stage 2 flight sandbox
// (player ship, station proxy, asteroid fields, HUD, targeting) with developer tooling.
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/assets/asset_manifest.h"
#include "engine/core/clock.h"
#include "engine/debug/frame_stats.h"
#include "engine/debug/overlay.h"
#include "engine/platform/input.h"
#include "game/camera_rig.h"
#include "game/content/scene_def.h"
#include "game/content/visual_profile.h"
#include "engine/audio/audio_device.h"
#include "game/audio_director.h"
#include "game/market_ui.h"
#include "game/persistence/save_game.h"
#include "game/session/game_session.h"
#include "game/session/progression_setup.h"
#include "game/settings.h"
#include "game/ui/menus.h"
#include "game/ui/ui.h"
#include "game/ui/ui_script.h"
#include "game/ui/ui_theme.h"
#include "engine/ui/font_atlas.h"
#include "game/ship_controller.h"
#include "game/world/space_world.h"

namespace fh {

class Window;
class VkContext;
class Renderer;

struct AppOptions {
  std::filesystem::path contentDir;
  std::filesystem::path shaderDir;
  std::string sceneId = "scene.stage2_flight";
  std::string benchmarkId;                       // e.g. bench.asteroid_field_chase
  std::filesystem::path benchmarkOut;            // default: benchmarks/<benchmark id>
  int width = 1600;
  int height = 900;
  bool validation = false;
  bool vsync = true;
  bool devOverlay = true;
  bool autofly = false;
  std::optional<std::string> cameraOverride;
  int exitAfterFrames = 0;                       // 0 = run until closed
  std::optional<std::filesystem::path> screenshotOnExit;
  std::optional<std::string> look;               // visual profile id override
  bool startDocked = false;                      // debug: force-dock at the nearest station
  bool startLanded = false;                      // debug: start landed at the first landing site
  std::string startIn;                           // debug: "ship" or "station" (implies startDocked)
  std::filesystem::path loadPath;                // load this save after startup
  std::filesystem::path settingsPath = "settings.json";
  std::filesystem::path uiScript;                // developer test driver (game/ui/ui_script.h)
};

class GameApp {
 public:
  explicit GameApp(AppOptions options);
  ~GameApp();
  int run();

 private:
  bool loadContent(std::vector<std::string>& errors);  // all-or-nothing; keeps old state on failure
  void handleAppInput();
  void buildOverlay(const CameraView& view, const Mat4& viewProj, const Transform& ship);
  void handleFootInput(float dt);
  void openMarket();
  void closeMarket();
  void saveGame(const std::filesystem::path& path, bool quiet = false);
 public:
  bool loadGame(const std::filesystem::path& path);
 private:
  SpaceWorld& world() { return session_->world(); }
  void buildDevOverlay();
  void setMouseCapture(bool on);
  // Front end (Stage 7).
  void applySettings();
  void applyBindingOverrides();
  void persistSettings();
  void refreshSaveSlots();
  void openMenu(MenuSystem::Screen s);
  void resumeGame();
  void startNewGame();
  void handleMenuActions();
  void buildMenus();
  UiInput uiInput();

  AppOptions options_;
  std::unique_ptr<Window> window_;
  std::unique_ptr<VkContext> ctx_;
  std::unique_ptr<Renderer> renderer_;
  InputState input_;
  std::optional<AssetManifest> manifest_;
  std::optional<BenchmarkDef> benchmark_;
  SceneDef scene_;
  VisualProfileSet looks_;
  int activeLook_ = 0;
  double lookShownUntil_ = 0;
  std::unique_ptr<ProgressionContent> progression_;  // outlives session_ (declared first)
  std::unique_ptr<GameSession> session_;
  // Stage 6 presentation state.
  bool miningMode_ = false;        // weapon selector: false = guns, true = mining laser
  double damageFlashUntil_ = 0;    // red edge flash after a hull hit
  double shieldFlashUntil_ = 0;
  float combatShake_ = 0;
  double missileWarnUntil_ = 0;
  struct PieceInstance {
    std::uint32_t mesh;
    Mat4 model;  // interior space
  };
  std::map<AssetID, std::vector<PieceInstance>> interiorPieces_;
  DockState lastDockState_ = DockState::Undocked;
  MarketPanel market_;
  AudioDirector audio_;
  AudioDevice audioDevice_;
  struct AudioWatch {
    DockState dock = DockState::Undocked;
    Location location = Location::Cockpit;
    double messageTime = -1;
    std::uint32_t collisions = 0;
    std::int64_t credits = 0;
    bool marketOpen = false;
  } audioWatch_;
  void updateAudio(float dt, float engineDemand, float speedFraction);
  bool mouseWasCaptured_ = false;
  std::optional<SaveGame> pendingSave_;
  ShipController controller_;
  CameraRig camera_;
  FixedStepClock clock_;
  FrameStats stats_;
  OverlayBuilder overlay_;
  bool showDevOverlay_ = true;
  bool mouseCaptured_ = false;
  std::vector<std::string> contentErrors_;
  std::optional<std::filesystem::path> pendingScreenshot_;
  int screenshotCounter_ = 0;
  std::uint64_t frameCount_ = 0;
  std::uint32_t visibleAsteroids_ = 0;
  std::uint32_t rebases_ = 0;
  double runTime_ = 0;
  bool quit_ = false;
  Settings settings_;
  std::filesystem::path settingsPath_;
  UiScript uiScript_;
  int lastMenuFocus_ = -1;
  int mouseSettleFrames_ = 0;  // a window resize moves the cursor without the player touching it
  std::uint64_t menuOpenedFrame_ = ~std::uint64_t(0);
  bool scripted_ = false;         // benchmarks / --frames: defaults, no menus, no settings file
  FontAtlas font_;
  UiTheme theme_;
  Ui ui_;
  MenuSystem menus_;
  std::vector<MenuAction> pendingMenuActions_;
  std::vector<SaveSlotInfo> slots_;
  bool freshStart_ = false;
  bool suppressFireUntilRelease_ = false;  // the click that captured the mouse       // next loadContent ignores the current session (new game)
  float lastUiMouseX_ = -1, lastUiMouseY_ = -1;
};

}  // namespace fh
