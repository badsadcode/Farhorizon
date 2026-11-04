#include "game/ui/ui_theme.h"

#include <algorithm>

#include "engine/core/file_io.h"
#include "game/content/json_util.h"

namespace fh {

std::optional<UiTheme> loadUiTheme(const AssetManifest& manifest, const AssetID& id, std::vector<std::string>* errors) {
  auto path = manifest.resolve(id, "ui_theme");
  auto text = path ? readTextFile(*path) : std::nullopt;
  content::Reader r("ui theme '" + id + "'", errors);
  if (!text) {
    r.error("missing or unreadable");
    return std::nullopt;
  }
  auto j = nlohmann::json::parse(*text, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    r.error("invalid JSON");
    return std::nullopt;
  }
  UiTheme t;
  auto font = [&](const char* key, std::filesystem::path& out) {
    const std::string rel = j.contains("fonts") ? r.str(j["fonts"], key) : std::string();
    if (rel.empty() || rel.find("..") != std::string::npos) {
      r.error(std::string("fonts.") + key + " must be a path inside the content root");
      return;
    }
    out = manifest.contentRoot() / rel;
    if (!std::filesystem::exists(out)) r.error("font file not found: " + rel);
  };
  font("mono", t.monoFont);
  font("sans", t.sansFont);
  if (j.contains("colors") && j["colors"].is_object()) {
    auto color = [&](const char* key, OverlayColor& c) {
      const auto& cj = j["colors"];
      if (!cj.contains(key)) return;
      const auto& a = cj[key];
      if (!a.is_array() || a.size() != 4) {
        r.error(std::string("colors.") + key + " must be [r,g,b,a] (0-255)");
        return;
      }
      auto b = [&](size_t i) { return std::uint8_t(std::clamp(a[i].is_number() ? a[i].get<int>() : 0, 0, 255)); };
      c = {b(0), b(1), b(2), b(3)};
    };
    color("text", t.text);
    color("textDim", t.textDim);
    color("textBright", t.textBright);
    color("accent", t.accent);
    color("danger", t.danger);
    color("ok", t.ok);
    color("panel", t.panel);
    color("panelBorder", t.panelBorder);
    color("highlight", t.highlight);
    color("backdrop", t.backdrop);
  }
  if (j.contains("sizes") && j["sizes"].is_object()) {
    const auto& s = j["sizes"];
    t.small = float(r.num(s, "small", false, t.small));
    t.body = float(r.num(s, "body", false, t.body));
    t.heading = float(r.num(s, "heading", false, t.heading));
    t.title = float(r.num(s, "title", false, t.title));
    t.subtitle = float(r.num(s, "subtitle", false, t.subtitle));
  }
  if (j.contains("panel") && j["panel"].is_object()) {
    const auto& p = j["panel"];
    t.chamfer = float(r.num(p, "chamfer", false, t.chamfer));
    t.border = float(r.num(p, "border", false, t.border));
    t.padding = float(r.num(p, "padding", false, t.padding));
  }
  t.rowHeight = float(r.num(j, "rowHeight", false, t.rowHeight));
  t.letterSpacing = float(r.num(j, "letterSpacing", false, t.letterSpacing));
  for (float v : {t.small, t.body, t.heading, t.title, t.subtitle, t.rowHeight})
    if (v < 6 || v > 200) r.error("sizes must be within 6..200 px");
  if (!r.ok()) return std::nullopt;
  return t;
}

}  // namespace fh
