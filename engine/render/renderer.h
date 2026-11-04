#pragma once
// Forward renderer: [HDR target] procedural sky -> starfield -> instanced PBR meshes,
// then [swapchain] exposure + tonemap -> HUD/debug overlay.
// All geometry is submitted camera-relative (the camera is at the render-space origin),
// which keeps float precision high regardless of where in the star system we are.
// Thread affinity: main thread only.
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include "engine/assets/mesh_data.h"
#include "engine/debug/overlay.h"
#include "engine/render/gpu_resources.h"
#include "engine/render/starfield.h"
#include "engine/render/vk_context.h"

namespace fh {

using MeshHandle = std::uint32_t;

// Post-processing look (data-driven, see content/visuals/post_profiles.json).
struct PostProcessParams {
  float bloomStrength = 0.0f;      // 0 disables the bloom passes
  float bloomThreshold = 1.0f;     // HDR luminance where bloom starts
  float bloomKnee = 0.5f;
  float bloomRadius = 1.0f;        // blur tap spacing in bloom texels
  int bloomIterations = 2;
  Vec3 shadowTint{1, 1, 1};
  Vec3 highlightTint{1, 1, 1};
  float saturation = 1.0f;
  float contrast = 1.0f;
  float vignette = 0.0f;
  float scanlines = 0.0f;          // 0..1 darkening between lines
  float scanlinePeriod = 3.0f;     // pixels
  float chromaticAberration = 0.0f;
  float grain = 0.0f;
};

struct RenderFrameParams {
  Mat4 viewRotation;               // world->view rotation only (no translation)
  float fovYRadians = 1.0f;
  float nearPlane = 0.05f;
  Vec3 sunDirection{0, 1, 0};      // unit vector toward the star
  Vec3 sunColor{1, 1, 1};          // linear colour * intensity
  float sunAngularRadiusRadians = 0.005f;
  Vec3 ambientSky{0.02f, 0.02f, 0.03f};
  Vec3 ambientGround{0.01f, 0.01f, 0.01f};
  float exposure = 1.0f;
  float timeSeconds = 0.0f;
  float galacticTiltRadians = 0.5f;
  Vec3 skyBase{0.0016f, 0.0019f, 0.0030f};
  Vec3 skyBand{0.020f, 0.018f, 0.024f};
  float skyBandIntensity = 1.0f;
  Vec3 skyNebula{0.010f, 0.004f, 0.012f};
  float skyNebulaStrength = 1.0f;
  Vec3 fillDirection{0, 1, 0};     // unit vector toward the fill light
  Vec3 fillColor{0, 0, 0};         // linear colour * intensity
  Vec3 rimColor{0, 0, 0};
  float rimPower = 3.0f;
  PostProcessParams post;
};

struct GlowSprite {
  Vec4 positionSize;  // camera-relative position, radius in metres
  Vec4 color;         // linear HDR colour
};
static_assert(sizeof(GlowSprite) == 32);

struct MeshDrawItem {
  MeshHandle mesh = 0;
  Mat4 model;                      // camera-relative
};

struct RenderStats {
  std::uint32_t drawCalls = 0;
  std::uint64_t triangles = 0;
  std::uint32_t stars = 0;
  std::uint32_t meshInstances = 0;  // instances submitted (after the caller's culling)
  double gpuMs = -1.0;             // most recent completed GPU frame; -1 if unsupported
};

class Renderer {
 public:
  Renderer(VkContext& ctx, std::filesystem::path shaderDir);
  ~Renderer();
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  MeshHandle uploadMesh(const MeshData& mesh);
  void clearMeshes();  // waits for the GPU; used by hot reload
  void setStarfield(const std::vector<StarInstance>& stars);

  // Renders and presents one frame. Items may come in any order; they are grouped per mesh and
  // drawn instanced. Returns false if the frame was skipped (swapchain
  // recreated). If `screenshotPath` is set, the presented image is also written as PNG.
  bool render(const RenderFrameParams& params, const std::vector<MeshDrawItem>& items,
              const std::vector<GlowSprite>& glows, const std::vector<OverlayVertex>& overlay,
              const std::filesystem::path* screenshotPath = nullptr);

