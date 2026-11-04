#include "engine/debug/overlay.h"

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

void OverlayBuilder::rect(float x, float y, float w, float h, OverlayColor c) {
  const OverlayVertex v[4] = {{x, y, c.r, c.g, c.b, c.a},
                              {x + w, y, c.r, c.g, c.b, c.a},
                              {x + w, y + h, c.r, c.g, c.b, c.a},
                              {x, y + h, c.r, c.g, c.b, c.a}};
  vertices_.insert(vertices_.end(), {v[0], v[1], v[2], v[0], v[2], v[3]});
}

void OverlayBuilder::line(float x0, float y0, float x1, float y1, float thickness, OverlayColor c) {
  const float dx = x1 - x0, dy = y1 - y0;
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len < 1e-4f) return;
  const float nx = -dy / len * thickness * 0.5f, ny = dx / len * thickness * 0.5f;
  const OverlayVertex v[4] = {{x0 + nx, y0 + ny, c.r, c.g, c.b, c.a},
                              {x1 + nx, y1 + ny, c.r, c.g, c.b, c.a},
                              {x1 - nx, y1 - ny, c.r, c.g, c.b, c.a},
                              {x0 - nx, y0 - ny, c.r, c.g, c.b, c.a}};
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
  vertices_.insert(vertices_.end(), {OverlayVertex{x0, y0, c.r, c.g, c.b, c.a}, OverlayVertex{x1, y1, c.r, c.g, c.b, c.a},
                                     OverlayVertex{x2, y2, c.r, c.g, c.b, c.a}});
}

float OverlayBuilder::text(float x, float y, const std::string& s, OverlayColor c, float scale) {
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
      v[k] = {x + p[0] * scale, y + p[1] * scale, c.r, c.g, c.b, c.a};
    }
    vertices_.insert(vertices_.end(), {v[0], v[1], v[2], v[0], v[2], v[3]});
  }
  return 10.0f * scale;
}

float OverlayBuilder::textWidth(const std::string& s, float scale) {
  std::string copy = s;
  return static_cast<float>(stb_easy_font_width(copy.data())) * scale;
}

}  // namespace fh
