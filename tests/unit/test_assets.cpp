#include <filesystem>

#include "engine/assets/asset_manifest.h"
#include "engine/assets/gltf_loader.h"
#include "test_framework.h"

using namespace fh;

static const std::filesystem::path kContent = FH_TEST_CONTENT_DIR;

TEST(manifest_loads_and_all_files_exist) {
  std::vector<std::string> errors;
  auto m = AssetManifest::load(kContent, &errors);
  CHECK(m.has_value());
  CHECK(errors.empty());
  if (!m) return;
  CHECK(m->find("mesh.ship.sparrow") != nullptr);
  CHECK(m->validateFiles().empty());
  CHECK(!m->resolve("does.not.exist").has_value());
  CHECK(!m->resolve("mesh.ship.sparrow", "scene").has_value());  // type mismatch rejected
}

TEST(manifest_rejects_bad_input) {
  std::vector<std::string> errors;
  CHECK(!AssetManifest::parse("{not json", kContent, &errors));
  CHECK(!AssetManifest::parse(R"({"schemaVersion":2,"assets":{}})", kContent, &errors));
  CHECK(!AssetManifest::parse(R"({"schemaVersion":1,"assets":{"x":{"type":"mesh"}}})", kContent, &errors));
  CHECK(!AssetManifest::parse(R"({"schemaVersion":1,"assets":{"x":{"type":"mesh","source":"../../etc/passwd"}}})",
                              kContent, &errors));
  CHECK(errors.size() >= 4);
}

TEST(gltf_loads_placeholder_ship) {
  std::vector<std::string> errors;
  auto m = AssetManifest::load(kContent, &errors);
  CHECK(m.has_value());
  if (!m) return;
  auto path = m->resolve("mesh.ship.sparrow", "mesh");
  CHECK(path.has_value());
  auto mesh = loadGltfMesh(*path);
  CHECK(mesh.has_value());
  if (!mesh) return;
  CHECK(mesh->triangleCount() > 50);
  CHECK(mesh->materials.size() == 4);
  CHECK(mesh->indices.size() % 3 == 0);
  for (auto i : mesh->indices) CHECK(i < mesh->vertices.size());
  for (const auto& sm : mesh->submeshes) CHECK(sm.firstIndex + sm.indexCount <= mesh->indices.size());
  CHECK(mesh->boundsRadius() > 0.1f);
  for (const auto& v : mesh->vertices) CHECK_NEAR(length(v.normal), 1.0, 1e-3);
}

TEST(gltf_missing_file_reports_error) {
  std::string err;
  CHECK(!loadGltfMesh(kContent / "models/does_not_exist.glb", &err).has_value());
  CHECK(!err.empty());
}