  const RenderStats& stats() const { return stats_; }
  VkExtent2D extent() const { return ctx_.swapchainExtent(); }

 private:
  struct GpuMaterial {
    VkDescriptorSet set = VK_NULL_HANDLE;
    Vec4 baseColor{1, 1, 1, 1};
    Vec3 emissive{0, 0, 0};
    float metallic = 0.0f;
    float roughness = 0.5f;
  };
  struct GpuMesh {
    GpuBuffer vertices, indices;
    std::vector<SubMesh> submeshes;
    std::vector<GpuMaterial> materials;   // last entry is the default material
    std::vector<GpuImage> textures;
    std::uint64_t triangles = 0;
  };
  struct FrameResources {
    GpuBuffer uniforms;
    VkDescriptorSet frameSet = VK_NULL_HANDLE;
    GpuBuffer overlayVertices;
    GpuBuffer instances;  // per-instance model matrices
    GpuBuffer glows;      // glow sprite instances
    bool timestampsWritten = false;
  };

  void createDescriptors();
  void createPipelines();
  VkShaderModule loadShader(const char* name);
  void ensureTargets();  // depth + HDR colour sized to the swapchain
  VkDescriptorSet allocateMaterialSet(VkImageView view, VkSampler sampler = VK_NULL_HANDLE);
  void writeScreenshot(const std::filesystem::path& path, const GpuBuffer& readback, VkExtent2D extent);

  VkContext& ctx_;
  std::filesystem::path shaderDir_;
  VkDescriptorSetLayout frameLayout_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout materialLayout_ = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
  VkPipelineLayout scenePipelineLayout_ = VK_NULL_HANDLE;  // sky + stars
  VkPipelineLayout meshPipelineLayout_ = VK_NULL_HANDLE;
  VkPipelineLayout overlayPipelineLayout_ = VK_NULL_HANDLE;
  VkPipeline skyPipeline_ = VK_NULL_HANDLE;
  VkPipeline starPipeline_ = VK_NULL_HANDLE;
  VkPipeline meshPipeline_ = VK_NULL_HANDLE;
  VkPipeline overlayPipeline_ = VK_NULL_HANDLE;
  VkPipeline tonemapPipeline_ = VK_NULL_HANDLE;
  VkPipelineLayout tonemapPipelineLayout_ = VK_NULL_HANDLE;
  VkDescriptorSet tonemapSet_ = VK_NULL_HANDLE;  // samples the HDR target
  VkPipelineLayout bloomPipelineLayout_ = VK_NULL_HANDLE;
  VkPipeline bloomDownPipeline_ = VK_NULL_HANDLE;
  VkPipeline bloomBlurPipeline_ = VK_NULL_HANDLE;
  VkPipeline glowPipeline_ = VK_NULL_HANDLE;
  VkSampler clampSampler_ = VK_NULL_HANDLE;
  GpuImage bloomA_, bloomB_;  // quarter-resolution ping-pong
  VkDescriptorSet bloomSetA_ = VK_NULL_HANDLE, bloomSetB_ = VK_NULL_HANDLE;
  VkSampler sampler_ = VK_NULL_HANDLE;
  VkQueryPool queryPool_ = VK_NULL_HANDLE;
  GpuImage whiteTexture_;
  GpuImage depth_;
  GpuImage hdr_;
  static constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
  static constexpr VkFormat kHdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
  std::vector<std::uint32_t> drawOrder_;  // scratch, reused every frame
  std::vector<Mat4> instanceScratch_;

  std::array<FrameResources, VkContext::kFramesInFlight> frames_{};
  std::vector<GpuMesh> meshes_;
  GpuBuffer starBuffer_;
  std::uint32_t starCount_ = 0;
  RenderStats stats_;
};

}  // namespace fh
