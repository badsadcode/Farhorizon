#include "engine/assets/asset_manifest.h"

#include <nlohmann/json.hpp>

#include "engine/core/file_io.h"
#include "engine/core/log.h"

namespace fh {

namespace {
void report(std::vector<std::string>* errors, const std::string& msg) {
  log::error("assets", "{}", msg);
  if (errors) errors->push_back(msg);
}
}  // namespace

std::optional<AssetManifest> AssetManifest::load(const std::filesystem::path& contentRoot,
                                                 std::vector<std::string>* errors) {
  const auto path = contentRoot / "asset_manifest.json";
  auto text = readTextFile(path);
  if (!text) {
    report(errors, "asset manifest missing: " + path.string());
    return std::nullopt;
  }
  return parse(*text, contentRoot, errors);
}

std::optional<AssetManifest> AssetManifest::parse(const std::string& json, const std::filesystem::path& contentRoot,
                                                  std::vector<std::string>* errors) {
  nlohmann::json doc = nlohmann::json::parse(json, nullptr, false);
  if (doc.is_discarded() || !doc.is_object()) {
    report(errors, "asset manifest: invalid JSON");
    return std::nullopt;
  }
  if (doc.value("schemaVersion", 0) != 1) {
    report(errors, "asset manifest: unsupported schemaVersion (expected 1)");
    return std::nullopt;
  }
  if (!doc.contains("assets") || !doc["assets"].is_object()) {
    report(errors, "asset manifest: missing 'assets' object");
    return std::nullopt;
  }
  AssetManifest m;
  m.root_ = contentRoot;
  bool ok = true;
  for (auto& [id, value] : doc["assets"].items()) {
    if (!value.is_object() || !value.contains("type") || !value.contains("source") ||
        !value["type"].is_string() || !value["source"].is_string()) {
      report(errors, "asset '" + id + "': needs string fields 'type' and 'source'");
      ok = false;
      continue;
    }
    AssetEntry e;
    e.id = id;
    e.type = value["type"].get<std::string>();
    e.source = value["source"].get<std::string>();
    e.license = value.value("license", std::string{});
    if (e.source.find("..") != std::string::npos || (!e.source.empty() && (e.source[0] == '/' || e.source[0] == '\\'))) {
      report(errors, "asset '" + id + "': source must be a relative path inside the content root");
      ok = false;
      continue;
    }
    m.entries_.emplace(id, std::move(e));
  }
  if (!ok) return std::nullopt;
  return m;
}

const AssetEntry* AssetManifest::find(const AssetID& id) const {
  auto it = entries_.find(id);
  return it == entries_.end() ? nullptr : &it->second;
}

std::optional<std::filesystem::path> AssetManifest::resolve(const AssetID& id, const char* expectedType) const {
  const AssetEntry* e = find(id);
  if (!e) {
    log::error("assets", "unknown asset ID '{}'", id);
    return std::nullopt;
  }
  if (expectedType && e->type != expectedType) {
    log::error("assets", "asset '{}' has type '{}', expected '{}'", id, e->type, expectedType);
    return std::nullopt;
  }
  return root_ / std::filesystem::path(e->source);
}

std::vector<std::string> AssetManifest::validateFiles() const {
  std::vector<std::string> problems;
  for (const auto& [id, e] : entries_) {
    if (!std::filesystem::exists(root_ / std::filesystem::path(e.source)))
      problems.push_back("asset '" + id + "': file not found: " + e.source);
  }
  return problems;
}

}  // namespace fh
