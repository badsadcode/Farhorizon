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
#include "engine/render/particle_atlas.h"
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
  Vec3 fadeColor{1, 1, 1};
  float fadeAmount = 0.0f;         // scene-swap fades (cloud deck)
  float heat = 0.0f;               // re-entry heat overlay
  float surfaceDetail = 0.0f;      // procedural grime and panel seams
};

struct PointLight {
  Vec3 position{};   // camera-relative
  float range = 10;  // metres (light reaches zero at range)
  Vec3 color{1, 1, 1};
};

// Far-field body (planet / moon) drawn as a ray-traced impostor behind local geometry.
struct PlanetDraw {
  Vec3 center{};           // camera-relative (m)
  float radius = 1;        // m
  Vec3 colorA{0.4f, 0.35f, 0.3f};
  float seed = 1;
  Vec3 colorB{0.15f, 0.14f, 0.13f};
  float craters = 0.5f;
  Vec3 atmosphereColor{0.5f, 0.45f, 0.4f};
  float atmosphereThickness = 0;
};

// Volumetric gas cloud, ray marched in the world.
struct CloudDraw {
  Vec3 center{};            // camera-relative (m)
  float radius = 1;         // m
  Vec3 colorA{1, 1, 1};
  float density = 1;        // optical depth per radius at full density
  Vec3 colorB{1, 1, 1};
  float seed = 1;
};

// A placed reflection probe. Everything is camera-relative, as all render data is.
struct ReflectionProbeDraw {
  Vec3 position{};
  Vec3 boxMin{}, boxMax{};
};

// Travel drive tunnel around the flight path; drawn when amount > 0.
struct TunnelDraw {
  Vec3 forward{0, 0, -1};
  float amount = 0.0f;
  Vec3 colorA{0.5f, 0.8f, 2.0f};
  Vec3 colorB{0.8f, 0.4f, 1.6f};
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
  bool drawSky = true;             // false for enclosed interiors
  bool drawStars = true;
  float sunGlare = 1.0f;           // 0 hides the star disc and glare (interior key lights)
  Vec3 skyBase{0.0016f, 0.0019f, 0.0030f};
  Vec3 skyBand{0.020f, 0.018f, 0.024f};
  float skyBandIntensity = 1.0f;
  Vec3 skyNebula{0.010f, 0.004f, 0.012f};
  float skyNebulaStrength = 1.0f;
  Vec3 skyNebulaB{0, 0, 0}, skyNebulaC{0, 0, 0};
  Vec3 fillDirection{0, 1, 0};     // unit vector toward the fill light
  Vec3 fillColor{0, 0, 0};         // linear colour * intensity
  Vec3 rimColor{0, 0, 0};
  float rimPower = 3.0f;
  std::vector<PointLight> pointLights;  // up to 8 are used (nearest first is the caller's job)
  Vec3 fogColor{0, 0, 0};
  float fogDensity = 0.0f;         // per metre; 0 = no fog
  std::vector<PlanetDraw> planets;
  // Atmospheric (planet surface) sky instead of deep space.
  bool atmosphereSky = false;
  Vec3 atmoZenith{}, atmoHorizon{}, atmoGround{};
  float atmoHaze = 1.0f;
  float starVisibility = 1.0f;
  PostProcessParams post;
  // Lighting features (Options > Graphics, and what each scene allows).
  Vec3 cameraPosition{};           // keeps shadow texels fixed in the world while the camera moves
  bool sunShadows = false;
  bool pointShadows = false;       // the nearest lamps cast shadows
  bool ambientOcclusion = false;
  float reflections = 0.0f;        // live reflection probe strength; 0 disables it
  bool liveAmbient = true;         // the live probe also supplies ambient light (not in Blender maps)
  std::vector<CloudDraw> clouds;   // drawn in order: far to near
  std::vector<RibbonVertex> ribbons;  // exhaust trails, triangle list
  TunnelDraw tunnel;
  // Placed reflection probes are baked once per generation: change it when the set changes. 0 = none.
  std::vector<ReflectionProbeDraw> reflectionProbes;
  std::uint32_t probeGeneration = 0;
  std::vector<PointLight> probeLights;  // every lamp, at full steady strength, for the bake
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
  // UI atlas for the overlay (RGBA8; null = plain white). Survives clearMeshes().
  void setUiAtlas(const std::uint8_t* rgba, std::uint32_t w, std::uint32_t h);

  // Renders and presents one frame. Items may come in any order; they are grouped per mesh and
  // drawn instanced. Returns false if the frame was skipped (swapchain
  // recreated). If `screenshotPath` is set, the presented image is also written as PNG.
  bool render(const RenderFrameParams& params, const std::vector<MeshDrawItem>& items,
              const std::vector<GlowSprite>& glows, const std::vector<ParticleSprite>& particles,
              const std::vector<OverlayVertex>& overlay,
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
    float panels = 1.0f;
    bool textured = false;   // has its own base colour: no procedural grime
    bool normalMapped = false;
  };
  struct GpuMesh {
    GpuBuffer vertices, indices;
    std::vector<SubMesh> submeshes;
    std::vector<GpuMaterial> materials;   // last entry is the default material
    std::vector<GpuImage> textures;
    std::uint64_t triangles = 0;
    float boundsRadius = 1.0f;
    Vec3 boundsCenter{};
  };
  struct FrameResources {
    GpuBuffer uniforms;
    VkDescriptorSet frameSet = VK_NULL_HANDLE;
    GpuBuffer overlayVertices;
    GpuBuffer instances;  // per-instance model matrices
    GpuBuffer glows;      // glow sprite instances
    GpuBuffer particles;  // particle sprite instances
    GpuBuffer ribbons;    // exhaust ribbon vertices
    GpuBuffer probeUniforms;  // frame uniforms for the reflection probe's view
    VkDescriptorSet probeSet = VK_NULL_HANDLE;
    bool timestampsWritten = false;
  };

