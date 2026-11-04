#include "engine/assets/gltf_loader.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <tuple>
#include <string_view>

#include "engine/assets/image_io.h"
#include "engine/core/file_io.h"
#include "engine/core/log.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define CGLTF_IMPLEMENTATION
#include <cgltf/cgltf.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace fh {

namespace {

struct CgltfDeleter {
  void operator()(cgltf_data* d) const { cgltf_free(d); }
};

Mat4 toMat4(const float m[16]) {
  Mat4 r;
  float* o = &r.c[0].x;
  for (int i = 0; i < 16; ++i) o[i] = m[i];
  return r;
}

const cgltf_accessor* findAttribute(const cgltf_primitive& prim, cgltf_attribute_type type, int index = 0) {
  for (cgltf_size i = 0; i < prim.attributes_count; ++i)
    if (prim.attributes[i].type == type && prim.attributes[i].index == index) return prim.attributes[i].data;
  return nullptr;
}

bool fail(std::string* error, const std::filesystem::path& path, const std::string& msg) {
  const std::string full = path.string() + ": " + msg;
  log::error("gltf", "{}", full);
  if (error) *error = full;
  return false;
}

}  // namespace

std::optional<MeshData> loadGltfMesh(const std::filesystem::path& path, std::string* error, GltfSceneInfo* sceneInfo) {
  cgltf_options options{};
  cgltf_data* raw = nullptr;
  const std::string pathStr = path.string();
  if (const cgltf_result res = cgltf_parse_file(&options, pathStr.c_str(), &raw); res != cgltf_result_success) {
    fail(error, path, "failed to parse glTF/GLB (cgltf_result " + std::to_string(int(res)) + ")");
    return std::nullopt;
  }
  std::unique_ptr<cgltf_data, CgltfDeleter> data(raw);
  if (cgltf_load_buffers(&options, data.get(), pathStr.c_str()) != cgltf_result_success) {
    fail(error, path, "failed to load buffers");
    return std::nullopt;
  }
  if (const cgltf_result res = cgltf_validate(data.get()); res != cgltf_result_success) {
    fail(error, path, "glTF validation failed (cgltf_result " + std::to_string(int(res)) + ")");
    return std::nullopt;
  }

  MeshData mesh;

  // ---- Images (decoded lazily per material use, keyed by cgltf image index) ----
  std::vector<int> imageRemap(data->images_count, -2);  // -2 = not attempted
  auto loadImage = [&](const cgltf_image* img, bool linear = false) -> int {
    const auto idx = static_cast<size_t>(img - data->images);
    if (imageRemap[idx] != -2) return imageRemap[idx];
    std::optional<ImageRGBA8> decoded;
    if (img->buffer_view) {
      const auto* bytes = static_cast<const std::uint8_t*>(img->buffer_view->buffer->data) + img->buffer_view->offset;
      decoded = decodeImage(bytes, img->buffer_view->size);
    } else if (img->uri && std::string_view(img->uri).rfind("data:", 0) != 0) {
      decoded = loadImageFile(path.parent_path() / img->uri);
    } else {
      log::warn("gltf", "{}: data-URI images are not supported; using white", pathStr);
    }
    if (decoded) {
      decoded->linear = linear;
      mesh.images.push_back(std::move(*decoded));
      imageRemap[idx] = static_cast<int>(mesh.images.size() - 1);
    } else {
      imageRemap[idx] = -1;
    }
    return imageRemap[idx];
  };

  // ---- Materials ----
  for (cgltf_size i = 0; i < data->materials_count; ++i) {
    const cgltf_material& src = data->materials[i];
    MaterialData m;
    m.name = src.name ? src.name : ("material_" + std::to_string(i));
    if (src.has_pbr_metallic_roughness) {
      const auto& pbr = src.pbr_metallic_roughness;
      m.baseColorFactor = {pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2],
                           pbr.base_color_factor[3]};
      m.metallic = pbr.metallic_factor;
      m.roughness = pbr.roughness_factor;
      if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image)
        m.baseColorImage = loadImage(pbr.base_color_texture.texture->image);
      if (pbr.metallic_roughness_texture.texture && pbr.metallic_roughness_texture.texture->image)
        m.metalRoughImage = loadImage(pbr.metallic_roughness_texture.texture->image, true);
    }
    if (src.normal_texture.texture && src.normal_texture.texture->image)
      m.normalImage = loadImage(src.normal_texture.texture->image, true);
    if (src.occlusion_texture.texture && src.occlusion_texture.texture->image)
      m.occlusionImage = loadImage(src.occlusion_texture.texture->image, true);
    if (src.emissive_texture.texture && src.emissive_texture.texture->image)
      m.emissiveImage = loadImage(src.emissive_texture.texture->image);
    m.emissive = {src.emissive_factor[0], src.emissive_factor[1], src.emissive_factor[2]};
    if (src.has_emissive_strength) m.emissive *= src.emissive_strength.emissive_strength;
    mesh.materials.push_back(std::move(m));
  }

  // ---- Geometry: walk the default scene, bake node transforms into vertices ----
  Vec3 bmin{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
  Vec3 bmax = -bmin;

  auto appendPrimitive = [&](MeshData& mesh, const cgltf_primitive& prim, const Mat4& world) -> bool {
    if (prim.type != cgltf_primitive_type_triangles) {
      log::warn("gltf", "{}: skipping non-triangle primitive", pathStr);
      return true;
    }
    const cgltf_accessor* pos = findAttribute(prim, cgltf_attribute_type_position);
    if (!pos) return fail(error, path, "primitive without POSITION");
    const cgltf_accessor* nrm = findAttribute(prim, cgltf_attribute_type_normal);
    const cgltf_accessor* uv = findAttribute(prim, cgltf_attribute_type_texcoord, 0);

    // Normal matrix = inverse-transpose of the upper 3x3.
    const Mat4 normalMat = world.inverse().transposed();
    const Vec3 c0 = world.transformDir({1, 0, 0}), c1 = world.transformDir({0, 1, 0}),
               c2 = world.transformDir({0, 0, 1});
    const bool mirrored = dot(cross(c0, c1), c2) < 0.0f;

    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    for (cgltf_size v = 0; v < pos->count; ++v) {
      MeshVertex mv{};
      float tmp[4] = {0, 0, 0, 0};
      cgltf_accessor_read_float(pos, v, tmp, 3);
      mv.position = world.transformPoint({tmp[0], tmp[1], tmp[2]});
      if (nrm) {
        cgltf_accessor_read_float(nrm, v, tmp, 3);
        mv.normal = normalize(normalMat.transformDir({tmp[0], tmp[1], tmp[2]}));
      }
      if (uv) {
        cgltf_accessor_read_float(uv, v, tmp, 2);
        mv.uv = {tmp[0], tmp[1]};
      }
      for (int k = 0; k < 3; ++k) {
        bmin[k] = std::min(bmin[k], mv.position[k]);
        bmax[k] = std::max(bmax[k], mv.position[k]);
      }
      mesh.vertices.push_back(mv);
    }

    SubMesh sm;
    sm.firstIndex = static_cast<std::uint32_t>(mesh.indices.size());
    sm.material = prim.material ? static_cast<int>(prim.material - data->materials) : -1;
    const cgltf_size count = prim.indices ? prim.indices->count : pos->count;
    if (count % 3 != 0) return fail(error, path, "triangle index count not divisible by 3");
    for (cgltf_size t = 0; t < count; t += 3) {
      std::uint32_t tri[3];
      for (int k = 0; k < 3; ++k)
        tri[k] = base + static_cast<std::uint32_t>(prim.indices ? cgltf_accessor_read_index(prim.indices, t + k) : t + k);
      if (mirrored) std::swap(tri[1], tri[2]);
      mesh.indices.insert(mesh.indices.end(), tri, tri + 3);
      if (!nrm) {  // flat normals for meshes exported without them
        const Vec3 n = normalize(cross(mesh.vertices[tri[1]].position - mesh.vertices[tri[0]].position,
                                       mesh.vertices[tri[2]].position - mesh.vertices[tri[0]].position));
        for (auto i : tri) mesh.vertices[i].normal = n;
      }
    }
    sm.indexCount = static_cast<std::uint32_t>(mesh.indices.size()) - sm.firstIndex;
    mesh.submeshes.push_back(sm);
    return true;
  };

  // Splits an emissive primitive into cells of a few metres, each a candidate light.
  auto collectEmitters = [&](const cgltf_primitive& prim, const Mat4& world, const std::string& name) {
    if (!prim.material || prim.type != cgltf_primitive_type_triangles) return;
    const MaterialData& m = mesh.materials[size_t(prim.material - data->materials)];
    Vec3 radiance = m.emissive;
    if (radiance.x + radiance.y + radiance.z <= 0.001f) return;
    if (m.emissiveImage >= 0 && size_t(m.emissiveImage) < mesh.images.size()) {
      const ImageRGBA8& img = mesh.images[size_t(m.emissiveImage)];
      Vec3 sum{};
      float count = 0;
      for (size_t i = 0; i + 3 < img.pixels.size(); i += 4 * 17) {
        for (int k = 0; k < 3; ++k) sum[k] += std::pow(float(img.pixels[i + size_t(k)]) / 255.0f, 2.2f);
        count += 1;
      }
      if (count > 0) radiance = {radiance.x * sum.x / count, radiance.y * sum.y / count, radiance.z * sum.z / count};
    }
    const cgltf_accessor* pos = findAttribute(prim, cgltf_attribute_type_position);
    if (!pos) return;
    constexpr float kCell = 3.0f;
    std::map<std::tuple<int, int, int>, GltfEmitterInfo> cells;
    const cgltf_size count = prim.indices ? prim.indices->count : pos->count;
    // Cells start at this surface's own corner, so a small one is one light wherever it sits.
    Vec3 origin{1e30f, 1e30f, 1e30f};
    for (cgltf_size v = 0; v < pos->count; ++v) {
      float tmp[3] = {0, 0, 0};
      cgltf_accessor_read_float(pos, v, tmp, 3);
      const Vec3 w = world.transformPoint({tmp[0], tmp[1], tmp[2]});
      for (int k = 0; k < 3; ++k) origin[k] = std::min(origin[k], w[k]);
    }
    for (cgltf_size t = 0; t + 2 < count; t += 3) {
      Vec3 corner[3];
      for (int k = 0; k < 3; ++k) {
        float tmp[3] = {0, 0, 0};
        cgltf_accessor_read_float(pos, prim.indices ? cgltf_accessor_read_index(prim.indices, t + cgltf_size(k)) : t + cgltf_size(k), tmp, 3);
        corner[k] = world.transformPoint({tmp[0], tmp[1], tmp[2]});
      }
      const Vec3 n = cross(corner[1] - corner[0], corner[2] - corner[0]) * 0.5f;
      const float a = length(n);
      if (a < 1e-7f) continue;
      const Vec3 c = (corner[0] + corner[1] + corner[2]) / 3.0f;
      GltfEmitterInfo& e = cells[{int((c.x - origin.x) / kCell), int((c.y - origin.y) / kCell), int((c.z - origin.z) / kCell)}];
      e.centroid += c * a;  // weighted: divided by the area below
      e.normalArea += n;
      e.area += a;
    }
    for (auto& [cell, e] : cells) {
      e.name = name;
      e.centroid = e.centroid / e.area;
      e.radiance = radiance;
      sceneInfo->emitters.push_back(std::move(e));
    }
  };

  std::function<bool(const cgltf_node*, const Mat4&)> visit = [&](const cgltf_node* node, const Mat4& parent) {
    float local[16];
    cgltf_node_transform_local(node, local);
    const Mat4 world = parent * toMat4(local);
    if (node->name) {
      const std::string_view n(node->name);
      if (n.rfind("socket_", 0) == 0 || n.rfind("hardpoint_", 0) == 0 || n.rfind("engine_", 0) == 0)
        mesh.sockets.push_back({std::string(n), world.transformPoint({0, 0, 0}), quatFromMatrix(world)});
    }
    std::string_view collide;
    const std::string extras = node->extras.data ? node->extras.data : "";
    if (const auto found = extras.find("\"fh_collide\""); found != std::string::npos) {
      if (extras.find("\"none\"", found) != std::string::npos) collide = "none";
      else if (extras.find("\"only\"", found) != std::string::npos) collide = "only";
    }
    if (sceneInfo) {
      const std::string name = node->name ? node->name : "";
      if (node->light) {
        const cgltf_light& l = *node->light;
        GltfLightInfo li;
        li.type = l.type == cgltf_light_type_directional ? GltfLightInfo::Type::Sun
                  : l.type == cgltf_light_type_spot      ? GltfLightInfo::Type::Spot
                                                         : GltfLightInfo::Type::Point;
        li.name = name;
        li.extras = extras;
        li.position = world.transformPoint({0, 0, 0});
        li.direction = normalize(world.transformDir({0, 0, -1}));
        li.color = {l.color[0], l.color[1], l.color[2]};
        li.intensity = l.intensity;
        li.range = l.range;
        sceneInfo->lights.push_back(std::move(li));
      } else if (!extras.empty()) {
        // Axis lengths in world space, so glTF's swapped up axis needs no special case.
        const Vec3 ax = world.transformDir({1, 0, 0}), ay = world.transformDir({0, 1, 0}), az = world.transformDir({0, 0, 1});
        const Vec3 scale{std::max({std::fabs(ax.x), std::fabs(ay.x), std::fabs(az.x)}),
                         std::max({std::fabs(ax.y), std::fabs(ay.y), std::fabs(az.y)}),
                         std::max({std::fabs(ax.z), std::fabs(ay.z), std::fabs(az.z)})};
        sceneInfo->nodes.push_back({name, extras, world.transformPoint({0, 0, 0}), quatFromMatrix(world), scale});
      }
    }
    if (node->mesh)
      for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p) {
        if (collide != "only" && !appendPrimitive(mesh, node->mesh->primitives[p], world)) return false;
        if (sceneInfo && collide != "only" && extras.find("\"fh_glow_only\"") == std::string::npos)
          collectEmitters(node->mesh->primitives[p], world, node->name ? node->name : "");
        if (sceneInfo && collide != "none" && !appendPrimitive(sceneInfo->collision, node->mesh->primitives[p], world))
          return false;
      }
    for (cgltf_size c = 0; c < node->children_count; ++c)
      if (!visit(node->children[c], world)) return false;
    return true;
  };

  const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count ? &data->scenes[0] : nullptr);
  if (scene) {
    for (cgltf_size i = 0; i < scene->nodes_count; ++i)
      if (!visit(scene->nodes[i], Mat4::identity())) return std::nullopt;
  } else {
    for (cgltf_size i = 0; i < data->nodes_count; ++i)
      if (!data->nodes[i].parent && !visit(&data->nodes[i], Mat4::identity())) return std::nullopt;
  }

  if (mesh.indices.empty()) {
    fail(error, path, "no triangle geometry found");
    return std::nullopt;
  }
  mesh.boundsMin = bmin;
  mesh.boundsMax = bmax;
  log::info("gltf", "loaded '{}': {} verts, {} tris, {} submeshes, {} materials, {} images, {} sockets",
            pathStr, mesh.vertices.size(), mesh.triangleCount(), mesh.submeshes.size(), mesh.materials.size(),
            mesh.images.size(), mesh.sockets.size());
  return mesh;
}

}  // namespace fh
