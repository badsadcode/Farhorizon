#include "engine/debug/overlay.h"

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