  void createDescriptors();
  void createPipelines();
  VkShaderModule loadShader(const char* name);
  void ensureTargets();  // depth + HDR colour sized to the swapchain
  void createLightingResources();
  void destroyLightingResources();
  void allocateProbeArray(std::uint32_t count);
  VkDescriptorSet allocateMaterialSet(VkImageView view, VkSampler sampler = VK_NULL_HANDLE,
                                      VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  GpuImage uiAtlas_;
  VkDescriptorSet overlaySet_ = VK_NULL_HANDLE;
  void writeScreenshot(const std::filesystem::path& path, const GpuBuffer& readback, VkExtent2D extent);

  VkContext& ctx_;
  std::filesystem::path shaderDir_;
  VkDescriptorSetLayout frameLayout_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout materialLayout_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout meshMaterialLayout_ = VK_NULL_HANDLE;
  GpuImage flatNormal_;
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
  VkPipeline planetPipeline_ = VK_NULL_HANDLE;
  VkPipelineLayout planetPipelineLayout_ = VK_NULL_HANDLE;
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

  // Lighting: sun cascades, lamp cube shadows, ambient occlusion, reflection probe. ADR 0011.
  static constexpr std::uint32_t kSunShadowSize = 2048;
  static constexpr std::uint32_t kPointShadowSize = 512;
  static constexpr std::uint32_t kPointShadowLights = 4;
  static constexpr std::uint32_t kProbeSize = 128;
  static constexpr std::uint32_t kProbeMips = 8;
  static constexpr VkFormat kAoFormat = VK_FORMAT_R8_UNORM;
  GpuImage sunShadow_, pointShadow_, probe_, probeDepth_, blackCube_, aoRaw_, ao_;
  std::array<VkImageView, 3> sunLayerViews_{};
  std::array<VkImageView, kPointShadowLights * 6> pointFaceViews_{};
  std::array<VkImageView, 6> probeFaceViews_{};
  VkSampler shadowSampler_ = VK_NULL_HANDLE;
  VkSampler nearestSampler_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout lightingLayout_ = VK_NULL_HANDLE;
  VkDescriptorSet lightingSet_ = VK_NULL_HANDLE;       // main pass: probe and AO
  VkDescriptorSet probeLightingSet_ = VK_NULL_HANDLE;  // probe pass: the probe cannot read itself
  VkDescriptorSet aoDepthSet_ = VK_NULL_HANDLE, aoRawSet_ = VK_NULL_HANDLE;
  VkPipelineLayout shadowPipelineLayout_ = VK_NULL_HANDLE;
  VkPipelineLayout ssaoPipelineLayout_ = VK_NULL_HANDLE;
  VkPipeline sunShadowPipeline_ = VK_NULL_HANDLE;
  VkPipeline pointShadowPipeline_ = VK_NULL_HANDLE;
  VkPipeline depthPrepassPipeline_ = VK_NULL_HANDLE;
  VkPipeline meshProbePipeline_ = VK_NULL_HANDLE;
  VkPipeline ssaoPipeline_ = VK_NULL_HANDLE;
  VkPipeline ssaoBlurPipeline_ = VK_NULL_HANDLE;
  VkPipeline particlePipeline_ = VK_NULL_HANDLE;
  VkPipeline ribbonPipeline_ = VK_NULL_HANDLE;
  VkPipeline tunnelPipeline_ = VK_NULL_HANDLE;
  VkPipeline cloudPipeline_ = VK_NULL_HANDLE;
  VkPipeline cloudCompositePipeline_ = VK_NULL_HANDLE;
  GpuImage cloudTarget_;  // half resolution, premultiplied
  VkDescriptorSet cloudSet_ = VK_NULL_HANDLE;
  VkPipelineLayout cloudPipelineLayout_ = VK_NULL_HANDLE;
  VkPipelineLayout particlePipelineLayout_ = VK_NULL_HANDLE;
  GpuImage particleAtlas_;
  VkDescriptorSet particleAtlasSet_ = VK_NULL_HANDLE;
  static constexpr std::uint32_t kPlacedProbeSize = 512;
  static constexpr std::uint32_t kPlacedProbeMips = 10;
  GpuImage probeArray_, probeArrayDepth_;
  GpuImage blackProbeArray_;  // for probe passes: a surface being rendered cannot also be read
  std::vector<VkImageView> probeArrayFaceViews_;
  std::uint32_t probeArrayCount_ = 0;     // layers allocated, in probes
  std::uint32_t probesBaked_ = 0;
  std::uint32_t probeGeneration_ = 0;
  std::uint32_t probeFace_ = 0;
  bool probeFilled_ = false;

  std::array<FrameResources, VkContext::kFramesInFlight> frames_{};
  std::vector<GpuMesh> meshes_;
  GpuBuffer starBuffer_;
  std::uint32_t starCount_ = 0;
  RenderStats stats_;
};

}  // namespace fh
