#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>

#include "engine/assets/mesh_data.h"

namespace fh {

std::optional<ImageRGBA8> decodeImage(const std::uint8_t* data, std::size_t size);
std::optional<ImageRGBA8> loadImageFile(const std::filesystem::path& path);
bool writePng(const std::filesystem::path& path, const ImageRGBA8& image);

}  // namespace fh
