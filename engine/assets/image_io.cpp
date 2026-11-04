#include "engine/assets/image_io.h"

#include <cstring>

#include "engine/core/file_io.h"
#include "engine/core/log.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <stb/stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace fh {

std::optional<ImageRGBA8> decodeImage(const std::uint8_t* data, std::size_t size) {
  int w = 0, h = 0, comp = 0;
  stbi_uc* px = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &comp, 4);
  if (!px) {
    log::error("assets", "image decode failed: {}", stbi_failure_reason());
    return std::nullopt;
  }
  ImageRGBA8 img;
  img.width = w;
  img.height = h;
  img.pixels.assign(px, px + size_t(w) * h * 4);
  stbi_image_free(px);
  return img;
}

std::optional<ImageRGBA8> loadImageFile(const std::filesystem::path& path) {
  auto bytes = readBinaryFile(path);
  if (!bytes) return std::nullopt;
  return decodeImage(bytes->data(), bytes->size());
}

namespace {
void appendBytes(void* ctx, void* data, int size) {
  auto* out = static_cast<std::vector<std::uint8_t>*>(ctx);
  auto* p = static_cast<std::uint8_t*>(data);
  out->insert(out->end(), p, p + size);
}
}  // namespace

bool writePng(const std::filesystem::path& path, const ImageRGBA8& image) {
  if (!image.valid()) return false;
  std::vector<std::uint8_t> encoded;
  if (!stbi_write_png_to_func(appendBytes, &encoded, image.width, image.height, 4, image.pixels.data(),
                              image.width * 4)) {
    log::error("assets", "PNG encode failed for '{}'", path.string());
    return false;
  }
  return writeFileAtomic(path, encoded.data(), encoded.size());
}

}  // namespace fh
