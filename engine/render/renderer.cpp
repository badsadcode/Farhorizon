#include "engine/render/renderer.h"

#include <algorithm>
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
};
static_assert(sizeof(FrameUniforms) == 208);

struct MeshPush {  // must match mesh.vert / mesh.frag push constants
  Mat4 model;
  Vec4 baseColor;
  Vec4 emissiveMetallic;
  Vec4 params;
};
static_assert(sizeof(MeshPush) == 112);

enum class Blend { None, Additive, Alpha };

struct PipelineDesc {
  VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  std::vector<VkVertexInputBindingDescription> bindings;
  std::vector<VkVertexInputAttributeDescription> attributes;
  Blend blend = Blend::None;
  bool depthTest = false;
  bool depthWrite = false;
  VkCullModeFlags cull = VK_CULL_MODE_NONE;
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
  ds.depthCompareOp = VK_COMPARE_OP_GREATER;  // reversed-Z

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
  cb.attachmentCount = 1;
  cb.pAttachments = &att;

  const VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dy.dynamicStateCount = 2;
  dy.pDynamicStates = dyn;

  VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachmentFormats = &colorFormat;
  rendering.depthAttachmentFormat = depthFormat;

  VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  ci.pNext = &rendering;
  ci.stageCount = 2;
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
  VK_CHECK(vkCreateSampler(ctx_.device(), &si, nullptr, &sampler_));

  const std::uint8_t white[4] = {255, 255, 255, 255};
  whiteTexture_ = gpu::createTextureRGBA8(ctx_, white, 1, 1);

  for (auto& f : frames_) {
    f.uniforms = gpu::createBuffer(ctx_, sizeof(FrameUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
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
  }

  if (ctx_.timestampsSupported()) {
    VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = 2 * VkContext::kFramesInFlight;
    VK_CHECK(vkCreateQueryPool(ctx_.device(), &qi, nullptr, &queryPool_));
  }
  ensureDepth();
}

Renderer::~Renderer() {
  vkDeviceWaitIdle(ctx_.device());
  clearMeshes();
  gpu::destroy(ctx_, starBuffer_);
  for (auto& f : frames_) {
    gpu::destroy(ctx_, f.uniforms);
    gpu::destroy(ctx_, f.overlayVertices);
  }
  gpu::destroy(ctx_, whiteTexture_);
  gpu::destroy(ctx_, depth_);
  VkDevice d = ctx_.device();
  if (queryPool_) vkDestroyQueryPool(d, queryPool_, nullptr);
  vkDestroySampler(d, sampler_, nullptr);
  for (VkPipeline p : {skyPipeline_, starPipeline_, meshPipeline_, overlayPipeline_}) vkDestroyPipeline(d, p, nullptr);
  for (VkPipelineLayout l : {scenePipelineLayout_, meshPipelineLayout_, overlayPipelineLayout_})
    vkDestroyPipelineLayout(d, l, nullptr);
  vkDestroyDescriptorPool(d, descriptorPool_, nullptr);
  vkDestroyDescriptorSetLayout(d, frameLayout_, nullptr);
  vkDestroyDescriptorSetLayout(d, materialLayout_, nullptr);
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

  const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8},
                                        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512}};
  VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pi.maxSets = 520;
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
  const VkFormat color = ctx_.swapchainFormat();

  VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  li.setLayoutCount = 1;
  li.pSetLayouts = &frameLayout_;
  VK_CHECK(vkCreatePipelineLayout(d, &li, nullptr, &scenePipelineLayout_));

  const VkDescriptorSetLayout meshSets[] = {frameLayout_, materialLayout_};
  VkPushConstantRange meshPush{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MeshPush)};
  li.setLayoutCount = 2;
  li.pSetLayouts = meshSets;
  li.pushConstantRangeCount = 1;
  li.pPushConstantRanges = &meshPush;
  VK_CHECK(vkCreatePipelineLayout(d, &li, nullptr, &meshPipelineLayout_));

  VkPushConstantRange overlayPush{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 2};
  li.setLayoutCount = 0;
  li.pSetLayouts = nullptr;
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

  PipelineDesc stars;
  stars.vert = load("stars.vert");
  stars.frag = load("stars.frag");
  stars.layout = scenePipelineLayout_;
  stars.blend = Blend::Additive;
  stars.bindings = {{0, sizeof(StarInstance), VK_VERTEX_INPUT_RATE_INSTANCE}};
  stars.attributes = {{0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16}};
  starPipeline_ = buildPipeline(d, stars, color, kDepthFormat);

  PipelineDesc mesh;
  mesh.vert = load("mesh.vert");
  mesh.frag = load("mesh.frag");
  mesh.layout = meshPipelineLayout_;
  mesh.depthTest = mesh.depthWrite = true;
  mesh.cull = VK_CULL_MODE_BACK_BIT;
  mesh.bindings = {{0, sizeof(MeshVertex), VK_VERTEX_INPUT_RATE_VERTEX}};
  mesh.attributes = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, position)},
                     {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, normal)},
                     {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(MeshVertex, uv)}};
  meshPipeline_ = buildPipeline(d, mesh, color, kDepthFormat);

  PipelineDesc overlay;
  overlay.vert = load("overlay.vert");
  overlay.frag = load("overlay.frag");
  overlay.layout = overlayPipelineLayout_;
  overlay.blend = Blend::Alpha;
  overlay.bindings = {{0, sizeof(OverlayVertex), VK_VERTEX_INPUT_RATE_VERTEX}};
  overlay.attributes = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0}, {1, 0, VK_FORMAT_R8G8B8A8_UNORM, 8}};
  overlayPipeline_ = buildPipeline(d, overlay, color, kDepthFormat);
  log::info("render", "pipelines created (sky, stars, mesh, overlay)");
}

