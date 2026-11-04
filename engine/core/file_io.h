#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fh {

std::optional<std::vector<std::uint8_t>> readBinaryFile(const std::filesystem::path& path);
std::optional<std::string> readTextFile(const std::filesystem::path& path);
// Writes to "<path>.tmp" then renames over the target so readers never see a partial file.
bool writeFileAtomic(const std::filesystem::path& path, const void* data, std::size_t size);

}  // namespace fh
