#pragma once
// Save games (design doc 13): a versioned JSON snapshot written atomically with a backup slot.
// Only *deviations* from the content baseline are stored (asteroid yields that changed, market
// stocks), plus the player's state. Unsupported versions are refused with a clear message.
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "engine/math/frames.h"

namespace fh {

class GameSession;

struct SaveGame {
  static constexpr int kSchemaVersion = 2;  // v2: landing sites (v1 files are migrated)
  int schemaVersion = kSchemaVersion;
  std::string sceneId;
  double worldTime = 0;
  // Player.
  std::string location = "cockpit";  // cockpit | ship | station
  bool docked = false;
  std::string dockedStation;         // prop id
  std::uint32_t dockedBay = 0;
  std::string landedPlanet;          // planet id (v2)
  std::string landedSite;            // site id (v2)
  DVec3 shipPosition;                // system frame, metres
  Quat shipOrientation;
  Vec3 shipVelocity;
  float boostEnergy = 0;
  std::int64_t credits = 0;
  std::map<std::string, int> cargo;
  // World deviations.
  struct MarketState {
    double lastTick = 0;
    std::vector<int> stocks;
    std::vector<double> carry;
  };
  std::map<std::string, MarketState> markets;  // by market asset id
  struct AsteroidState {
    std::string field;
    std::uint32_t index = 0;
    int yield = 0;
  };
  std::vector<AsteroidState> asteroids;        // only those whose yield changed
};

std::string serializeSave(const SaveGame& s);
std::optional<SaveGame> deserializeSave(const std::string& json, std::string* error);

// Writes atomically; the previous save (if any) is kept as "<path>.bak".
bool writeSaveFile(const std::filesystem::path& path, const SaveGame& s, std::string* error);
std::optional<SaveGame> readSaveFile(const std::filesystem::path& path, std::string* error);

// Session <-> snapshot. apply() expects a session freshly built from the same scene with its
// origin at the saved ship position (see GameApp::loadGame / tests).
SaveGame captureSave(GameSession& session, const std::string& sceneId);
bool applySave(GameSession& session, const SaveGame& save, std::string* error);

}  // namespace fh
