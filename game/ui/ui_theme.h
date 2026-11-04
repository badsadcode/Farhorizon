#pragma once
// UI theme (content/ui/theme.json): fonts, colours, sizes and panel style for menus and the
// terminal. Layout sizes are authored at 1080p and scaled with the window (and Settings::uiScale).
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "engine/assets/asset_manifest.h"
#include "engine/debug/overlay.h"

namespace fh {

struct UiTheme {
  std::filesystem::path monoFont, sansFont;  // absolute paths after loading
  OverlayColor text{226, 172, 98, 255}, textDim{226, 172, 98, 130}, textBright{255, 218, 160, 255};
  OverlayColor accent{112, 196, 222, 255}, danger{222, 76, 54, 255}, ok{150, 208, 124, 255};
  OverlayColor panel{7, 8, 9, 228}, panelBorder{226, 172, 98, 150}, highlight{226, 172, 98, 46};
  OverlayColor backdrop{0, 0, 0, 165};
  float small = 15, body = 20, heading = 26, title = 88, subtitle = 18;
  float chamfer = 14, border = 1.5f, padding = 22;
  float rowHeight = 40;
  float letterSpacing = 0.32f;  // em, for spaced-out titles
};

std::optional<UiTheme> loadUiTheme(const AssetManifest& manifest, const AssetID& id, std::vector<std::string>* errors);

}  // namespace fh
