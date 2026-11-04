#pragma once
// Developer overlay: builds 2D triangles (text via stb_easy_font + background panels)
// in pixel coordinates. The renderer draws whatever vertices are produced here.
#include <cstdint>
#include <string>
#include <vector>

namespace fh {

struct OverlayVertex {
  float x, y;
  std::uint8_t r, g, b, a;  // sRGB-encoded
};
static_assert(sizeof(OverlayVertex) == 12);

struct OverlayColor {
  std::uint8_t r = 255, g = 255, b = 255, a = 255;
};

class OverlayBuilder {
 public:
  void clear() { vertices_.clear(); }
  void rect(float x, float y, float w, float h, OverlayColor c);
  // Draws text at pixel position; returns the height used. `scale` multiplies the 7px font.
  float text(float x, float y, const std::string& s, OverlayColor c, float scale = 2.0f);
  static float textWidth(const std::string& s, float scale = 2.0f);
  const std::vector<OverlayVertex>& vertices() const { return vertices_; }

 private:
  std::vector<OverlayVertex> vertices_;
};

}  // namespace fh
