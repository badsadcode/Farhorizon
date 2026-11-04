#pragma once
// 2D overlay geometry (HUD, menus, developer text) in pixel coordinates, top-left origin.
// Everything samples one RGBA atlas: glyphs from the TrueType FontAtlas, solid shapes from its
// white texel, so the whole overlay is a single draw call. Without a font (tools, early
// startup) text falls back to stb_easy_font quads.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "engine/ui/font_atlas.h"

namespace fh {

struct OverlayVertex {
  float x, y;
  float u, v;
  std::uint8_t r, g, b, a;  // sRGB-encoded
};
static_assert(sizeof(OverlayVertex) == 20);

struct OverlayColor {
  std::uint8_t r = 255, g = 255, b = 255, a = 255;
  OverlayColor withAlpha(float k) const {
    return {r, g, b, std::uint8_t(float(a) * (k < 0 ? 0 : k > 1 ? 1 : k) + 0.5f)};
  }
};

enum class TextAlign { Left, Center, Right };

class OverlayBuilder {
 public:
  // The font used by every builder (set once after the atlas is built; may be null).
  static void setFont(const FontAtlas* font) { font_ = font; }
  static const FontAtlas* font() { return font_; }

  void clear() { vertices_.clear(); }
  void rect(float x, float y, float w, float h, OverlayColor c);
  void line(float x0, float y0, float x1, float y1, float thickness, OverlayColor c);
  void circle(float cx, float cy, float radius, float thickness, OverlayColor c, int segments = 32);
  void triangle(float x0, float y0, float x1, float y1, float x2, float y2, OverlayColor c);
  // Panel with the top-left and bottom-right corners cut (`chamfer` px). `border` alpha 0 = none.
  void panel(float x, float y, float w, float h, OverlayColor fill, OverlayColor border, float chamfer = 10.0f,
             float borderWidth = 1.5f);
  // Legacy sizing: `scale` 2 = the old 14 px debug text height. Returns the line height used.
  float text(float x, float y, const std::string& s, OverlayColor c, float scale = 2.0f);
  static float textWidth(const std::string& s, float scale = 2.0f);
  // Pixel-sized text; `y` is the top of the line. Returns the advance width.
  float textPx(float x, float y, std::string_view s, OverlayColor c, float px, FontFace face = FontFace::Mono,
               TextAlign align = TextAlign::Left);
  static float measure(std::string_view s, float px, FontFace face = FontFace::Mono);
  static float lineHeight(float px, FontFace face = FontFace::Mono);

  const std::vector<OverlayVertex>& vertices() const { return vertices_; }

 private:
  void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, OverlayColor c);
  OverlayVertex solid(float x, float y, OverlayColor c) const;
  static float legacyPx(float scale) { return scale * 9.0f; }

  static inline const FontAtlas* font_ = nullptr;
  std::vector<OverlayVertex> vertices_;
};

}  // namespace fh
