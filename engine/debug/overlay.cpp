#include "engine/debug/overlay.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include <stb/stb_easy_font.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace fh {

OverlayVertex OverlayBuilder::solid(float x, float y, OverlayColor c) const {
  const float u = font_ ? font_->whiteU() : 0.5f, v = font_ ? font_->whiteV() : 0.5f;
  return {x, y, u, v, c.r, c.g, c.b, c.a};
}

void OverlayBuilder::quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                          OverlayColor c) {
  const OverlayVertex a{x0, y0, u0, v0, c.r, c.g, c.b, c.a}, b{x1, y0, u1, v0, c.r, c.g, c.b, c.a},
      d{x1, y1, u1, v1, c.r, c.g, c.b, c.a}, e{x0, y1, u0, v1, c.r, c.g, c.b, c.a};
  vertices_.insert(vertices_.end(), {a, b, d, a, d, e});
}

void OverlayBuilder::rect(float x, float y, float w, float h, OverlayColor c) {
  const OverlayVertex v[4] = {solid(x, y, c), solid(x + w, y, c), solid(x + w, y + h, c), solid(x, y + h, c)};
  vertices_.insert(vertices_.end(), {v[0], v[1], v[2], v[0], v[2], v[3]});
}

void OverlayBuilder::line(float x0, float y0, float x1, float y1, float thickness, OverlayColor c) {
  const float dx = x1 - x0, dy = y1 - y0;
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len < 1e-4f) return;
  const float nx = -dy / len * thickness * 0.5f, ny = dx / len * thickness * 0.5f;
  const OverlayVertex v[4] = {solid(x0 + nx, y0 + ny, c), solid(x1 + nx, y1 + ny, c), solid(x1 - nx, y1 - ny, c),
                              solid(x0 - nx, y0 - ny, c)};
  vertices_.insert(vertices_.end(), {v[0], v[1], v[2], v[0], v[2], v[3]});
}

void OverlayBuilder::circle(float cx, float cy, float radius, float thickness, OverlayColor c, int segments) {
  const float step = 6.2831853f / float(segments);
  for (int i = 0; i < segments; ++i) {
    const float a0 = step * float(i), a1 = step * float(i + 1);
    line(cx + std::cos(a0) * radius, cy + std::sin(a0) * radius, cx + std::cos(a1) * radius,
         cy + std::sin(a1) * radius, thickness, c);
  }
}

void OverlayBuilder::triangle(float x0, float y0, float x1, float y1, float x2, float y2, OverlayColor c) {
  vertices_.insert(vertices_.end(), {solid(x0, y0, c), solid(x1, y1, c), solid(x2, y2, c)});
}

void OverlayBuilder::panel(float x, float y, float w, float h, OverlayColor fill, OverlayColor border, float ch,
                           float bw) {
  ch = std::min(ch, std::min(w, h) * 0.5f);
  // Hexagon outline: top-left and bottom-right corners cut.
  const float px[6] = {x + ch, x + w, x + w, x + w - ch, x, x};
  const float py[6] = {y, y, y + h - ch, y + h, y + h, y + ch};
  if (fill.a > 0) {
    const float cx = x + w * 0.5f, cy = y + h * 0.5f;
    for (int i = 0; i < 6; ++i) triangle(cx, cy, px[i], py[i], px[(i + 1) % 6], py[(i + 1) % 6], fill);
  }
  if (border.a > 0 && bw > 0)
    for (int i = 0; i < 6; ++i) line(px[i], py[i], px[(i + 1) % 6], py[(i + 1) % 6], bw, border);
}

float OverlayBuilder::textPx(float x, float y, std::string_view s, OverlayColor c, float px, FontFace face,
                             TextAlign align) {
  if (!font_ || !font_->valid()) {  // fallback: blocky debug font
    const float scale = px / 9.0f;
    const float w = textWidth(std::string(s), scale);
    if (align == TextAlign::Center) x -= w * 0.5f;
    if (align == TextAlign::Right) x -= w;
    text(x, y, std::string(s), c, scale);
    return w;
  }
  const BakedFont& f = font_->pick(face, px);
  const float k = px / f.sizePx;
  const float w = font_->measure(face, px, s);
  if (align == TextAlign::Center) x -= w * 0.5f;
  if (align == TextAlign::Right) x -= w;
  // Pixel-snap the pen when the baked size is used 1:1 (crisp small text).
  const bool exact = std::fabs(k - 1.0f) < 1e-3f;
  float penX = exact ? std::round(x) : x;
  const float baseY = exact ? std::round(y + f.ascent) : y + f.ascent * k;
  for (size_t i = 0; i < s.size();) {
    int g = FontAtlas::glyphIndex(FontAtlas::nextCodepoint(s, i));
    if (g < 0) g = FontAtlas::glyphIndex('?');
    const FontGlyph& gl = f.glyphs[size_t(g)];
    if (gl.x1 > gl.x0)
      quad(penX + gl.x0 * k, baseY + gl.y0 * k, penX + gl.x1 * k, baseY + gl.y1 * k, gl.u0, gl.v0, gl.u1, gl.v1, c);
    penX += gl.advance * k;
  }
  return w;
}

float OverlayBuilder::measure(std::string_view s, float px, FontFace face) {
  if (!font_ || !font_->valid()) return textWidth(std::string(s), px / 9.0f);
  return font_->measure(face, px, s);
}

float OverlayBuilder::lineHeight(float px, FontFace face) {
  if (!font_ || !font_->valid()) return px * 1.2f;
  return font_->lineHeight(face, px);
}

float OverlayBuilder::text(float x, float y, const std::string& s, OverlayColor c, float scale) {
  if (font_ && font_->valid()) {
    textPx(x, y, s, c, legacyPx(scale));
    return 10.0f * scale;
  }
  // stb_easy_font emits quads: 4 vertices of {float x,y,z; uint8 rgba[4]} = 16 bytes each.
  static thread_local std::vector<char> buffer(1 << 18);
  std::string copy = s;
  unsigned char col[4] = {c.r, c.g, c.b, c.a};
  const int quads = stb_easy_font_print(0, 0, copy.data(), col, buffer.data(), static_cast<int>(buffer.size()));
  for (int q = 0; q < quads; ++q) {
    OverlayVertex v[4];
    for (int k = 0; k < 4; ++k) {
      float p[3];
      std::memcpy(p, buffer.data() + (q * 4 + k) * 16, sizeof(p));
      v[k] = solid(x + p[0] * scale, y + p[1] * scale, c);
    }
    vertices_.insert(vertices_.end(), {v[0], v[1], v[2], v[0], v[2], v[3]});
  }
  return 10.0f * scale;
}

float OverlayBuilder::textWidth(const std::string& s, float scale) {
  if (font_ && font_->valid()) return font_->measure(FontFace::Mono, legacyPx(scale), s);
  std::string copy = s;
  return static_cast<float>(stb_easy_font_width(copy.data())) * scale;
}

}  // namespace fh
