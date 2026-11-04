#include "engine/ui/font_atlas.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "engine/core/file_io.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb/stb_truetype.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace fh {

namespace {
// ASCII printable + a few typographic extras used by the HUD.
constexpr std::uint32_t kExtras[] = {0xB0 /*°*/, 0xB7 /*·*/, 0xD7 /*×*/};
constexpr int kAscii = 95;  // 32..126
constexpr int kGlyphs = kAscii + int(std::size(kExtras));

const std::vector<float>& bakedSizes(int face) {
  static const std::vector<float> mono{12, 14, 16, 18, 20, 22, 26, 32, 40, 52};
  static const std::vector<float> sans{14, 16, 20, 24, 32, 44, 64, 88};
  return face == 0 ? mono : sans;
}
}  // namespace

int FontAtlas::glyphIndex(std::uint32_t cp) {
  if (cp >= 32 && cp <= 126) return int(cp - 32);
  for (int i = 0; i < int(std::size(kExtras)); ++i)
    if (kExtras[i] == cp) return kAscii + i;
  return -1;
}

std::uint32_t FontAtlas::nextCodepoint(std::string_view s, size_t& i) {
  const auto b = static_cast<unsigned char>(s[i++]);
  if (b < 0x80) return b;
  int extra = (b & 0xE0) == 0xC0 ? 1 : (b & 0xF0) == 0xE0 ? 2 : (b & 0xF8) == 0xF0 ? 3 : -1;
  if (extra < 0) return '?';
  std::uint32_t cp = b & (0x3F >> extra);
  for (int k = 0; k < extra; ++k) {
    if (i >= s.size() || (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) return '?';
    cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 0x3F);
  }
  return cp;
}

bool FontAtlas::build(const std::vector<std::filesystem::path>& faces, std::string* error) {
  auto fail = [&](const std::string& m) {
    if (error) *error = m;
    pixels_.clear();
    return false;
  };
  if (faces.empty() || faces.size() > 2) return fail("expected 1 or 2 font faces");
  std::vector<std::vector<unsigned char>> data;
  for (const auto& p : faces) {
    auto bytes = readBinaryFile(p);
    if (!bytes || bytes->empty()) return fail("cannot read font '" + p.string() + "'");
    data.emplace_back(bytes->begin(), bytes->end());
  }
  width_ = 2048;
  const std::uint32_t packH = 2048;
  height_ = packH + 4;  // 4 spare rows hold the white block for solid shapes
  std::vector<unsigned char> alpha(size_t(width_) * height_, 0);
  stbtt_pack_context pc;
  if (!stbtt_PackBegin(&pc, alpha.data(), int(width_), int(packH), int(width_), 1, nullptr))
    return fail("stbtt_PackBegin failed");
  for (size_t f = 0; f < 2; ++f) {
    const size_t src = std::min(f, data.size() - 1);  // one face given = used for both
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, data[src].data(), stbtt_GetFontOffsetForIndex(data[src].data(), 0)))
      return fail("invalid TrueType data in '" + faces[src].string() + "'");
    faces_[f].clear();
    for (float size : bakedSizes(int(f))) {
      stbtt_PackSetOversampling(&pc, size <= 22 ? 2 : 1, 1);
      std::vector<stbtt_packedchar> chars(kGlyphs);
      std::vector<int> codepoints;
      for (int c = 32; c <= 126; ++c) codepoints.push_back(c);
      for (auto e : kExtras) codepoints.push_back(int(e));
      stbtt_pack_range range{};
      range.font_size = size;
      range.array_of_unicode_codepoints = codepoints.data();
      range.num_chars = kGlyphs;
      range.chardata_for_range = chars.data();
      if (!stbtt_PackFontRanges(&pc, data[src].data(), 0, &range, 1))
        return fail("font atlas full while packing size " + std::to_string(int(size)));
      BakedFont bf;
      bf.sizePx = size;
      float asc = 0, desc = 0, gap = 0;
      stbtt_GetScaledFontVMetrics(data[src].data(), 0, size, &asc, &desc, &gap);
      bf.ascent = asc;
      bf.descent = desc;
      bf.lineGap = gap;
      bf.glyphs.resize(kGlyphs);
      for (int g = 0; g < kGlyphs; ++g) {
        const stbtt_packedchar& pcd = chars[size_t(g)];
        FontGlyph& gl = bf.glyphs[size_t(g)];
        gl.x0 = pcd.xoff;
        gl.y0 = pcd.yoff;
        gl.x1 = pcd.xoff2;
        gl.y1 = pcd.yoff2;
        gl.u0 = float(pcd.x0) / float(width_);
        gl.v0 = float(pcd.y0) / float(height_);
        gl.u1 = float(pcd.x1) / float(width_);
        gl.v1 = float(pcd.y1) / float(height_);
        gl.advance = pcd.xadvance;
      }
      faces_[f].push_back(std::move(bf));
    }
  }
  stbtt_PackEnd(&pc);
  // White block in the spare rows (sampled at its centre for solid geometry).
  for (std::uint32_t y = packH; y < height_; ++y)
    for (std::uint32_t x = 0; x < 4; ++x) alpha[size_t(y) * width_ + x] = 255;
  whiteU_ = 2.0f / float(width_);
  whiteV_ = (float(packH) + 2.0f) / float(height_);
  pixels_.assign(size_t(width_) * height_ * 4, 255);
  for (size_t i = 0; i < alpha.size(); ++i) pixels_[i * 4 + 3] = alpha[i];
  return true;
}

const BakedFont& FontAtlas::pick(FontFace face, float px) const {
  const auto& list = faces_[size_t(face)];
  for (const auto& f : list)
    if (f.sizePx >= px * 0.95f) return f;
  return list.back();
}

float FontAtlas::measure(FontFace face, float px, std::string_view s) const {
  if (!valid()) return 0;
  const BakedFont& f = pick(face, px);
  const float k = px / f.sizePx;
  float w = 0;
  for (size_t i = 0; i < s.size();) {
    const int g = glyphIndex(nextCodepoint(s, i));
    w += f.glyphs[size_t(g < 0 ? glyphIndex('?') : g)].advance * k;
  }
  return w;
}

float FontAtlas::ascent(FontFace face, float px) const {
  const BakedFont& f = pick(face, px);
  return f.ascent * px / f.sizePx;
}

float FontAtlas::lineHeight(FontFace face, float px) const {
  const BakedFont& f = pick(face, px);
  return (f.ascent - f.descent + f.lineGap) * px / f.sizePx;
}

}  // namespace fh
