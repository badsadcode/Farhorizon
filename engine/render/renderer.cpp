#include "engine/render/renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include "engine/assets/image_io.h"
#include "engine/core/file_io.h"
#include "engine/core/log.h"

namespace fh {

namespace {

struct FrameUniforms {  // must match shaders/common.glsl (std140)
  Mat4 viewProj;
  Mat4 invViewProj;
  Vec4 sunDirCos;
  Vec4 sunColorExp;
  Vec4 ambientSkyTime;
  Vec4 ambientGround;
  Vec4 viewport;
  Vec4 camRight;
  Vec4 camUp;
  Vec4 skyBase;
  Vec4 skyBand;
  Vec4 skyNebula;
  Vec4 fillDir;
  Vec4 fillColor;
  Vec4 rimColorPower;
  Vec4 fogColorDensity;
  Vec4 lightCount;
  Vec4 pointPosRange[16];
  Vec4 pointColor[16];
  Vec4 atmoZenith;
  Vec4 atmoHorizon;
  Vec4 atmoGround;
  Mat4 sunShadow[3];
  Vec4 shadowSplits;
  Vec4 shadowInfo;
  Vec4 lighting;
  Vec4 viewForward;
  Vec4 skyNebulaB;
  Vec4 skyNebulaC;
  Vec4 viewOrigin;
  Vec4 probePos[8];
  Vec4 probeMin[8];
  Vec4 probeMax[8];
};
static_assert(sizeof(FrameUniforms) == 1616);

// Vulkan cube map table: sc, tc and ma for each face, as row vectors over (x, y, z).
struct CubeFace {
  Vec3 sc, tc, ma;
};
constexpr float kPointNear = 0.15f;  // matches shaders/mesh.frag
const CubeFace kCubeFaces[6] = {{{0, 0, -1}, {0, -1, 0}, {1, 0, 0}}, {{0, 0, 1}, {0, -1, 0}, {-1, 0, 0}},
                                {{1, 0, 0}, {0, 0, 1}, {0, 1, 0}},   {{1, 0, 0}, {0, 0, -1}, {0, -1, 0}},
                                {{1, 0, 0}, {0, -1, 0}, {0, 0, 1}},  {{-1, 0, 0}, {0, -1, 0}, {0, 0, -1}}};

struct PlanetPush {  // must match planet.frag
  Vec4 centerRadius;
  Vec4 colorA;
  Vec4 colorB;
  Vec4 atmo;
};
static_assert(sizeof(PlanetPush) == 64);

struct MeshPush {  // must match mesh.frag push constants
  Vec4 baseColor;
  Vec4 emissiveMetallic;
  Vec4 params;
};
static_assert(sizeof(MeshPush) == 48);

struct PostPush {  // must match tonemap.frag
  Vec4 shadowTintSat;
  Vec4 highlightTintCon;
  Vec4 lens;
  Vec4 misc;
  Vec4 fadeColorAmount;
  Vec4 heat;
};
static_assert(sizeof(PostPush) == 96);

enum class Blend { None, Additive, Alpha, Premultiplied };

struct PipelineDesc {
  VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  std::vector<VkVertexInputBindingDescription> bindings;
  std::vector<VkVertexInputAttributeDescription> attributes;
  Blend blend = Blend::None;
  bool depthTest = false;
  bool depthWrite = false;
  VkCullModeFlags cull = VK_CULL_MODE_NONE;
  VkCompareOp depthOp = VK_COMPARE_OP_GREATER;  // reversed-Z; shadow maps use forward depth
};

VkPipeline buildPipeline(VkDevice device, const PipelineDesc& d, VkFormat colorFormat, VkFormat depthFormat) {
  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, d.vert,
               "main", nullptr};
  stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, d.frag,
               "main", nullptr};

  VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vi.vertexBindingDescriptionCount = static_cast<std::uint32_t>(d.bindings.size());
  vi.pVertexBindingDescriptions = d.bindings.data();
  vi.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(d.attributes.size());
  vi.pVertexAttributeDescriptions = d.attributes.data();

  VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vp.viewportCount = 1;
  vp.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = d.cull;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;  // projection flips Y, so glTF CCW stays CCW
  rs.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  ds.depthTestEnable = d.depthTest;
  ds.depthWriteEnable = d.depthWrite;
  ds.depthCompareOp = d.depthOp;

  VkPipelineColorBlendAttachmentState att{};
  att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                       VK_COLOR_COMPONENT_A_BIT;
  if (d.blend == Blend::Additive) {
    att.blendEnable = VK_TRUE;
    att.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    att.colorBlendOp = VK_BLEND_OP_ADD;
    att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    att.alphaBlendOp = VK_BLEND_OP_ADD;
  } else if (d.blend == Blend::Premultiplied) {
    att.blendEnable = VK_TRUE;
    att.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    att.colorBlendOp = VK_BLEND_OP_ADD;
    att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    att.alphaBlendOp = VK_BLEND_OP_ADD;
  } else if (d.blend == Blend::Alpha) {
    att.blendEnable = VK_TRUE;
    att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    att.colorBlendOp = VK_BLEND_OP_ADD;
    att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    att.alphaBlendOp = VK_BLEND_OP_ADD;
  }
  VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cb.attachmentCount = colorFormat == VK_FORMAT_UNDEFINED ? 0 : 1;
  cb.pAttachments = &att;

  const VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dy.dynamicStateCount = 2;
  dy.pDynamicStates = dyn;

  VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  rendering.colorAttachmentCount = colorFormat == VK_FORMAT_UNDEFINED ? 0 : 1;
  rendering.pColorAttachmentFormats = &colorFormat;
  rendering.depthAttachmentFormat = depthFormat;

  VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  ci.pNext = &rendering;
  ci.stageCount = d.frag ? 2 : 1;  // depth-only passes need no fragment shader
  ci.pStages = stages;
  ci.pVertexInputState = &vi;
  ci.pInputAssemblyState = &ia;
  ci.pViewportState = &vp;
  ci.pRasterizationState = &rs;
  ci.pMultisampleState = &ms;
  ci.pDepthStencilState = &ds;
  ci.pColorBlendState = &cb;
  ci.pDynamicState = &dy;
  ci.layout = d.layout;
  VkPipeline p;
  VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &p));
  return p;
}

}  // namespace

