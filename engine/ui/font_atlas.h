#pragma once
// TrueType fonts baked into one RGBA atlas (white + coverage alpha) at a set of pixel sizes,
// with stb_truetype. GPU-free: the renderer uploads `pixels()`. Text picks the closest baked
// size and scales the quads, so any size works and the common sizes are pixel-exact.
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace fh {

enum class FontFace : std::uint8_t { Mono = 0, Sans = 1 };

struct FontGlyph {
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // quad relative to the pen at the baseline (baked px)
  float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
  float advance = 0;
};

struct BakedFont {
  float sizePx = 16;
  float ascent = 12, descent = -4, lineGap = 0;  // baked px
  std::vector<FontGlyph> glyphs;                 // indexed via FontAtlas::glyphIndex
};

class FontAtlas {
 public:
  // `faces[i]` is the TTF for FontFace(i). Returns false with a message on failure.
  bool build(const std::vector<std::filesystem::path>& faces, std::string* error);
  bool valid() const { return !pixels_.empty(); }

  std::uint32_t width() const { return width_; }
  std::uint32_t height() const { return height_; }
  const std::vector<std::uint8_t>& pixels() const { return pixels_; }  // RGBA8
  float whiteU() const { return whiteU_; }
  float whiteV() const { return whiteV_; }

  // Closest baked size (prefers the next larger one).
  const BakedFont& pick(FontFace face, float px) const;
  static int glyphIndex(std::uint32_t codepoint);  // -1 = not baked
  float measure(FontFace face, float px, std::string_view utf8) const;
  float ascent(FontFace face, float px) const;
  float lineHeight(FontFace face, float px) const;

  // Decodes one UTF-8 codepoint and advances `i` (invalid bytes yield '?').
  static std::uint32_t nextCodepoint(std::string_view s, size_t& i);

 private:
  std::uint32_t width_ = 0, height_ = 0;
  std::vector<std::uint8_t> pixels_;
  std::array<std::vector<BakedFont>, 2> faces_;
  float whiteU_ = 0, whiteV_ = 0;
};

}  // namespace fh
