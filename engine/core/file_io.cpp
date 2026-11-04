#include "engine/core/file_io.h"

#include <fstream>

#include "engine/core/log.h"

namespace fh {

std::optional<std::vector<std::uint8_t>> readBinaryFile(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) {
    log::error("io", "cannot open '{}'", path.string());
    return std::nullopt;
  }
  const auto size = static_cast<std::size_t>(f.tellg());
  std::vector<std::uint8_t> data(size);
  f.seekg(0);
  if (size > 0 && !f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size))) {
    log::error("io", "read failed for '{}'", path.string());
    return std::nullopt;
  }
  return data;
}

std::optional<std::string> readTextFile(const std::filesystem::path& path) {
  auto bytes = readBinaryFile(path);
  if (!bytes) return std::nullopt;
  return std::string(bytes->begin(), bytes->end());
}

bool writeFileAtomic(const std::filesystem::path& path, const void* data, std::size_t size) {
  std::filesystem::path tmp = path;
  tmp += ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) {
      log::error("io", "cannot create '{}'", tmp.string());
      return false;
    }
    f.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!f) {
      log::error("io", "write failed for '{}'", tmp.string());
      return false;
    }
  }
  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    log::error("io", "rename '{}' -> '{}' failed: {}", tmp.string(), path.string(), ec.message());
    return false;
  }
  return true;
}

}  // namespace fh