Renderer::Renderer(VkContext& ctx, std::filesystem::path shaderDir) : ctx_(ctx), shaderDir_(std::move(shaderDir)) {
  createDescriptors();
  createPipelines();

  VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  si.magFilter = VK_FILTER_LINEAR;
  si.minFilter = VK_FILTER_LINEAR;
  si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  si.maxLod = VK_LOD_CLAMP_NONE;
  si.anisotropyEnable = ctx_.anisotropySupported() ? VK_TRUE : VK_FALSE;
  si.maxAnisotropy = 8.0f;
  VK_CHECK(vkCreateSampler(ctx_.device(), &si, nullptr, &sampler_));
  si.anisotropyEnable = VK_FALSE;
  si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  VK_CHECK(vkCreateSampler(ctx_.device(), &si, nullptr, &clampSampler_));

  const std::uint8_t white[4] = {255, 255, 255, 255};
  whiteTexture_ = gpu::createTextureRGBA8(ctx_, white, 1, 1);
  const std::uint8_t flat[4] = {128, 128, 255, 255};
  flatNormal_ = gpu::createMaterialTexture(ctx_, flat, 1, 1, true);
  createLightingResources();
  {
    const ImageRGBA8 atlas = generateParticleAtlas();
    particleAtlas_ = gpu::createTextureRGBA8(ctx_, atlas.pixels.data(), std::uint32_t(atlas.width), std::uint32_t(atlas.height));
    particleAtlasSet_ = allocateMaterialSet(particleAtlas_.view, clampSampler_);
  }

  for (auto& f : frames_) {
    f.uniforms = gpu::createBuffer(ctx_, sizeof(FrameUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
    f.probeUniforms = gpu::createBuffer(ctx_, sizeof(FrameUniforms),
                                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = descriptorPool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &frameLayout_;
    VK_CHECK(vkAllocateDescriptorSets(ctx_.device(), &ai, &f.frameSet));
    VkDescriptorBufferInfo bi{f.uniforms.buffer, 0, sizeof(FrameUniforms)};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = f.frameSet;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w.pBufferInfo = &bi;
    vkUpdateDescriptorSets(ctx_.device(), 1, &w, 0, nullptr);
    VK_CHECK(vkAllocateDescriptorSets(ctx_.device(), &ai, &f.probeSet));
    VkDescriptorBufferInfo pbi{f.probeUniforms.buffer, 0, sizeof(FrameUniforms)};
    w.dstSet = f.probeSet;
    w.pBufferInfo = &pbi;
    vkUpdateDescriptorSets(ctx_.device(), 1, &w, 0, nullptr);
  }

  if (ctx_.timestampsSupported()) {
    VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = 2 * VkContext::kFramesInFlight;
    VK_CHECK(vkCreateQueryPool(ctx_.device(), &qi, nullptr, &queryPool_));
  }
  ensureTargets();
}

Renderer::~Renderer() {
  vkDeviceWaitIdle(ctx_.device());
  clearMeshes();
  gpu::destroy(ctx_, starBuffer_);
  for (auto& f : frames_) {
    gpu::destroy(ctx_, f.uniforms);
    gpu::destroy(ctx_, f.probeUniforms);
    gpu::destroy(ctx_, f.overlayVertices);
    gpu::destroy(ctx_, f.instances);
    gpu::destroy(ctx_, f.glows);
    gpu::destroy(ctx_, f.particles);
    gpu::destroy(ctx_, f.ribbons);
  }
  gpu::destroy(ctx_, hdr_);
  gpu::destroy(ctx_, bloomA_);
  gpu::destroy(ctx_, bloomB_);
  gpu::destroy(ctx_, whiteTexture_);
  gpu::destroy(ctx_, flatNormal_);
  gpu::destroy(ctx_, particleAtlas_);
  gpu::destroy(ctx_, uiAtlas_);
  gpu::destroy(ctx_, depth_);
  destroyLightingResources();
  VkDevice d = ctx_.device();
  if (queryPool_) vkDestroyQueryPool(d, queryPool_, nullptr);
  vkDestroySampler(d, sampler_, nullptr);
  vkDestroySampler(d, clampSampler_, nullptr);
  for (VkPipeline p : {skyPipeline_, starPipeline_, meshPipeline_, overlayPipeline_, tonemapPipeline_,
                       bloomDownPipeline_, bloomBlurPipeline_, glowPipeline_, planetPipeline_, sunShadowPipeline_,
                       pointShadowPipeline_, depthPrepassPipeline_, meshProbePipeline_, ssaoPipeline_, ssaoBlurPipeline_, particlePipeline_, cloudPipeline_, cloudCompositePipeline_, ribbonPipeline_, tunnelPipeline_})
    vkDestroyPipeline(d, p, nullptr);
  for (VkPipelineLayout l : {scenePipelineLayout_, meshPipelineLayout_, overlayPipelineLayout_, tonemapPipelineLayout_,
                             bloomPipelineLayout_, planetPipelineLayout_, shadowPipelineLayout_, ssaoPipelineLayout_, particlePipelineLayout_, cloudPipelineLayout_})
    vkDestroyPipelineLayout(d, l, nullptr);
  vkDestroyDescriptorPool(d, descriptorPool_, nullptr);
  vkDestroyDescriptorSetLayout(d, frameLayout_, nullptr);
  vkDestroyDescriptorSetLayout(d, materialLayout_, nullptr);
  vkDestroyDescriptorSetLayout(d, meshMaterialLayout_, nullptr);
  vkDestroyDescriptorSetLayout(d, lightingLayout_, nullptr);
}

void Renderer::createDescriptors() {
  VkDevice d = ctx_.device();
  VkDescriptorSetLayoutBinding ub{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                  VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
  VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  li.bindingCount = 1;
  li.pBindings = &ub;
  VK_CHECK(vkCreateDescriptorSetLayout(d, &li, nullptr, &frameLayout_));

  VkDescriptorSetLayoutBinding tb{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
  li.pBindings = &tb;
  VK_CHECK(vkCreateDescriptorSetLayout(d, &li, nullptr, &materialLayout_));

  VkDescriptorSetLayoutBinding mb[5]{};
  for (std::uint32_t i = 0; i < 5; ++i)
    mb[i] = {i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
  li.bindingCount = 5;
  li.pBindings = mb;
  VK_CHECK(vkCreateDescriptorSetLayout(d, &li, nullptr, &meshMaterialLayout_));

  VkDescriptorSetLayoutBinding lb[5]{};
  for (std::uint32_t i = 0; i < 5; ++i)
    lb[i] = {i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
  li.bindingCount = 5;
  li.pBindings = lb;
  VK_CHECK(vkCreateDescriptorSetLayout(d, &li, nullptr, &lightingLayout_));

  const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16},
                                        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8192}};
  VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pi.maxSets = 2048;
  pi.poolSizeCount = 2;
  pi.pPoolSizes = sizes;
  VK_CHECK(vkCreateDescriptorPool(d, &pi, nullptr, &descriptorPool_));
}

VkShaderModule Renderer::loadShader(const char* name) {
  const auto path = shaderDir_ / (std::string(name) + ".spv");
  auto code = readBinaryFile(path);
  if (!code || code->empty() || code->size() % 4 != 0)
    throw std::runtime_error("missing or invalid SPIR-V: " + path.string());
  VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  ci.codeSize = code->size();
  ci.pCode = reinterpret_cast<const std::uint32_t*>(code->data());
  VkShaderModule m;
  VK_CHECK(vkCreateShaderModule(ctx_.device(), &ci, nullptr, &m));
  return m;
}

void Renderer::createPipelines() {
  VkDevice d = ctx_.device();
  const VkFormat color = kHdrFormat;  // scene passes render to the HDR target
  const VkFormat swap = ctx_.swapchainFormat();

  VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  li.setLayoutCount = 1;
  li.pSetLayouts = &frameLayout_;
  VK_CHECK(vkCreatePipelineLayout(d, &li, nullptr, &scenePipelineLayout_));

  const VkDescriptorSetLayout meshSets[] = {frameLayout_, meshMaterialLayout_, lightingLayout_};
  VkPushConstantRange meshPush{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MeshPush)};
  li.setLayoutCount = 3;
  li.pSetLayouts = meshSets;
  li.pushConstantRangeCount = 1;
  li.pPushConstantRanges = &meshPush;
  VK_CHECK(vkCreatePipelineLayout(d, &li, nullptr, &meshPipelineLayout_));
  {
    const VkDescriptorSetLayout tonemapSets[] = {frameLayout_, materialLayout_, materialLayout_};  // frame, HDR, bloom
    VkPushConstantRange postPush{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PostPush)};
    VkPipelineLayoutCreateInfo tl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    tl.setLayoutCount = 3;
    tl.pSetLayouts = tonemapSets;
    tl.pushConstantRangeCount = 1;
    tl.pPushConstantRanges = &postPush;
    VK_CHECK(vkCreatePipelineLayout(d, &tl, nullptr, &tonemapPipelineLayout_));
    VkPushConstantRange planetPush{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PlanetPush)};
    VkPipelineLayoutCreateInfo pll{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pll.setLayoutCount = 1;
    pll.pSetLayouts = &frameLayout_;
    pll.pushConstantRangeCount = 1;
    pll.pPushConstantRanges = &planetPush;
    VK_CHECK(vkCreatePipelineLayout(d, &pll, nullptr, &planetPipelineLayout_));
    VkPushConstantRange bloomPush{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Vec4)};
    VkPipelineLayoutCreateInfo bl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    bl.setLayoutCount = 1;
    bl.pSetLayouts = &materialLayout_;
    bl.pushConstantRangeCount = 1;
    bl.pPushConstantRanges = &bloomPush;
    VK_CHECK(vkCreatePipelineLayout(d, &bl, nullptr, &bloomPipelineLayout_));
    const VkDescriptorSetLayout ssaoSets[] = {frameLayout_, materialLayout_};
    VkPipelineLayoutCreateInfo sl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    sl.setLayoutCount = 2;
    sl.pSetLayouts = ssaoSets;
    sl.pushConstantRangeCount = 1;
    sl.pPushConstantRanges = &bloomPush;
    VK_CHECK(vkCreatePipelineLayout(d, &sl, nullptr, &ssaoPipelineLayout_));
    const VkDescriptorSetLayout particleSets[] = {frameLayout_, materialLayout_, materialLayout_};  // frame, atlas, depth
    VkPipelineLayoutCreateInfo pal{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pal.setLayoutCount = 3;
    pal.pSetLayouts = particleSets;
    pal.pushConstantRangeCount = 1;
    pal.pPushConstantRanges = &bloomPush;
    VK_CHECK(vkCreatePipelineLayout(d, &pal, nullptr, &particlePipelineLayout_));
    const VkDescriptorSetLayout cloudSets[] = {frameLayout_, materialLayout_};  // frame, depth
    VkPushConstantRange cloudPush{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Vec4) * 4};
    VkPipelineLayoutCreateInfo cll{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    cll.setLayoutCount = 2;
    cll.pSetLayouts = cloudSets;
    cll.pushConstantRangeCount = 1;
    cll.pPushConstantRanges = &cloudPush;
    VK_CHECK(vkCreatePipelineLayout(d, &cll, nullptr, &cloudPipelineLayout_));
    VkPushConstantRange shadowPush{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4)};
    VkPipelineLayoutCreateInfo shl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    shl.pushConstantRangeCount = 1;
    shl.pPushConstantRanges = &shadowPush;
    VK_CHECK(vkCreatePipelineLayout(d, &shl, nullptr, &shadowPipelineLayout_));
  }

  VkPushConstantRange overlayPush{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 2};
  li.setLayoutCount = 1;
  li.pSetLayouts = &materialLayout_;  // the UI atlas (font glyphs + white texel)
  li.pPushConstantRanges = &overlayPush;
  VK_CHECK(vkCreatePipelineLayout(d, &li, nullptr, &overlayPipelineLayout_));

  struct Modules {
    VkDevice d;
    std::vector<VkShaderModule> list;
    ~Modules() {
      for (auto m : list) vkDestroyShaderModule(d, m, nullptr);
    }
  } modules{d, {}};
  auto load = [&](const char* n) {
    modules.list.push_back(loadShader(n));
    return modules.list.back();
  };

  PipelineDesc sky;
  sky.vert = load("sky.vert");
  sky.frag = load("sky.frag");
  sky.layout = scenePipelineLayout_;
  skyPipeline_ = buildPipeline(d, sky, color, kDepthFormat);

  PipelineDesc planet;
  planet.vert = sky.vert;
  planet.frag = load("planet.frag");
  planet.layout = planetPipelineLayout_;
  planet.blend = Blend::Premultiplied;
  planetPipeline_ = buildPipeline(d, planet, color, kDepthFormat);

  PipelineDesc cloud;
  cloud.vert = sky.vert;
  cloud.frag = load("cloud.frag");
  cloud.layout = cloudPipelineLayout_;
  cloud.blend = Blend::Premultiplied;
  cloudPipeline_ = buildPipeline(d, cloud, color, VK_FORMAT_UNDEFINED);

  PipelineDesc stars;
  stars.vert = load("stars.vert");
  stars.frag = load("stars.frag");
  stars.layout = scenePipelineLayout_;
  stars.blend = Blend::Additive;
  stars.bindings = {{0, sizeof(StarInstance), VK_VERTEX_INPUT_RATE_INSTANCE}};
  stars.attributes = {{0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16}};
  starPipeline_ = buildPipeline(d, stars, color, kDepthFormat);

  PipelineDesc glow;
  glow.vert = load("glow.vert");
  glow.frag = load("glow.frag");
  glow.layout = scenePipelineLayout_;
  glow.blend = Blend::Additive;
  glow.depthTest = true;  // occluded by hulls, but never writes depth
  glow.bindings = {{0, sizeof(GlowSprite), VK_VERTEX_INPUT_RATE_INSTANCE}};
  glow.attributes = {{0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16}};
  glowPipeline_ = buildPipeline(d, glow, color, kDepthFormat);

  PipelineDesc particle;
  particle.vert = load("particle.vert");
  particle.frag = load("particle.frag");
  particle.layout = particlePipelineLayout_;
  particle.blend = Blend::Premultiplied;
  particle.depthTest = true;  // occluded by hulls, but never writes depth
  particle.bindings = {{0, sizeof(ParticleSprite), VK_VERTEX_INPUT_RATE_INSTANCE}};
  particle.attributes = {{0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
                         {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16},
                         {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 32},
                         {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 48}};
  particlePipeline_ = buildPipeline(d, particle, color, kDepthFormat);

  PipelineDesc ribbon;
  ribbon.vert = load("ribbon.vert");
  ribbon.frag = load("ribbon.frag");
  ribbon.layout = particlePipelineLayout_;
  ribbon.blend = Blend::Premultiplied;
  ribbon.depthTest = true;
  ribbon.bindings = {{0, sizeof(RibbonVertex), VK_VERTEX_INPUT_RATE_VERTEX}};
  ribbon.attributes = {{0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
                       {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16},
                       {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 32}};
  ribbonPipeline_ = buildPipeline(d, ribbon, color, kDepthFormat);

  PipelineDesc mesh;
  mesh.vert = load("mesh.vert");
  mesh.frag = load("mesh.frag");
  mesh.layout = meshPipelineLayout_;
  mesh.depthTest = true;
  mesh.depthWrite = false;                     // the depth prepass already wrote it
  mesh.depthOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
  mesh.cull = VK_CULL_MODE_BACK_BIT;
  mesh.bindings = {{0, sizeof(MeshVertex), VK_VERTEX_INPUT_RATE_VERTEX},
                   {1, sizeof(Mat4), VK_VERTEX_INPUT_RATE_INSTANCE}};
  mesh.attributes = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, position)},
                     {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, normal)},
                     {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(MeshVertex, uv)},
                     {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
                     {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 16},
                     {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 32},
                     {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 48}};
  meshPipeline_ = buildPipeline(d, mesh, color, kDepthFormat);

  PipelineDesc prepass = mesh;
  prepass.frag = VK_NULL_HANDLE;
  prepass.depthWrite = true;
  prepass.depthOp = VK_COMPARE_OP_GREATER;
  depthPrepassPipeline_ = buildPipeline(d, prepass, VK_FORMAT_UNDEFINED, kDepthFormat);

  // Cube faces are left-handed, so the probe sees every triangle with reversed winding.
  PipelineDesc meshProbe = mesh;
  meshProbe.depthWrite = true;
  meshProbe.depthOp = VK_COMPARE_OP_GREATER;
  meshProbe.cull = VK_CULL_MODE_FRONT_BIT;
  meshProbePipeline_ = buildPipeline(d, meshProbe, color, kDepthFormat);

  PipelineDesc shadow;
  shadow.vert = load("shadow_sun.vert");
  shadow.layout = shadowPipelineLayout_;
  shadow.depthTest = shadow.depthWrite = true;
  shadow.depthOp = VK_COMPARE_OP_LESS;
  shadow.cull = VK_CULL_MODE_NONE;             // thin placeholder geometry must still cast
  shadow.bindings = mesh.bindings;
  shadow.attributes = mesh.attributes;
  sunShadowPipeline_ = buildPipeline(d, shadow, VK_FORMAT_UNDEFINED, kDepthFormat);
  shadow.vert = load("shadow_point.vert");
  pointShadowPipeline_ = buildPipeline(d, shadow, VK_FORMAT_UNDEFINED, kDepthFormat);

  PipelineDesc overlay;
  overlay.vert = load("overlay.vert");
  overlay.frag = load("overlay.frag");
  overlay.layout = overlayPipelineLayout_;
  overlay.blend = Blend::Alpha;
  overlay.bindings = {{0, sizeof(OverlayVertex), VK_VERTEX_INPUT_RATE_VERTEX}};
  overlay.attributes = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32_SFLOAT, 8},
                        {2, 0, VK_FORMAT_R8G8B8A8_UNORM, 16}};
  overlayPipeline_ = buildPipeline(d, overlay, swap, VK_FORMAT_UNDEFINED);

  PipelineDesc tonemap;
  tonemap.vert = load("tonemap.vert");
  tonemap.frag = load("tonemap.frag");
  tonemap.layout = tonemapPipelineLayout_;
  tonemapPipeline_ = buildPipeline(d, tonemap, swap, VK_FORMAT_UNDEFINED);

  PipelineDesc tunnelDesc;
  tunnelDesc.vert = sky.vert;
  tunnelDesc.frag = load("tunnel.frag");
  tunnelDesc.layout = cloudPipelineLayout_;
  tunnelDesc.blend = Blend::Additive;
  tunnelPipeline_ = buildPipeline(d, tunnelDesc, color, kDepthFormat);

  PipelineDesc cloudComposite;
  cloudComposite.vert = tonemap.vert;
  cloudComposite.frag = load("cloud_composite.frag");
  cloudComposite.layout = bloomPipelineLayout_;
  cloudComposite.blend = Blend::Premultiplied;
  cloudCompositePipeline_ = buildPipeline(d, cloudComposite, color, kDepthFormat);

  PipelineDesc bloomDown;
  bloomDown.vert = tonemap.vert;
  bloomDown.frag = load("bloom_down.frag");
  bloomDown.layout = bloomPipelineLayout_;
  bloomDownPipeline_ = buildPipeline(d, bloomDown, kHdrFormat, VK_FORMAT_UNDEFINED);
  PipelineDesc bloomBlur = bloomDown;
  bloomBlur.frag = load("bloom_blur.frag");
  bloomBlurPipeline_ = buildPipeline(d, bloomBlur, kHdrFormat, VK_FORMAT_UNDEFINED);
  PipelineDesc ssao;
  ssao.vert = tonemap.vert;
  ssao.frag = load("ssao.frag");
  ssao.layout = ssaoPipelineLayout_;
  ssaoPipeline_ = buildPipeline(d, ssao, kAoFormat, VK_FORMAT_UNDEFINED);
  PipelineDesc ssaoBlur;
  ssaoBlur.vert = tonemap.vert;
  ssaoBlur.frag = load("ssao_blur.frag");
  ssaoBlur.layout = bloomPipelineLayout_;
  ssaoBlurPipeline_ = buildPipeline(d, ssaoBlur, kAoFormat, VK_FORMAT_UNDEFINED);
  log::info("render", "pipelines created (sky, stars, mesh, bloom, tonemap, overlay)");
}

