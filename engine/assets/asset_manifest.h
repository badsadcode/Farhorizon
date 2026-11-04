#pragma once
// Content refers to assets by stable AssetID strings, never file paths (design doc 9.5).
// The manifest maps each ID to a type and a source file relative to the content root.
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace fh {

using AssetID = std::string;

struct AssetEntry {
  AssetID id;
  std::string type;          // "mesh", "ship", "scene", "config", ...
  std::string source;        // relative to content root, forward slashes
  std::string license;       // provenance for third-party or placeholder assets
};

class AssetManifest {
 public:
  // Parses `<contentRoot>/asset_manifest.json`. Errors are appended to `errors` with context.
  static std::optional<AssetManifest> load(const std::filesystem::path& contentRoot,
                                           std::vector<std::string>* errors = nullptr);
  static std::optional<AssetManifest> parse(const std::string& json, const std::filesystem::path& contentRoot,
                                            std::vector<std::string>* errors = nullptr);

  const AssetEntry* find(const AssetID& id) const;
  // Absolute path for an asset, or nullopt (and an error log naming the ID) if unknown.
  std::optional<std::filesystem::path> resolve(const AssetID& id, const char* expectedType = nullptr) const;
  // Checks every entry's source file exists. Returns human readable problems.
  std::vector<std::string> validateFiles() const;

  const std::filesystem::path& contentRoot() const { return root_; }
  const std::unordered_map<AssetID, AssetEntry>& entries() const { return entries_; }

 private:
  std::filesystem::path root_;
  std::unordered_map<AssetID, AssetEntry> entries_;
};

}  // namespace fh
