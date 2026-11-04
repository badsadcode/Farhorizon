cmake_minimum_required(VERSION 3.24)
project(FARHORIZON VERSION 0.8.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Static MSVC runtime: the packaged game runs without the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(FH_BUILD_GAME   "Build the game executable (needs Vulkan + SDL3)" ON)
option(FH_BUILD_TESTS  "Build headless unit tests" ON)
option(FH_SANITIZERS   "Enable ASan/UBSan in Debug builds (GCC/Clang)" OFF)
option(FH_SYSTEM_SDL3  "Use an installed SDL3 instead of fetching the pinned release" OFF)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(Warnings)

# ---- Vendored header-only third-party code (see docs/DEPENDENCIES.md) ----
add_library(fh_third_party INTERFACE)
target_include_directories(fh_third_party SYSTEM INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# ---- fh_core: platform-independent, GPU-free code. Unit tests link only this. ----
add_library(fh_core STATIC
  engine/core/log.cpp
  engine/core/file_io.cpp
  engine/core/clock.cpp
  engine/assets/asset_manifest.cpp
  engine/assets/gltf_loader.cpp
  engine/assets/image_io.cpp
  engine/debug/frame_stats.cpp
  engine/debug/overlay.cpp
  engine/ui/font_atlas.cpp
  engine/render/starfield.cpp
  engine/render/particle_atlas.cpp
  game/fx/particles.cpp
  game/content/ship_def.cpp
  game/content/scene_def.cpp
  game/content/visual_profile.cpp
  game/ships/flight_model.cpp
  game/world/asteroid_gen.cpp
  game/world/space_world.cpp
  game/world/terrain.cpp
  game/physics/collision.cpp
  game/player/character_controller.cpp
  game/space/docking.cpp
  game/content/interior_def.cpp
  game/content/map_def.cpp
  game/session/game_session.cpp
  game/session/world_builder.cpp
  game/economy/economy.cpp
  game/space/mining.cpp
  game/combat/weapon_def.cpp
  game/combat/combat.cpp
  game/progression/upgrades.cpp
  game/missions/contracts.cpp
  game/session/progression_setup.cpp
  game/settings.cpp
  game/ui/ui_theme.cpp
  game/ui/ui.cpp
  game/ui/bindings.cpp
  game/space/travel_drive.cpp
  game/space/landing.cpp
  game/persistence/save_game.cpp
  engine/audio/mixer.cpp
  game/audio_director.cpp
)
target_include_directories(fh_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fh_core PUBLIC fh_third_party)
fh_set_warnings(fh_core)

if(FH_BUILD_GAME)
  include(Dependencies)   # SDL3 + Vulkan
  include(Shaders)

  add_library(fh_engine STATIC
    engine/platform/window.cpp
    engine/platform/input.cpp
    engine/audio/audio_device.cpp
    engine/render/vk_context.cpp
    engine/render/gpu_resources.cpp
    engine/render/renderer.cpp
  )
  target_link_libraries(fh_engine PUBLIC fh_core SDL3::SDL3 Vulkan::Vulkan)
  fh_set_warnings(fh_engine)

  fh_compile_shaders(fh_shaders
    shaders/sky.vert shaders/sky.frag
    shaders/stars.vert shaders/stars.frag
    shaders/mesh.vert shaders/mesh.frag
    shaders/overlay.vert shaders/overlay.frag
    shaders/tonemap.vert shaders/tonemap.frag
    shaders/bloom_down.frag shaders/bloom_blur.frag
    shaders/glow.vert shaders/glow.frag
    shaders/planet.frag
    shaders/shadow_sun.vert shaders/shadow_point.vert
    shaders/ssao.frag shaders/ssao_blur.frag
    shaders/particle.vert shaders/particle.frag
    shaders/cloud.frag shaders/cloud_composite.frag
    shaders/ribbon.vert shaders/ribbon.frag shaders/tunnel.frag)

  add_executable(farhorizon
    game/main.cpp
    game/game_app.cpp
    game/camera_rig.cpp
    game/ship_controller.cpp
    game/hud.cpp
    game/market_ui.cpp
    game/ui/menus.cpp
    game/ui/ui_script.cpp
  )
  target_link_libraries(farhorizon PRIVATE fh_engine)
  add_dependencies(farhorizon fh_shaders)
  fh_set_warnings(farhorizon)
  # Runtime data locations are passed in so a build directory runs without copying.
  target_compile_definitions(farhorizon PRIVATE
    FH_DEFAULT_CONTENT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/content"
    FH_DEFAULT_SHADER_DIR="${FH_SHADER_OUTPUT_DIR}")
  if(WIN32)
    set_target_properties(farhorizon PROPERTIES WIN32_EXECUTABLE OFF) # keep console for logs in Stage 1
  endif()
endif()

# ---- Tools (GPU-free) ----
add_executable(content_validator tools/content_validator/main.cpp)
target_link_libraries(content_validator PRIVATE fh_core)
fh_set_warnings(content_validator)

if(FH_BUILD_TESTS)
  enable_testing()
  add_executable(fh_unit_tests
    tests/unit/test_main.cpp
    tests/unit/test_math.cpp
    tests/unit/test_frames.cpp
    tests/unit/test_clock.cpp
    tests/unit/test_assets.cpp
    tests/unit/test_content.cpp
    tests/unit/test_starfield.cpp
    tests/unit/test_flight.cpp
    tests/unit/test_world.cpp
    tests/unit/test_character.cpp
    tests/unit/test_docking.cpp
    tests/unit/test_economy.cpp
    tests/unit/test_stage4.cpp
    tests/unit/test_audio.cpp
    tests/unit/test_stage5.cpp
    tests/unit/test_stage6.cpp
    tests/unit/test_ui.cpp
    tests/unit/test_fx.cpp
    tests/unit/test_map.cpp
  )
  target_link_libraries(fh_unit_tests PRIVATE fh_core)
  target_compile_definitions(fh_unit_tests PRIVATE
    FH_TEST_CONTENT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/content"
    FH_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  fh_set_warnings(fh_unit_tests)
  add_test(NAME unit COMMAND fh_unit_tests)
  add_test(NAME content COMMAND content_validator ${CMAKE_CURRENT_SOURCE_DIR}/content)
endif()