VkExtent2D Renderer::sceneExtent() const {
  const VkExtent2D w = ctx_.swapchainExtent();
  return {std::max(1u, std::uint32_t(std::lround(float(w.width) * renderScale_))),
          std::max(1u, std::uint32_t(std::lround(float(w.height) * renderScale_)))};
}

void Renderer::setRenderScale(float scale) { renderScale_ = std::clamp(scale, 0.25f, 1.0f); }

void Renderer::ensureTargets() {
  const VkExtent2D e = sceneExtent();  // the 3D scene renders here; tonemap scales it to the window
  if (depth_.image && depth_.extent.width == e.width && depth_.extent.height == e.height) return;
  vkDeviceWaitIdle(ctx_.device());
  gpu::destroy(ctx_, depth_);
  gpu::destroy(ctx_, hdr_);
  depth_ = gpu::createImage(ctx_, e, kDepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_IMAGE_ASPECT_DEPTH_BIT);
  hdr_ = gpu::createImage(ctx_, e, kHdrFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          VK_IMAGE_ASPECT_COLOR_BIT);
  gpu::destroy(ctx_, bloomA_);
  gpu::destroy(ctx_, bloomB_);
  const VkExtent2D be{std::max(1u, e.width / 4), std::max(1u, e.height / 4)};
  bloomA_ = gpu::createImage(ctx_, be, kHdrFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                             VK_IMAGE_ASPECT_COLOR_BIT);
  bloomB_ = gpu::createImage(ctx_, be, kHdrFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                             VK_IMAGE_ASPECT_COLOR_BIT);
  for (VkDescriptorSet* set : {&tonemapSet_, &bloomSetA_, &bloomSetB_})
    if (*set) vkFreeDescriptorSets(ctx_.device(), descriptorPool_, 1, set);
  tonemapSet_ = allocateMaterialSet(hdr_.view, clampSampler_);  // same layout: one combined image sampler
  bloomSetA_ = allocateMaterialSet(bloomA_.view, clampSampler_);
  bloomSetB_ = allocateMaterialSet(bloomB_.view, clampSampler_);

  // Ambient occlusion targets, half resolution.
  gpu::destroy(ctx_, aoRaw_);
  gpu::destroy(ctx_, ao_);
  gpu::destroy(ctx_, cloudTarget_);
  const VkExtent2D ae{std::max(1u, e.width / 2), std::max(1u, e.height / 2)};
  aoRaw_ = gpu::createImage(ctx_, ae, kAoFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_IMAGE_ASPECT_COLOR_BIT);
  ao_ = gpu::createImage(ctx_, ae, kAoFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT);
  cloudTarget_ = gpu::createImage(ctx_, ae, kHdrFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                  VK_IMAGE_ASPECT_COLOR_BIT);
  ctx_.immediateSubmit([&](VkCommandBuffer cmd) {
    for (GpuImage* img : {&aoRaw_, &ao_, &cloudTarget_})
      gpu::imageBarrier(cmd, img->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
  });
  for (VkDescriptorSet* set : {&aoDepthSet_, &aoRawSet_, &cloudSet_})
    if (*set) vkFreeDescriptorSets(ctx_.device(), descriptorPool_, 1, set);
  aoDepthSet_ = allocateMaterialSet(depth_.view, nearestSampler_, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
  aoRawSet_ = allocateMaterialSet(aoRaw_.view, clampSampler_);
  cloudSet_ = allocateMaterialSet(cloudTarget_.view, clampSampler_);
  VkDescriptorImageInfo aoInfo{clampSampler_, ao_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet aoWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  aoWrite.dstSet = lightingSet_;
  aoWrite.dstBinding = 2;
  aoWrite.descriptorCount = 1;
  aoWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  aoWrite.pImageInfo = &aoInfo;
  vkUpdateDescriptorSets(ctx_.device(), 1, &aoWrite, 0, nullptr);
}

void Renderer::createLightingResources() {
  VkDevice d = ctx_.device();
  VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  si.magFilter = si.minFilter = VK_FILTER_LINEAR;
  si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  si.compareEnable = VK_TRUE;
  si.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  si.maxLod = 0.0f;
  VK_CHECK(vkCreateSampler(d, &si, nullptr, &shadowSampler_));
  si.compareEnable = VK_FALSE;
  si.magFilter = si.minFilter = VK_FILTER_NEAREST;
  VK_CHECK(vkCreateSampler(d, &si, nullptr, &nearestSampler_));

  gpu::ImageDesc sun;
  sun.extent = {kSunShadowSize, kSunShadowSize};
  sun.format = kDepthFormat;
  sun.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  sun.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
  sun.layers = 3;
  sun.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
  sunShadow_ = gpu::createImage(ctx_, sun);
  for (std::uint32_t i = 0; i < sunLayerViews_.size(); ++i)
    sunLayerViews_[i] = gpu::createSubView(ctx_, sunShadow_, VK_IMAGE_ASPECT_DEPTH_BIT, 0, i);

  gpu::ImageDesc point = sun;
  point.extent = {kPointShadowSize, kPointShadowSize};
  point.layers = kPointShadowLights * 6;
  point.cubeCompatible = true;
  point.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
  pointShadow_ = gpu::createImage(ctx_, point);
  for (std::uint32_t i = 0; i < pointFaceViews_.size(); ++i)
    pointFaceViews_[i] = gpu::createSubView(ctx_, pointShadow_, VK_IMAGE_ASPECT_DEPTH_BIT, 0, i);

  gpu::ImageDesc probe;
  probe.extent = {kProbeSize, kProbeSize};
  probe.format = kHdrFormat;
  probe.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  probe.mips = kProbeMips;
  probe.layers = 6;
  probe.cubeCompatible = true;
  probe.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
  probe_ = gpu::createImage(ctx_, probe);
  for (std::uint32_t i = 0; i < 6; ++i)
    probeFaceViews_[i] = gpu::createSubView(ctx_, probe_, VK_IMAGE_ASPECT_COLOR_BIT, 0, i);
  probeDepth_ = gpu::createImage(ctx_, {kProbeSize, kProbeSize}, kDepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                 VK_IMAGE_ASPECT_DEPTH_BIT);

  gpu::ImageDesc black = probe;
  black.extent = {1, 1};
  black.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  black.mips = 1;
  blackCube_ = gpu::createImage(ctx_, black);

  ctx_.immediateSubmit([&](VkCommandBuffer cmd) {
    const VkPipelineStageFlags2 frag = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    const VkAccessFlags2 read = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    const VkImageLayout ro = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    gpu::imageBarrierRange(cmd, sunShadow_.image, VK_IMAGE_LAYOUT_UNDEFINED, ro, VK_PIPELINE_STAGE_2_NONE, 0, frag, read,
                           VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 3);
    gpu::imageBarrierRange(cmd, pointShadow_.image, VK_IMAGE_LAYOUT_UNDEFINED, ro, VK_PIPELINE_STAGE_2_NONE, 0, frag, read,
                           VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, kPointShadowLights * 6);
    gpu::imageBarrierRange(cmd, probe_.image, VK_IMAGE_LAYOUT_UNDEFINED, ro, VK_PIPELINE_STAGE_2_NONE, 0, frag, read,
                           VK_IMAGE_ASPECT_COLOR_BIT, 0, kProbeMips, 0, 6);
    gpu::imageBarrierRange(cmd, blackCube_.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                           VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6);
    const VkClearColorValue zero{{0, 0, 0, 0}};
    const VkImageSubresourceRange all{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6};
    vkCmdClearColorImage(cmd, blackCube_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &all);
    gpu::imageBarrierRange(cmd, blackCube_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, ro, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                           VK_ACCESS_2_TRANSFER_WRITE_BIT, frag, read, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6);
  });

  allocateProbeArray(0);  // one black probe, so the sets are always complete
  blackProbeArray_ = probeArray_;
  probeArray_ = {};
  allocateProbeArray(0);
  auto allocate = [&](VkImageView env, VkImageView ao, VkImageView placed) {
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = descriptorPool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &lightingLayout_;
    VkDescriptorSet set;
    VK_CHECK(vkAllocateDescriptorSets(d, &ai, &set));
    const VkImageLayout ro = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const VkDescriptorImageInfo infos[5] = {{shadowSampler_, sunShadow_.view, ro},
                                            {shadowSampler_, pointShadow_.view, ro},
                                            {clampSampler_, ao, ro},
                                            {clampSampler_, env, ro},
                                            {clampSampler_, placed, ro}};
    VkWriteDescriptorSet writes[5]{};
    for (std::uint32_t i = 0; i < 5; ++i) {
      writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      writes[i].dstSet = set;
      writes[i].dstBinding = i;
      writes[i].descriptorCount = 1;
      writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      writes[i].pImageInfo = &infos[i];
    }
    vkUpdateDescriptorSets(d, 5, writes, 0, nullptr);
    return set;
  };
  lightingSet_ = allocate(probe_.view, whiteTexture_.view, probeArray_.view);  // ensureTargets points AO at its target
  probeLightingSet_ = allocate(blackCube_.view, whiteTexture_.view, blackProbeArray_.view);
}

void Renderer::allocateProbeArray(std::uint32_t count) {
  VkDevice d = ctx_.device();
  for (VkImageView v : probeArrayFaceViews_) vkDestroyImageView(d, v, nullptr);
  probeArrayFaceViews_.clear();
  gpu::destroy(ctx_, probeArray_);
  gpu::destroy(ctx_, probeArrayDepth_);
  probeArrayCount_ = count;
  gpu::ImageDesc desc;
  desc.format = kHdrFormat;
  desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
               VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  desc.cubeCompatible = true;
  desc.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
  desc.layers = std::max(1u, count) * 6;
  desc.extent = count ? VkExtent2D{kPlacedProbeSize, kPlacedProbeSize} : VkExtent2D{1, 1};
  desc.mips = count ? kPlacedProbeMips : 1;
  probeArray_ = gpu::createImage(ctx_, desc);
  ctx_.immediateSubmit([&](VkCommandBuffer cmd) {
    gpu::imageBarrierRange(cmd, probeArray_.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                           VK_IMAGE_ASPECT_COLOR_BIT, 0, desc.mips, 0, desc.layers);
    const VkClearColorValue zero{{0, 0, 0, 0}};
    const VkImageSubresourceRange all{VK_IMAGE_ASPECT_COLOR_BIT, 0, desc.mips, 0, desc.layers};
    vkCmdClearColorImage(cmd, probeArray_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &all);
    gpu::imageBarrierRange(cmd, probeArray_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                           VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 0, desc.mips, 0, desc.layers);
  });
  if (count > 0) {
    for (std::uint32_t i = 0; i < count * 6; ++i)
      probeArrayFaceViews_.push_back(gpu::createSubView(ctx_, probeArray_, VK_IMAGE_ASPECT_COLOR_BIT, 0, i));
    probeArrayDepth_ = gpu::createImage(ctx_, {kPlacedProbeSize, kPlacedProbeSize}, kDepthFormat,
                                        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
  }
  if (lightingSet_) {  // not on the first call, before the set exists
    VkDescriptorImageInfo info{clampSampler_, probeArray_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = lightingSet_;
    w.dstBinding = 4;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &info;
    vkUpdateDescriptorSets(d, 1, &w, 0, nullptr);
  }
}

void Renderer::destroyLightingResources() {
  VkDevice d = ctx_.device();
  for (VkImageView v : probeArrayFaceViews_) vkDestroyImageView(d, v, nullptr);
  gpu::destroy(ctx_, probeArray_);
  gpu::destroy(ctx_, probeArrayDepth_);
  gpu::destroy(ctx_, blackProbeArray_);
  for (VkImageView v : sunLayerViews_) vkDestroyImageView(d, v, nullptr);
  for (VkImageView v : pointFaceViews_) vkDestroyImageView(d, v, nullptr);
  for (VkImageView v : probeFaceViews_) vkDestroyImageView(d, v, nullptr);
  for (GpuImage* img : {&sunShadow_, &pointShadow_, &probe_, &probeDepth_, &blackCube_, &aoRaw_, &ao_, &cloudTarget_})
    gpu::destroy(ctx_, *img);
  vkDestroySampler(d, shadowSampler_, nullptr);
  vkDestroySampler(d, nearestSampler_, nullptr);
}

VkDescriptorSet Renderer::allocateMaterialSet(VkImageView view, VkSampler sampler, VkImageLayout layout) {
  VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  ai.descriptorPool = descriptorPool_;
  ai.descriptorSetCount = 1;
  ai.pSetLayouts = &materialLayout_;
  VkDescriptorSet set;
  VK_CHECK(vkAllocateDescriptorSets(ctx_.device(), &ai, &set));
  VkDescriptorImageInfo ii{sampler ? sampler : sampler_, view, layout};
  VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  w.dstSet = set;
  w.descriptorCount = 1;
  w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  w.pImageInfo = &ii;
  vkUpdateDescriptorSets(ctx_.device(), 1, &w, 0, nullptr);
  return set;
}

MeshHandle Renderer::uploadMesh(const MeshData& mesh) {
  GpuMesh g;
  g.vertices = gpu::createBufferWithData(ctx_, mesh.vertices.data(), mesh.vertices.size() * sizeof(MeshVertex),
                                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
  g.indices = gpu::createBufferWithData(ctx_, mesh.indices.data(), mesh.indices.size() * sizeof(std::uint32_t),
                                        VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
  for (const auto& img : mesh.images)
    g.textures.push_back(gpu::createMaterialTexture(ctx_, img.pixels.data(), std::uint32_t(img.width),
                                                    std::uint32_t(img.height), img.linear));
  auto materialSet = [&](const MaterialData* m) {
    auto view = [&](int image, VkImageView fallback) {
      return image >= 0 && size_t(image) < g.textures.size() ? g.textures[size_t(image)].view : fallback;
    };
    const VkImageView white = whiteTexture_.view;
    const VkImageView views[5] = {m ? view(m->baseColorImage, white) : white, m ? view(m->normalImage, flatNormal_.view) : flatNormal_.view,
                                  m ? view(m->metalRoughImage, white) : white, m ? view(m->emissiveImage, white) : white,
                                  m ? view(m->occlusionImage, white) : white};
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = descriptorPool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &meshMaterialLayout_;
    VkDescriptorSet set;
    VK_CHECK(vkAllocateDescriptorSets(ctx_.device(), &ai, &set));
    VkDescriptorImageInfo infos[5];
    VkWriteDescriptorSet writes[5]{};
    for (std::uint32_t i = 0; i < 5; ++i) {
      infos[i] = {sampler_, views[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
      writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      writes[i].dstSet = set;
      writes[i].dstBinding = i;
      writes[i].descriptorCount = 1;
      writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      writes[i].pImageInfo = &infos[i];
    }
    vkUpdateDescriptorSets(ctx_.device(), 5, writes, 0, nullptr);
    return set;
  };
  for (const auto& m : mesh.materials) {
    GpuMaterial gm;
    gm.set = materialSet(&m);
    gm.baseColor = m.baseColorFactor;
    gm.emissive = m.emissive;
    gm.metallic = m.metallic;
    gm.roughness = m.roughness;
    gm.panels = m.panels;
    gm.textured = m.baseColorImage >= 0 || !m.proceduralDetail;
    gm.normalMapped = m.normalImage >= 0;
    g.materials.push_back(gm);
  }
  GpuMaterial fallback;  // default material for primitives without one
  fallback.set = materialSet(nullptr);
  fallback.baseColor = {0.8f, 0.8f, 0.8f, 1.0f};
  g.materials.push_back(fallback);
  g.submeshes = mesh.submeshes;
  g.triangles = mesh.triangleCount();
  g.boundsRadius = mesh.boundsRadius();
  g.boundsCenter = mesh.boundsCenter();
  meshes_.push_back(std::move(g));
  return static_cast<MeshHandle>(meshes_.size() - 1);
}

void Renderer::setUiAtlas(const std::uint8_t* rgba, std::uint32_t w, std::uint32_t h) {
  vkDeviceWaitIdle(ctx_.device());
  if (overlaySet_) vkFreeDescriptorSets(ctx_.device(), descriptorPool_, 1, &overlaySet_);
  overlaySet_ = VK_NULL_HANDLE;
  gpu::destroy(ctx_, uiAtlas_);
  if (!rgba) return;
  uiAtlas_ = gpu::createTextureRGBA8(ctx_, rgba, w, h);
  overlaySet_ = allocateMaterialSet(uiAtlas_.view, clampSampler_);
}

void Renderer::clearMeshes() {
  vkDeviceWaitIdle(ctx_.device());
  for (auto& m : meshes_) {
    gpu::destroy(ctx_, m.vertices);
    gpu::destroy(ctx_, m.indices);
    for (auto& t : m.textures) gpu::destroy(ctx_, t);
    for (auto& mat : m.materials) vkFreeDescriptorSets(ctx_.device(), descriptorPool_, 1, &mat.set);
  }
  meshes_.clear();
}

void Renderer::setStarfield(const std::vector<StarInstance>& stars) {
  vkDeviceWaitIdle(ctx_.device());
  gpu::destroy(ctx_, starBuffer_);
  starCount_ = static_cast<std::uint32_t>(stars.size());
  if (!stars.empty())
    starBuffer_ = gpu::createBufferWithData(ctx_, stars.data(), stars.size() * sizeof(StarInstance),
                                            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

bool Renderer::render(const RenderFrameParams& p, const std::vector<MeshDrawItem>& items,
                      const std::vector<GlowSprite>& glows, const std::vector<ParticleSprite>& particles,
                      const std::vector<OverlayVertex>& overlay,
                      const std::filesystem::path* screenshotPath) {
  if (p.probeGeneration != probeGeneration_) {  // a different set of placed probes: start again
    vkDeviceWaitIdle(ctx_.device());
    probeGeneration_ = p.probeGeneration;
    probesBaked_ = 0;
    allocateProbeArray(std::uint32_t(std::min<size_t>(8, p.reflectionProbes.size())));
  }
  std::uint32_t imageIndex = 0;
  if (!ctx_.beginFrame(imageIndex)) return false;
  ensureTargets();
  const std::uint32_t fi = ctx_.frameIndex();
  FrameResources& fr = frames_[fi];
  const VkExtent2D swapExtent = ctx_.swapchainExtent();  // window: tonemap, HUD, screenshots
  const VkExtent2D extent = sceneExtent();               // 3D scene (render scale)

  // GPU time of the previous use of this frame slot (its fence has been waited on).
  if (queryPool_ && fr.timestampsWritten) {
    std::uint64_t ts[2] = {0, 0};
    if (vkGetQueryPoolResults(ctx_.device(), queryPool_, fi * 2, 2, sizeof(ts), ts, sizeof(std::uint64_t),
                              VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
      stats_.gpuMs = double(ts[1] - ts[0]) * double(ctx_.timestampPeriodNs()) * 1e-6;
  }

  // ---- Per-frame uniforms ----
  const float aspect = float(extent.width) / float(std::max(1u, extent.height));
  const Mat4 proj = Mat4::perspectiveReversedZInfinite(p.fovYRadians, aspect, p.nearPlane);
  FrameUniforms u;
  u.viewProj = proj * p.viewRotation;
  u.invViewProj = u.viewProj.inverse();
  u.sunDirCos = Vec4(normalize(p.sunDirection), std::cos(p.sunAngularRadiusRadians));
  u.sunColorExp = Vec4(p.sunColor, p.exposure);
  u.ambientSkyTime = Vec4(p.ambientSky, p.timeSeconds);
  u.ambientGround = Vec4(p.ambientGround, p.galacticTiltRadians);
  u.viewport = Vec4(float(extent.width), float(extent.height), 1.0f / float(extent.width), 1.0f / float(extent.height));
  // Rows of the rotation-only view matrix are the camera axes in render space.
  u.camRight = Vec4(p.viewRotation.at(0, 0), p.viewRotation.at(0, 1), p.viewRotation.at(0, 2), 0);
  u.camUp = Vec4(p.viewRotation.at(1, 0), p.viewRotation.at(1, 1), p.viewRotation.at(1, 2), 0);
  u.skyBase = Vec4(p.skyBase, p.sunGlare);
  u.skyBand = Vec4(p.skyBand, p.skyBandIntensity);
  u.skyNebula = Vec4(p.skyNebula, p.skyNebulaStrength);
  u.skyNebulaB = Vec4(p.skyNebulaB, p.liveAmbient ? 1.0f : 0.0f);
  u.skyNebulaC = Vec4(p.skyNebulaC, 0);
  u.fillDir = Vec4(normalize(p.fillDirection), 0);
  u.fillColor = Vec4(p.fillColor, 0);
  u.rimColorPower = Vec4(p.rimColor, p.rimPower);
  u.fogColorDensity = Vec4(p.fogColor, p.fogDensity);
  u.atmoZenith = Vec4(p.atmoZenith, p.atmosphereSky ? 1.0f : 0.0f);
  u.atmoHorizon = Vec4(p.atmoHorizon, std::max(0.1f, p.atmoHaze));
  u.atmoGround = Vec4(p.atmoGround, p.starVisibility);
  const size_t nLights = std::min<size_t>({kPointShadowLights, size_t(std::max(0, p.maxPointLights)), p.pointLights.size()});
  u.lightCount = Vec4(float(nLights), 0, 0, 0);
  for (size_t i = 0; i < nLights; ++i) {
    u.pointPosRange[i] = Vec4(p.pointLights[i].position, p.pointLights[i].range);
    u.pointColor[i] = Vec4(p.pointLights[i].color, 0);
  }
  // Rows of the rotation-only view: the camera looks down -Z.
  const Vec3 forward{-p.viewRotation.at(2, 0), -p.viewRotation.at(2, 1), -p.viewRotation.at(2, 2)};
  u.viewForward = Vec4(forward, 0);
  // Only finished snapshots are used, so a probe never shows black while its turn is coming.
  const std::uint32_t probesUsable = std::min<std::uint32_t>(probesBaked_, std::uint32_t(p.reflectionProbes.size()));
  u.viewOrigin = Vec4(0, 0, 0, p.reflections > 0.0f ? float(probesUsable) : 0.0f);
  for (std::uint32_t i = 0; i < 8; ++i) {
    const bool used = i < p.reflectionProbes.size();
    u.probePos[i] = used ? Vec4(p.reflectionProbes[i].position, 0) : Vec4(0, 0, 0, 0);
    u.probeMin[i] = used ? Vec4(p.reflectionProbes[i].boxMin, 0) : Vec4(0, 0, 0, 0);
    u.probeMax[i] = used ? Vec4(p.reflectionProbes[i].boxMax, 0) : Vec4(0, 0, 0, 0);
  }
  u.lighting = Vec4(p.ambientOcclusion ? 1.0f : 0.0f, p.reflections, p.post.surfaceDetail, float(kProbeMips));
  const std::uint32_t shadowedLights =
      p.pointShadows ? std::min<std::uint32_t>(kPointShadowLights, std::uint32_t(nLights)) : 0;
  u.shadowInfo = Vec4(0, 0, 0, float(shadowedLights));
  u.shadowSplits = Vec4(0, 0, 0, 0);
  {
    // Sun cascades: a bounding sphere per slice of the view frustum keeps the size constant as the
    // camera turns, and snapping to whole texels keeps shadow edges still as it moves.
    const float splits[3] = {40.0f, 220.0f, 1400.0f};
    const float tanY = std::tan(p.fovYRadians * 0.5f), tanX = tanY * aspect, k2 = tanX * tanX + tanY * tanY;
    const Vec3 L = normalize(p.sunDirection);
    const Vec3 upRef = std::fabs(L.y) < 0.95f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    const Vec3 R = normalize(cross(upRef, L)), U = cross(L, R);
    float nearSplit = 0.0f;
    for (int i = 0; i < 3; ++i) {
      const float d0 = nearSplit, d1 = splits[i];
      const float z = std::min(d1, 0.5f * (d0 + d1) * (1.0f + k2));
      const float r = std::sqrt((d1 - z) * (d1 - z) + d1 * d1 * k2);
      const float texel = 2.0f * r / float(kSunShadowSize);
      Vec3 c = forward * z;
      const Vec3 world = c + p.cameraPosition;
      const float wx = dot(world, R), wy = dot(world, U);
      c = c + R * (std::floor(wx / texel) * texel - wx) + U * (std::floor(wy / texel) * texel - wy);
      const float zNear = r + 4000.0f, zFar = -r;  // reach casters well beyond the slice, toward the sun
      const float range = zNear - zFar;
      Mat4 m;
      for (int col = 0; col < 3; ++col) {
        m.at(0, col) = R[col] / r;
        m.at(1, col) = U[col] / r;
        m.at(2, col) = -L[col] / range;
      }
      m.at(0, 3) = -dot(c, R) / r;
      m.at(1, 3) = -dot(c, U) / r;
      m.at(2, 3) = (zNear + dot(c, L)) / range;
      u.sunShadow[i] = m;
      u.shadowInfo[i] = texel;
      u.shadowSplits[i] = d1;
      nearSplit = d1;
    }
    u.shadowSplits.w = p.sunShadows ? 1.0f : 0.0f;
  }
  std::memcpy(fr.uniforms.mapped, &u, sizeof(u));

  // ---- Overlay vertices (grow buffer as needed; this slot's GPU work is complete) ----
  const VkDeviceSize overlayBytes = overlay.size() * sizeof(OverlayVertex);
  if (overlayBytes > fr.overlayVertices.size) {
    gpu::destroy(ctx_, fr.overlayVertices);
    fr.overlayVertices = gpu::createBuffer(ctx_, std::max<VkDeviceSize>(overlayBytes * 2, 64 * 1024),
                                           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
  }
  if (overlayBytes) std::memcpy(fr.overlayVertices.mapped, overlay.data(), overlayBytes);

  // ---- Instances: group items by mesh so each submesh is one instanced draw ----
  drawOrder_.resize(items.size());
  for (std::uint32_t i = 0; i < items.size(); ++i) drawOrder_[i] = i;
  std::stable_sort(drawOrder_.begin(), drawOrder_.end(),
                   [&](std::uint32_t a, std::uint32_t b) { return items[a].mesh < items[b].mesh; });
  instanceScratch_.clear();
  for (std::uint32_t i : drawOrder_) instanceScratch_.push_back(items[i].model);
  const VkDeviceSize instanceBytes = instanceScratch_.size() * sizeof(Mat4);
  if (instanceBytes > fr.instances.size) {
    gpu::destroy(ctx_, fr.instances);
    fr.instances = gpu::createBuffer(ctx_, std::max<VkDeviceSize>(instanceBytes * 2, 64 * 1024),
                                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
  }
  if (instanceBytes) std::memcpy(fr.instances.mapped, instanceScratch_.data(), instanceBytes);
  const VkDeviceSize glowBytes = glows.size() * sizeof(GlowSprite);
  if (glowBytes > fr.glows.size) {
    gpu::destroy(ctx_, fr.glows);
    fr.glows = gpu::createBuffer(ctx_, std::max<VkDeviceSize>(glowBytes * 2, 16 * 1024),
                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
  }
  if (glowBytes) std::memcpy(fr.glows.mapped, glows.data(), glowBytes);
  const VkDeviceSize particleBytes = particles.size() * sizeof(ParticleSprite);
  if (particleBytes > fr.particles.size) {
    gpu::destroy(ctx_, fr.particles);
    fr.particles = gpu::createBuffer(ctx_, std::max<VkDeviceSize>(particleBytes * 2, 64 * 1024),
                                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
  }
  if (particleBytes) std::memcpy(fr.particles.mapped, particles.data(), particleBytes);
  const VkDeviceSize ribbonBytes = p.ribbons.size() * sizeof(RibbonVertex);
  if (ribbonBytes > fr.ribbons.size) {
    gpu::destroy(ctx_, fr.ribbons);
    fr.ribbons = gpu::createBuffer(ctx_, std::max<VkDeviceSize>(ribbonBytes * 2, 256 * 1024),
                                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
  }
  if (ribbonBytes) std::memcpy(fr.ribbons.mapped, p.ribbons.data(), ribbonBytes);

  const bool wantShot = screenshotPath && ctx_.swapchainSupportsTransferSrc();
  if (screenshotPath && !wantShot) log::warn("render", "screenshot unsupported: swapchain lacks TRANSFER_SRC usage");
  GpuBuffer readback;
  if (wantShot)
    readback = gpu::createBuffer(ctx_, VkDeviceSize(swapExtent.width) * swapExtent.height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                 true);

  // ---- Record ----
  VkCommandBuffer cmd = ctx_.frame().cmd;
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
  if (queryPool_) {
    vkCmdResetQueryPool(cmd, queryPool_, fi * 2, 2);
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, queryPool_, fi * 2);
  }

  const VkImage swapImage = ctx_.swapchainImage(imageIndex);
  RenderStats st;
  st.gpuMs = stats_.gpuMs;
  const VkPipelineStageFlags2 kDepthStages =
      VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
  const VkImageLayout kShaderRead = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  // Geometry only, instanced per mesh. `firstInstance`/`instanceCount` come from the mesh groups.
  auto forEachGroup = [&](auto&& fn) {
    std::uint32_t begin = 0;
    while (begin < drawOrder_.size()) {
      const MeshHandle handle = items[drawOrder_[begin]].mesh;
      std::uint32_t end = begin;
      while (end < drawOrder_.size() && items[drawOrder_[end]].mesh == handle) ++end;
      if (handle < meshes_.size()) fn(meshes_[handle], begin, end - begin);
      begin = end;
    }
  };
  auto bindGeometry = [&](const GpuMesh& m) {
    const VkBuffer buffers[2] = {m.vertices.buffer, fr.instances.buffer};
    const VkDeviceSize offsets[2] = {0, 0};
    vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
    vkCmdBindIndexBuffer(cmd, m.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
  };
  auto depthPass = [&](VkImageView view, VkExtent2D size, float clear) {
    VkRenderingAttachmentInfo att{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    att.imageView = view;
    att.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.clearValue.depthStencil = {clear, 0};
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea = {{0, 0}, size};
    info.layerCount = 1;
    info.pDepthAttachment = &att;
    vkCmdBeginRendering(cmd, &info);
    const VkViewport v{0, 0, float(size.width), float(size.height), 0.0f, 1.0f};
    const VkRect2D r{{0, 0}, size};
    vkCmdSetViewport(cmd, 0, 1, &v);
    vkCmdSetScissor(cmd, 0, 1, &r);
  };

  // ---- Sun shadow cascades ----
  if (p.sunShadows && !items.empty()) {
    gpu::imageBarrierRange(cmd, sunShadow_.image, kShaderRead, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                           VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0, kDepthStages,
                           VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 3);
    for (int c = 0; c < 3; ++c) {
      depthPass(sunLayerViews_[size_t(c)], {kSunShadowSize, kSunShadowSize}, 1.0f);
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sunShadowPipeline_);
      vkCmdPushConstants(cmd, shadowPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4), &u.sunShadow[c]);
      forEachGroup([&](const GpuMesh& m, std::uint32_t first, std::uint32_t count) {
        bindGeometry(m);
        for (const auto& sm : m.submeshes) {
          vkCmdDrawIndexed(cmd, sm.indexCount, count, sm.firstIndex, 0, first);
          ++st.drawCalls;
          st.triangles += std::uint64_t(sm.indexCount / 3) * count;
        }
      });
      vkCmdEndRendering(cmd);
    }
    gpu::imageBarrierRange(cmd, sunShadow_.image, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, kShaderRead, kDepthStages,
                           VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                           VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 3);
  }

  // ---- Lamp shadows: a cube map for each of the nearest lamps ----
  auto lampShadowPass = [&](const PointLight* lights, std::uint32_t shadowedLights) {
    if (shadowedLights == 0 || items.empty()) return;
    gpu::imageBarrierRange(cmd, pointShadow_.image, kShaderRead, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                           VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0, kDepthStages,
                           VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0,
                           kPointShadowLights * 6);
    struct PointPush {
      Vec4 lightPosNear;
      Vec4 farFace;
    };
    for (std::uint32_t li = 0; li < shadowedLights; ++li) {
      const PointLight& light = lights[li];
      // Only what the lamp can reach. Lamp fixtures glow and must not shadow their own light.
      std::vector<std::uint32_t> visible;
      for (std::uint32_t k = 0; k < drawOrder_.size(); ++k) {
        const MeshDrawItem& it = items[drawOrder_[k]];
        if (it.mesh >= meshes_.size() || !it.castsLampShadow) continue;
        const GpuMesh& m = meshes_[it.mesh];
        const float scale = length(Vec3{it.model.at(0, 0), it.model.at(1, 0), it.model.at(2, 0)});
        const Vec3 center = it.model.transformPoint(m.boundsCenter);
        const float reach = light.range + m.boundsRadius * scale;
        if (dot(center - light.position, center - light.position) <= reach * reach) visible.push_back(k);
      }
      for (std::uint32_t face = 0; face < 6; ++face) {
        depthPass(pointFaceViews_[li * 6 + face], {kPointShadowSize, kPointShadowSize}, 1.0f);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pointShadowPipeline_);
        const PointPush push{Vec4(light.position, kPointNear), Vec4(light.range, float(face), 0, 0)};
        vkCmdPushConstants(cmd, shadowPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
        MeshHandle bound = ~MeshHandle(0);
        for (std::uint32_t k : visible) {
          const MeshHandle handle = items[drawOrder_[k]].mesh;
          const GpuMesh& m = meshes_[handle];
          if (handle != bound) {
            bindGeometry(m);
            bound = handle;
          }
          for (const auto& sm : m.submeshes) {
            const size_t mi = (sm.material >= 0 && size_t(sm.material) + 1 < m.materials.size()) ? size_t(sm.material)
                                                                                                 : m.materials.size() - 1;
            const Vec3& e = m.materials[mi].emissive;
            if (e.x + e.y + e.z > 0.01f) continue;
            vkCmdDrawIndexed(cmd, sm.indexCount, 1, sm.firstIndex, 0, k);
            ++st.drawCalls;
            st.triangles += sm.indexCount / 3;
          }
        }
        vkCmdEndRendering(cmd);
      }
    }
    gpu::imageBarrierRange(cmd, pointShadow_.image, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, kShaderRead, kDepthStages,
                           VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                           VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0,
                           kPointShadowLights * 6);
  };

  // Shaded opaque geometry, shared by the probe and the main pass.
  auto drawMeshes = [&](VkPipeline pipeline, VkDescriptorSet frameSet, VkDescriptorSet lightSet) {
    if (items.empty()) return;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipelineLayout_, 0, 1, &frameSet, 0, nullptr);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipelineLayout_, 2, 1, &lightSet, 0, nullptr);
    forEachGroup([&](const GpuMesh& m, std::uint32_t first, std::uint32_t count) {
      bindGeometry(m);
      for (const auto& sm : m.submeshes) {
        const size_t matIndex = (sm.material >= 0 && size_t(sm.material) + 1 < m.materials.size())
                                    ? size_t(sm.material)
                                    : m.materials.size() - 1;
        const GpuMaterial& mat = m.materials[matIndex];
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipelineLayout_, 1, 1, &mat.set, 0, nullptr);
        MeshPush push{mat.baseColor, Vec4(mat.emissive, mat.metallic),
                      Vec4(mat.roughness, mat.panels, mat.textured ? 1.0f : 0.0f, mat.normalMapped ? 1.0f : 0.0f)};
        vkCmdPushConstants(cmd, meshPipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
        vkCmdDrawIndexed(cmd, sm.indexCount, count, sm.firstIndex, 0, first);
        ++st.drawCalls;
        st.triangles += std::uint64_t(sm.indexCount / 3) * count;
      }
      st.meshInstances += count;
    });
  };
  // Sky and planets for any view.
  auto drawBackground = [&](const Mat4& viewProj, VkExtent2D size, VkDescriptorSet frameSet, bool withStars) {
    const VkRect2D full{{0, 0}, size};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipelineLayout_, 0, 1, &frameSet, 0, nullptr);
    if (p.drawSky) {
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline_);
      vkCmdDraw(cmd, 3, 1, 0, 0);
      ++st.drawCalls;
      ++st.triangles;
    }
    if (withStars && starCount_ > 0 && p.drawStars) {
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, starPipeline_);
      VkDeviceSize off = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &starBuffer_.buffer, &off);
      vkCmdDraw(cmd, 6, starCount_, 0, 0);
      ++st.drawCalls;
      st.triangles += 2ull * starCount_;
      st.stars = starCount_;
    }
    if (p.planets.empty()) return;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, planetPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, planetPipelineLayout_, 0, 1, &frameSet, 0, nullptr);
    for (const PlanetDraw& pd : p.planets) {
      const float outer = pd.radius * (1.0f + pd.atmosphereThickness);
      // Screen bounds from the 8 corners of the bounding cube (full screen if any is behind).
      float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
      bool fullScreen = length(pd.center) < outer * 1.05f;
      for (int k = 0; k < 8 && !fullScreen; ++k) {
        const Vec3 corner = pd.center + Vec3{(k & 1) ? outer : -outer, (k & 2) ? outer : -outer, (k & 4) ? outer : -outer};
        const Vec4 clip = viewProj * Vec4(corner, 1.0f);
        if (clip.w <= 1.0f) {
          fullScreen = true;
          break;
        }
        const float sx = (clip.x / clip.w * 0.5f + 0.5f) * float(size.width);
        const float sy = (clip.y / clip.w * 0.5f + 0.5f) * float(size.height);
        minX = std::min(minX, sx), maxX = std::max(maxX, sx), minY = std::min(minY, sy), maxY = std::max(maxY, sy);
      }
      VkRect2D rect = full;
      if (!fullScreen) {
        const int x0 = std::clamp(int(std::floor(minX)), 0, int(size.width));
        const int y0 = std::clamp(int(std::floor(minY)), 0, int(size.height));
        const int x1 = std::clamp(int(std::ceil(maxX)), 0, int(size.width));
        const int y1 = std::clamp(int(std::ceil(maxY)), 0, int(size.height));
        if (x1 <= x0 || y1 <= y0) continue;  // off screen
        rect = {{x0, y0}, {std::uint32_t(x1 - x0), std::uint32_t(y1 - y0)}};
      }
      vkCmdSetScissor(cmd, 0, 1, &rect);
      const PlanetPush push{Vec4(pd.center, pd.radius), Vec4(pd.colorA, pd.seed), Vec4(pd.colorB, pd.craters),
                            Vec4(pd.atmosphereColor, pd.atmosphereThickness)};
      vkCmdPushConstants(cmd, planetPipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
      vkCmdDraw(cmd, 3, 1, 0, 0);
      ++st.drawCalls;
    }
    vkCmdSetScissor(cmd, 0, 1, &full);
  };

  // One face of a reflection probe, then that face's mip chain. Roughness reads blurrier mips.
  auto renderProbeFace = [&](GpuImage& target, VkImageView faceView, GpuImage& depth, std::uint32_t layer, std::uint32_t face,
                             std::uint32_t mips, const FrameUniforms& base, const Vec3& position) {
    const std::uint32_t size = target.extent.width;
    const CubeFace& cf = kCubeFaces[face];
    FrameUniforms pu = base;
    Mat4 vp;
    for (int col = 0; col < 3; ++col) {
      vp.at(0, col) = cf.sc[col];
      vp.at(1, col) = cf.tc[col];
      vp.at(2, col) = 0.0f;
      vp.at(3, col) = cf.ma[col];
    }
    vp.at(2, 3) = p.nearPlane;  // reversed-Z, infinite far plane
    vp.at(3, 3) = 0.0f;
    vp = vp * Mat4::translation(-position);
    pu.viewProj = vp;
    pu.invViewProj = vp.inverse();
    pu.viewport = Vec4(float(size), float(size), 1.0f / float(size), 1.0f / float(size));
    pu.viewOrigin = Vec4(position, 0.0f);
    pu.skyBase.w = 0.0f;     // no sun disc: direct light already counts the sun
    pu.lighting.x = 0.0f;    // no AO
    pu.lighting.y = 0.0f;    // no reflections of reflections
    pu.viewForward = Vec4(cf.ma, 0);
    // Cascades follow the camera's view, but the widest one covers everything near it, so probes
    // use only that one: coarse shadows are fine in a reflection, and sunlight stops at walls.
    pu.shadowSplits = p.sunShadows ? Vec4(-1e9f, -1e9f, 1e9f, 1.0f) : Vec4(0, 0, 0, 0);
    vkCmdUpdateBuffer(cmd, fr.probeUniforms.buffer, 0, sizeof(pu), &pu);
    VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    mb.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    mb.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    mb.dstAccessMask = VK_ACCESS_2_UNIFORM_READ_BIT;
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    di.memoryBarrierCount = 1;
    di.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(cmd, &di);

    gpu::imageBarrierRange(cmd, target.image, kShaderRead, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                           VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                           VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, layer, 1);
    gpu::imageBarrier(cmd, depth.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                      kDepthStages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, kDepthStages,
                      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                      VK_IMAGE_ASPECT_DEPTH_BIT);
    VkRenderingAttachmentInfo pc{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    pc.imageView = faceView;
    pc.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    pc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    pc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    pc.clearValue.color = {{p.skyBase.x, p.skyBase.y, p.skyBase.z, 1}};
    VkRenderingAttachmentInfo pd{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    pd.imageView = depth.view;
    pd.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    pd.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    pd.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    pd.clearValue.depthStencil = {0.0f, 0};
    VkRenderingInfo pri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    const VkExtent2D ps{size, size};
    pri.renderArea = {{0, 0}, ps};
    pri.layerCount = 1;
    pri.colorAttachmentCount = 1;
    pri.pColorAttachments = &pc;
    pri.pDepthAttachment = &pd;
    vkCmdBeginRendering(cmd, &pri);
    const VkViewport pv{0, 0, float(size), float(size), 0.0f, 1.0f};
    const VkRect2D pr{{0, 0}, ps};
    vkCmdSetViewport(cmd, 0, 1, &pv);
    vkCmdSetScissor(cmd, 0, 1, &pr);
    drawBackground(vp, ps, fr.probeSet, false);
    drawMeshes(meshProbePipeline_, fr.probeSet, probeLightingSet_);
    vkCmdEndRendering(cmd);

    gpu::imageBarrierRange(cmd, target.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                           VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT,
                           VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, layer, 1);
    std::int32_t mipSize = std::int32_t(size);
    for (std::uint32_t mip = 1; mip < mips; ++mip) {
      gpu::imageBarrierRange(cmd, target.image, kShaderRead, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_BLIT_BIT,
                             VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, layer, 1);
      VkImageBlit blit{};
      blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, layer, 1};
      blit.srcOffsets[1] = {mipSize, mipSize, 1};
      const std::int32_t next = std::max(1, mipSize / 2);
      blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, layer, 1};
      blit.dstOffsets[1] = {next, next, 1};
      vkCmdBlitImage(cmd, target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target.image,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
      gpu::imageBarrierRange(cmd, target.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_BLIT_BIT,
                             VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT,
                             VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, layer, 1);
      mipSize = next;
    }
    gpu::imageBarrierRange(cmd, target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, kShaderRead,
                           VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                           VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                           VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, layer, 1);
  };

  // ---- Placed reflection probes: one each frame until all are done, lit by the lamps around them ----
  if (p.reflections > 0.0f && probesBaked_ < probeArrayCount_ && probesBaked_ < p.reflectionProbes.size()) {
    const std::uint32_t index = probesBaked_;
    const Vec3 at = p.reflectionProbes[index].position;
    std::vector<PointLight> lamps = p.probeLights;
    std::sort(lamps.begin(), lamps.end(), [&](const PointLight& a, const PointLight& b) {
      return dot(a.position - at, a.position - at) < dot(b.position - at, b.position - at);
    });
    const std::uint32_t lampCount = std::uint32_t(std::min<size_t>(kPointShadowLights, lamps.size()));
    FrameUniforms base = u;
    base.lightCount = Vec4(float(lampCount), 0, 0, 0);
    for (std::uint32_t i = 0; i < lampCount; ++i) {
      base.pointPosRange[i] = Vec4(lamps[i].position, lamps[i].range);
      base.pointColor[i] = Vec4(lamps[i].color, 0);
    }
    const std::uint32_t shadowed = p.pointShadows ? std::min(kPointShadowLights, lampCount) : 0;
    base.shadowInfo.w = float(shadowed);
    lampShadowPass(lamps.data(), shadowed);
    for (std::uint32_t face = 0; face < 6; ++face) {
      const std::uint32_t layer = index * 6 + face;
      renderProbeFace(probeArray_, probeArrayFaceViews_[layer], probeArrayDepth_, layer, face, kPlacedProbeMips, base, at);
    }
    ++probesBaked_;
  }

  lampShadowPass(p.pointLights.data(), shadowedLights);

  // ---- Live reflection probe: one cube face per frame around the camera ----
  if (p.reflections > 0.0f) {
    const std::uint32_t faces = probeFilled_ ? 1 : 6;  // the first frame fills the whole cube
    for (std::uint32_t n = 0; n < faces; ++n) {
      const std::uint32_t face = probeFace_;
      probeFace_ = (probeFace_ + 1) % 6;
      renderProbeFace(probe_, probeFaceViews_[face], probeDepth_, face, face, kProbeMips, u, {0, 0, 0});
    }
    probeFilled_ = true;
  }

  // ---- Depth prepass: ambient occlusion needs depth before shading ----
  gpu::imageBarrier(cmd, depth_.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, kDepthStages,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, kDepthStages,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                    VK_IMAGE_ASPECT_DEPTH_BIT);
  depthPass(depth_.view, extent, 0.0f);  // reversed-Z: 0 is infinitely far
  if (!items.empty()) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, depthPrepassPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipelineLayout_, 0, 1, &fr.frameSet, 0, nullptr);
    forEachGroup([&](const GpuMesh& m, std::uint32_t first, std::uint32_t count) {
      bindGeometry(m);
      for (const auto& sm : m.submeshes) {
        vkCmdDrawIndexed(cmd, sm.indexCount, count, sm.firstIndex, 0, first);
        ++st.drawCalls;
        st.triangles += std::uint64_t(sm.indexCount / 3) * count;
      }
    });
  }
  vkCmdEndRendering(cmd);
  gpu::imageBarrier(cmd, depth_.image, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
                    kDepthStages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    kDepthStages | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                    VK_IMAGE_ASPECT_DEPTH_BIT);

  // ---- Ambient occlusion: half resolution, then a small blur ----
  if (p.ambientOcclusion) {
    auto aoPass = [&](GpuImage& target, auto&& bindAndDraw) {
      gpu::imageBarrier(cmd, target.image, kShaderRead, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
      VkRenderingAttachmentInfo att{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
      att.imageView = target.view;
      att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
      info.renderArea = {{0, 0}, target.extent};
      info.layerCount = 1;
      info.colorAttachmentCount = 1;
      info.pColorAttachments = &att;
      vkCmdBeginRendering(cmd, &info);
      const VkViewport v{0, 0, float(target.extent.width), float(target.extent.height), 0.0f, 1.0f};
      const VkRect2D r{{0, 0}, target.extent};
      vkCmdSetViewport(cmd, 0, 1, &v);
      vkCmdSetScissor(cmd, 0, 1, &r);
      bindAndDraw();
      vkCmdDraw(cmd, 3, 1, 0, 0);
      vkCmdEndRendering(cmd);
      gpu::imageBarrier(cmd, target.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, kShaderRead,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
      ++st.drawCalls;
    };
    aoPass(aoRaw_, [&] {
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ssaoPipeline_);
      const VkDescriptorSet sets[2] = {fr.frameSet, aoDepthSet_};
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ssaoPipelineLayout_, 0, 2, sets, 0, nullptr);
      const Vec4 push(0.9f, 1.1f, 0.02f, 0.0f);  // radius (m), intensity, radius growth with distance
      vkCmdPushConstants(cmd, ssaoPipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    });
    aoPass(ao_, [&] {
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ssaoBlurPipeline_);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bloomPipelineLayout_, 0, 1, &aoRawSet_, 0, nullptr);
      const Vec4 push(1.0f / float(ao_.extent.width), 1.0f / float(ao_.extent.height), 0.0f, 0.0f);
      vkCmdPushConstants(cmd, bloomPipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    });
  }

  // ---- Volumetric clouds: ray marched at half resolution, stopping at the prepass depth ----
  if (!p.clouds.empty()) {
    gpu::imageBarrier(cmd, cloudTarget_.image, kShaderRead, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    VkRenderingAttachmentInfo att{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    att.imageView = cloudTarget_.view;
    att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.clearValue.color = {{0, 0, 0, 0}};
    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea = {{0, 0}, cloudTarget_.extent};
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &att;
    vkCmdBeginRendering(cmd, &info);
    const VkViewport v{0, 0, float(cloudTarget_.extent.width), float(cloudTarget_.extent.height), 0.0f, 1.0f};
    const VkRect2D r{{0, 0}, cloudTarget_.extent};
    vkCmdSetViewport(cmd, 0, 1, &v);
    vkCmdSetScissor(cmd, 0, 1, &r);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, cloudPipeline_);
    const VkDescriptorSet sets[2] = {fr.frameSet, aoDepthSet_};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, cloudPipelineLayout_, 0, 2, sets, 0, nullptr);
    for (const CloudDraw& c : p.clouds) {
      const Vec4 push[4] = {Vec4(c.center, c.radius), Vec4(c.colorA, c.density), Vec4(c.colorB, c.seed),
                            Vec4(p.nearPlane, 0, 0, 0)};
      vkCmdPushConstants(cmd, cloudPipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), push);
      vkCmdDraw(cmd, 3, 1, 0, 0);
      ++st.drawCalls;
    }
    vkCmdEndRendering(cmd);
    gpu::imageBarrier(cmd, cloudTarget_.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, kShaderRead,
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
  }

  gpu::imageBarrier(cmd, hdr_.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

  VkRenderingAttachmentInfo colorAtt{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  colorAtt.imageView = hdr_.view;
  colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAtt.clearValue.color = {{p.skyBase.x, p.skyBase.y, p.skyBase.z, 1}};  // visible when the sky is off
  VkRenderingAttachmentInfo depthAtt{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  depthAtt.imageView = depth_.view;
  depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;  // from the prepass; also sampled by AO
  depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_NONE;
  VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
  ri.renderArea = {{0, 0}, extent};
  ri.layerCount = 1;
  ri.colorAttachmentCount = 1;
  ri.pColorAttachments = &colorAtt;
  ri.pDepthAttachment = &depthAtt;
  vkCmdBeginRendering(cmd, &ri);

  VkViewport vp{0, 0, float(extent.width), float(extent.height), 0.0f, 1.0f};
  VkRect2D sc{{0, 0}, extent};
  vkCmdSetViewport(cmd, 0, 1, &vp);
  vkCmdSetScissor(cmd, 0, 1, &sc);

  drawBackground(u.viewProj, extent, fr.frameSet, true);

  drawMeshes(meshPipeline_, fr.frameSet, lightingSet_);

  // Volumetric clouds, over opaque geometry.
  if (!p.clouds.empty()) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, cloudCompositePipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bloomPipelineLayout_, 0, 1, &cloudSet_, 0, nullptr);
    const Vec4 push(1.0f / float(cloudTarget_.extent.width), 1.0f / float(cloudTarget_.extent.height), 0.0f, 0.0f);
    vkCmdPushConstants(cmd, bloomPipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    ++st.drawCalls;
  }

  // Travel drive tunnel: behind the ship, over the distant sky.
  if (p.tunnel.amount > 0.001f) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, tunnelPipeline_);
    const VkDescriptorSet sets[2] = {fr.frameSet, aoDepthSet_};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, cloudPipelineLayout_, 0, 2, sets, 0, nullptr);
    const Vec4 push[4] = {Vec4(normalize(p.tunnel.forward), p.tunnel.amount), Vec4(p.tunnel.colorA, 0),
                          Vec4(p.tunnel.colorB, 0), Vec4(0, 0, 0, 0)};
    vkCmdPushConstants(cmd, cloudPipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    ++st.drawCalls;
  }

  // Glow sprites (additive, depth-tested against hulls, after opaque geometry)
  if (!glows.empty()) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, glowPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipelineLayout_, 0, 1, &fr.frameSet, 0, nullptr);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &fr.glows.buffer, &off);
    vkCmdDraw(cmd, 6, static_cast<std::uint32_t>(glows.size()), 0, 0);
    ++st.drawCalls;
  }

  // Exhaust ribbons.
  if (!p.ribbons.empty()) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ribbonPipeline_);
    const VkDescriptorSet sets[3] = {fr.frameSet, particleAtlasSet_, aoDepthSet_};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, particlePipelineLayout_, 0, 3, sets, 0, nullptr);
    const Vec4 push(p.nearPlane, 0.0f, 0.0f, 0.0f);
    vkCmdPushConstants(cmd, particlePipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &fr.ribbons.buffer, &off);
    vkCmdDraw(cmd, static_cast<std::uint32_t>(p.ribbons.size()), 1, 0, 0);
    ++st.drawCalls;
    st.triangles += p.ribbons.size() / 3;
  }

  // Particles: textured and soft against geometry (depth is read-only here, so it can be sampled).
  if (!particles.empty()) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, particlePipeline_);
    const VkDescriptorSet sets[3] = {fr.frameSet, particleAtlasSet_, aoDepthSet_};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, particlePipelineLayout_, 0, 3, sets, 0, nullptr);
    const Vec4 push(p.nearPlane, 0.0f, 0.0f, 0.0f);
    vkCmdPushConstants(cmd, particlePipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &fr.particles.buffer, &off);
    vkCmdDraw(cmd, 6, static_cast<std::uint32_t>(particles.size()), 0, 0);
    ++st.drawCalls;
  }

  vkCmdEndRendering(cmd);

  // ---- Tonemap HDR -> swapchain, then overlay ----
  gpu::imageBarrier(cmd, hdr_.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
  // ---- Bloom: bright-pass downsample, then separable blur ping-pong at quarter resolution ----
  const PostProcessParams& pp = p.post;
  VkDescriptorSet bloomSet = tonemapSet_;  // any readable image; contributes nothing at strength 0
  if (pp.bloomStrength > 0.0f) {
    const VkExtent2D be = bloomA_.extent;
    auto bloomPass = [&](GpuImage& target, VkPipeline pipeline, VkDescriptorSet src, const Vec4& push) {
      gpu::imageBarrier(cmd, target.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
      VkRenderingAttachmentInfo att{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
      att.imageView = target.view;
      att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      VkRenderingInfo bri{VK_STRUCTURE_TYPE_RENDERING_INFO};
      bri.renderArea = {{0, 0}, be};
      bri.layerCount = 1;
      bri.colorAttachmentCount = 1;
      bri.pColorAttachments = &att;
      vkCmdBeginRendering(cmd, &bri);
      VkViewport bvp{0, 0, float(be.width), float(be.height), 0.0f, 1.0f};
      VkRect2D bsc{{0, 0}, be};
      vkCmdSetViewport(cmd, 0, 1, &bvp);
      vkCmdSetScissor(cmd, 0, 1, &bsc);
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bloomPipelineLayout_, 0, 1, &src, 0, nullptr);
      vkCmdPushConstants(cmd, bloomPipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Vec4), &push);
      vkCmdDraw(cmd, 3, 1, 0, 0);
      vkCmdEndRendering(cmd);
      gpu::imageBarrier(cmd, target.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
      ++st.drawCalls;
    };
    bloomPass(bloomA_, bloomDownPipeline_, tonemapSet_,
              Vec4(pp.bloomThreshold, std::max(1e-3f, pp.bloomKnee), 1.0f / float(extent.width),
                   1.0f / float(extent.height)));
    const int iterations = std::clamp(pp.bloomIterations, 1, 4);
    for (int i = 0; i < iterations; ++i) {
      const float r = pp.bloomRadius;  // repeated constant-width blur widens smoothly (no ringing)
      bloomPass(bloomB_, bloomBlurPipeline_, bloomSetA_, Vec4(r / float(be.width), 0, 0, 0));
      bloomPass(bloomA_, bloomBlurPipeline_, bloomSetB_, Vec4(0, r / float(be.height), 0, 0));
    }
    bloomSet = bloomSetA_;
  }

  gpu::imageBarrier(cmd, swapImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
  VkRenderingAttachmentInfo swapAtt{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  swapAtt.imageView = ctx_.swapchainView(imageIndex);
  swapAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  swapAtt.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // the fullscreen tonemap writes every pixel
  swapAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkRenderingInfo ri2{VK_STRUCTURE_TYPE_RENDERING_INFO};
  ri2.renderArea = {{0, 0}, swapExtent};
  ri2.layerCount = 1;
  ri2.colorAttachmentCount = 1;
  ri2.pColorAttachments = &swapAtt;
  vkCmdBeginRendering(cmd, &ri2);
  const VkViewport swapVp{0, 0, float(swapExtent.width), float(swapExtent.height), 0.0f, 1.0f};
  const VkRect2D swapSc{{0, 0}, swapExtent};
  vkCmdSetViewport(cmd, 0, 1, &swapVp);
  vkCmdSetScissor(cmd, 0, 1, &swapSc);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, tonemapPipeline_);
  const VkDescriptorSet tonemapSets[3] = {fr.frameSet, tonemapSet_, bloomSet};
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, tonemapPipelineLayout_, 0, 3, tonemapSets, 0, nullptr);
  const PostPush post{Vec4(pp.shadowTint, pp.saturation), Vec4(pp.highlightTint, pp.contrast),
                      Vec4(pp.vignette, pp.scanlines, pp.scanlinePeriod, pp.chromaticAberration),
                      Vec4(pp.grain, pp.bloomStrength, p.timeSeconds, 0.0f), Vec4(pp.fadeColor, pp.fadeAmount),
                      Vec4(pp.heat, 0, 0, 0)};
  vkCmdPushConstants(cmd, tonemapPipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(post), &post);
  vkCmdDraw(cmd, 3, 1, 0, 0);
  ++st.drawCalls;

  // Overlay
  if (!overlay.empty()) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, overlayPipeline_);
    const float invHalf[2] = {2.0f / float(swapExtent.width), 2.0f / float(swapExtent.height)};
    vkCmdPushConstants(cmd, overlayPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(invHalf), invHalf);
    if (!overlaySet_) overlaySet_ = allocateMaterialSet(whiteTexture_.view, clampSampler_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, overlayPipelineLayout_, 0, 1, &overlaySet_, 0, nullptr);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &fr.overlayVertices.buffer, &off);
    vkCmdDraw(cmd, static_cast<std::uint32_t>(overlay.size()), 1, 0, 0);
    ++st.drawCalls;
  }

  vkCmdEndRendering(cmd);

  if (wantShot) {
    gpu::imageBarrier(cmd, swapImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {swapExtent.width, swapExtent.height, 1};
    vkCmdCopyImageToBuffer(cmd, swapImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &region);
    gpu::imageBarrier(cmd, swapImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                      VK_PIPELINE_STAGE_2_COPY_BIT, 0, VK_PIPELINE_STAGE_2_NONE, 0);
  } else {
    gpu::imageBarrier(cmd, swapImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_NONE, 0);
  }
  if (queryPool_) {
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, queryPool_, fi * 2 + 1);
    fr.timestampsWritten = true;
  }
  VK_CHECK(vkEndCommandBuffer(cmd));
  ctx_.endFrame(imageIndex);
  stats_ = st;

  if (wantShot) {
    vkDeviceWaitIdle(ctx_.device());
    writeScreenshot(*screenshotPath, readback, swapExtent);
    gpu::destroy(ctx_, readback);
  }
  return true;
}

void Renderer::writeScreenshot(const std::filesystem::path& path, const GpuBuffer& readback, VkExtent2D extent) {
  ImageRGBA8 img;
  img.width = int(extent.width);
  img.height = int(extent.height);
  img.pixels.resize(size_t(extent.width) * extent.height * 4);
  std::memcpy(img.pixels.data(), readback.mapped, img.pixels.size());
  const VkFormat f = ctx_.swapchainFormat();
  const bool bgra = f == VK_FORMAT_B8G8R8A8_SRGB || f == VK_FORMAT_B8G8R8A8_UNORM;
  for (size_t i = 0; i < img.pixels.size(); i += 4) {
    if (bgra) std::swap(img.pixels[i], img.pixels[i + 2]);
    img.pixels[i + 3] = 255;
  }
  if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
  if (writePng(path, img)) log::info("render", "screenshot written: {}", path.string());
}

}  // namespace fh