void Renderer::ensureDepth() {
  const VkExtent2D e = ctx_.swapchainExtent();
  if (depth_.image && depth_.extent.width == e.width && depth_.extent.height == e.height) return;
  vkDeviceWaitIdle(ctx_.device());
  gpu::destroy(ctx_, depth_);
  depth_ = gpu::createImage(ctx_, e, kDepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
}

VkDescriptorSet Renderer::allocateMaterialSet(VkImageView view) {
  VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  ai.descriptorPool = descriptorPool_;
  ai.descriptorSetCount = 1;
  ai.pSetLayouts = &materialLayout_;
  VkDescriptorSet set;
  VK_CHECK(vkAllocateDescriptorSets(ctx_.device(), &ai, &set));
  VkDescriptorImageInfo ii{sampler_, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
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
    g.textures.push_back(gpu::createTextureRGBA8(ctx_, img.pixels.data(), std::uint32_t(img.width),
                                                 std::uint32_t(img.height)));
  for (const auto& m : mesh.materials) {
    GpuMaterial gm;
    const bool hasTex = m.baseColorImage >= 0 && size_t(m.baseColorImage) < g.textures.size();
    gm.set = allocateMaterialSet(hasTex ? g.textures[size_t(m.baseColorImage)].view : whiteTexture_.view);
    gm.baseColor = m.baseColorFactor;
    gm.emissive = m.emissive;
    gm.metallic = m.metallic;
    gm.roughness = m.roughness;
    g.materials.push_back(gm);
  }
  GpuMaterial fallback;  // default material for primitives without one
  fallback.set = allocateMaterialSet(whiteTexture_.view);
  fallback.baseColor = {0.8f, 0.8f, 0.8f, 1.0f};
  g.materials.push_back(fallback);
  g.submeshes = mesh.submeshes;
  g.triangles = mesh.triangleCount();
  meshes_.push_back(std::move(g));
  return static_cast<MeshHandle>(meshes_.size() - 1);
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
                      const std::vector<OverlayVertex>& overlay, const std::filesystem::path* screenshotPath) {
  std::uint32_t imageIndex = 0;
  if (!ctx_.beginFrame(imageIndex)) return false;
  ensureDepth();
  const std::uint32_t fi = ctx_.frameIndex();
  FrameResources& fr = frames_[fi];
  const VkExtent2D extent = ctx_.swapchainExtent();

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
  std::memcpy(fr.uniforms.mapped, &u, sizeof(u));

  // ---- Overlay vertices (grow buffer as needed; this slot's GPU work is complete) ----
  const VkDeviceSize overlayBytes = overlay.size() * sizeof(OverlayVertex);
  if (overlayBytes > fr.overlayVertices.size) {
    gpu::destroy(ctx_, fr.overlayVertices);
    fr.overlayVertices = gpu::createBuffer(ctx_, std::max<VkDeviceSize>(overlayBytes * 2, 64 * 1024),
                                           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
  }
  if (overlayBytes) std::memcpy(fr.overlayVertices.mapped, overlay.data(), overlayBytes);

  const bool wantShot = screenshotPath && ctx_.swapchainSupportsTransferSrc();
  if (screenshotPath && !wantShot) log::warn("render", "screenshot unsupported: swapchain lacks TRANSFER_SRC usage");
  GpuBuffer readback;
  if (wantShot)
    readback = gpu::createBuffer(ctx_, VkDeviceSize(extent.width) * extent.height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
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
  gpu::imageBarrier(cmd, swapImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
  gpu::imageBarrier(cmd, depth_.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                    VK_IMAGE_ASPECT_DEPTH_BIT);

  VkRenderingAttachmentInfo colorAtt{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  colorAtt.imageView = ctx_.swapchainView(imageIndex);
  colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAtt.clearValue.color = {{0, 0, 0, 1}};
  VkRenderingAttachmentInfo depthAtt{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  depthAtt.imageView = depth_.view;
  depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
  depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAtt.clearValue.depthStencil = {0.0f, 0};  // reversed-Z: 0 is infinitely far
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

  RenderStats st;
  st.gpuMs = stats_.gpuMs;

  // Sky
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline_);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipelineLayout_, 0, 1, &fr.frameSet, 0, nullptr);
  vkCmdDraw(cmd, 3, 1, 0, 0);
  ++st.drawCalls;
  ++st.triangles;

  // Stars (instanced)
  if (starCount_ > 0) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, starPipeline_);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &starBuffer_.buffer, &off);
    vkCmdDraw(cmd, 6, starCount_, 0, 0);
    ++st.drawCalls;
    st.triangles += 2ull * starCount_;
    st.stars = starCount_;
  }

  // Meshes
  if (!items.empty()) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipelineLayout_, 0, 1, &fr.frameSet, 0, nullptr);
    for (const auto& item : items) {
      if (item.mesh >= meshes_.size()) continue;
      const GpuMesh& m = meshes_[item.mesh];
      VkDeviceSize off = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &m.vertices.buffer, &off);
      vkCmdBindIndexBuffer(cmd, m.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
      for (const auto& sm : m.submeshes) {
        const size_t matIndex =
            (sm.material >= 0 && size_t(sm.material) + 1 < m.materials.size()) ? size_t(sm.material) : m.materials.size() - 1;
        const GpuMaterial& mat = m.materials[matIndex];
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipelineLayout_, 1, 1, &mat.set, 0, nullptr);
        MeshPush push{item.model, mat.baseColor, Vec4(mat.emissive, mat.metallic), Vec4(mat.roughness, 0, 0, 0)};
        vkCmdPushConstants(cmd, meshPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(push), &push);
        vkCmdDrawIndexed(cmd, sm.indexCount, 1, sm.firstIndex, 0, 0);
        ++st.drawCalls;
        st.triangles += sm.indexCount / 3;
      }
    }
  }

  // Overlay
  if (!overlay.empty()) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, overlayPipeline_);
    const float invHalf[2] = {2.0f / float(extent.width), 2.0f / float(extent.height)};
    vkCmdPushConstants(cmd, overlayPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(invHalf), invHalf);
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
    region.imageExtent = {extent.width, extent.height, 1};
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
    writeScreenshot(*screenshotPath, readback, extent);
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
