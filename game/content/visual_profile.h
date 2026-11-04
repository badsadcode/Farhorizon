#pragma once
// Visual "looks": post-processing parameters + HUD palette (content/visuals/*.json).
#include <optional>
#include <string>
#include <vector>

#include "engine/assets/asset_manifest.h"
#include "engine/math/math.h"

namespace fh {

struct HudPalette {
  Vec4 primary{1.0f, 0.77f, 0.47f, 0.92f};  // sRGB colour + alpha
  Vec4 nav{0.47f, 0.86f, 1.0f, 0.92f};
  Vec4 field{0.67f, 0.78f, 0.59f, 0.86f};
  Vec4 warn{1.0f, 0.35f, 0.27f, 1.0f};
};

struct VisualProfile {
  std::string id;
  std::string name;
  float bloomStrength = 0.0f;
  float bloomThreshold = 1.0f;
  float bloomKnee = 0.5f;
  float bloomRadius = 1.0f;
  int bloomIterations = 2;
  Vec3 shadowTint{1, 1, 1};
  Vec3 highlightTint{1, 1, 1};
  float saturation = 1.0f;
  float contrast = 1.0f;
  float vignette = 0.0f;
  float scanlines = 0.0f;
  float scanlinePeriod = 3.0f;
  float chromaticAberration = 0.0f;
  float grain = 0.0f;
  float surfaceDetail = 0.5f;    // procedural grime and panel seams on untextured models
  HudPalette hud;
};

struct VisualProfileSet {
  std::vector<VisualProfile> profiles;
  std::string defaultId;
  int indexOf(const std::string& id) const;  // -1 if missing
};

std::optional<VisualProfileSet> parseVisualProfiles(const std::string& json, const std::string& context,
                                                    std::vector<std::string>* errors);
std::optional<VisualProfileSet> loadVisualProfiles(const AssetManifest& manifest, const AssetID& id,
                                                   std::vector<std::string>* errors);

}  // namespace fh
