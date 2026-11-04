#include "engine/render/gpu_resources.h"

#include <atomic>
#include <cstring>

#include "engine/render/vk_context.h"

namespace fh::gpu {

namespace {
std::atomic<std::uint64_t> g_allocated{0};

VkDeviceMemory allocate(VkContext& ctx, const VkMemoryRequirements& req, VkMemoryPropertyFlags props) {
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = ctx.findMemoryType(req.memoryTypeBits, props);
  VkDeviceMemory mem;
  VK_CHECK(vkAllocateMemory(ctx.device(), &ai, nullptr, &mem));
  g_allocated += req.size;
  return mem;
}

}  // namespace

std::uint64_t allocatedBytes() { return g_allocated.load(); }

GpuBuffer createBuffer(VkContext& ctx, VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible) {
  GpuBuffer b;
  b.size = size;
  VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  ci.size = size;
  ci.usage = usage;
  ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VK_CHECK(vkCreateBuffer(ctx.device(), &ci, nullptr, &b.buffer));
  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(ctx.device(), b.buffer, &req);
  b.memory = allocate(ctx, req,
                      hostVisible ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                  : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  VK_CHECK(vkBindBufferMemory(ctx.device(), b.buffer, b.memory, 0));
  if (hostVisible) VK_CHECK(vkMapMemory(ctx.device(), b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped));
  return b;
}

GpuBuffer createBufferWithData(VkContext& ctx, const void* data, VkDeviceSize size, VkBufferUsageFlags usage) {
  GpuBuffer staging = createBuffer(ctx, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
  std::memcpy(staging.mapped, data, size);
  GpuBuffer dst = createBuffer(ctx, size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
  ctx.immediateSubmit([&](VkCommandBuffer cmd) {
    VkBufferCopy region{0, 0, size};
    vkCmdCopyBuffer(cmd, staging.buffer, dst.buffer, 1, &region);
  });
  destroy(ctx, staging);
  return dst;
}

void destroy(VkContext& ctx, GpuBuffer& b) {
  if (!b.buffer) return;
  VkMemoryRequirements req;
  vkGetBufferMemoryRequirements(ctx.device(), b.buffer, &req);
  g_allocated -= req.size;
  if (b.mapped) vkUnmapMemory(ctx.device(), b.memory);
  vkDestroyBuffer(ctx.device(), b.buffer, nullptr);
  vkFreeMemory(ctx.device(), b.memory, nullptr);
  b = {};
}

GpuImage createImage(VkContext& ctx, VkExtent2D extent, VkFormat format, VkImageUsageFlags usage,
                     VkImageAspectFlags aspect) {
  GpuImage img;
  img.format = format;
  img.extent = extent;
  VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ci.imageType = VK_IMAGE_TYPE_2D;
  ci.format = format;
  ci.extent = {extent.width, extent.height, 1};
  ci.mipLevels = 1;
  ci.arrayLayers = 1;
  ci.samples = VK_SAMPLE_COUNT_1_BIT;
  ci.tiling = VK_IMAGE_TILING_OPTIMAL;
  ci.usage = usage;
  ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VK_CHECK(vkCreateImage(ctx.device(), &ci, nullptr, &img.image));
  VkMemoryRequirements req;
  vkGetImageMemoryRequirements(ctx.device(), img.image, &req);
  img.memory = allocate(ctx, req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  VK_CHECK(vkBindImageMemory(ctx.device(), img.image, img.memory, 0));
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = img.image;
  vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vi.format = format;
  vi.subresourceRange = {aspect, 0, 1, 0, 1};
  VK_CHECK(vkCreateImageView(ctx.device(), &vi, nullptr, &img.view));
  return img;
}

GpuImage createImage(VkContext& ctx, const ImageDesc& d) {
  GpuImage img;
  img.format = d.format;
  img.extent = d.extent;
  VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ci.flags = d.cubeCompatible ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
  ci.imageType = VK_IMAGE_TYPE_2D;
  ci.format = d.format;
  ci.extent = {d.extent.width, d.extent.height, 1};
  ci.mipLevels = d.mips;
  ci.arrayLayers = d.layers;
  ci.samples = VK_SAMPLE_COUNT_1_BIT;
  ci.tiling = VK_IMAGE_TILING_OPTIMAL;
  ci.usage = d.usage;
  ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VK_CHECK(vkCreateImage(ctx.device(), &ci, nullptr, &img.image));
  VkMemoryRequirements req;
  vkGetImageMemoryRequirements(ctx.device(), img.image, &req);
  img.memory = allocate(ctx, req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  VK_CHECK(vkBindImageMemory(ctx.device(), img.image, img.memory, 0));
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = img.image;
  vi.viewType = d.viewType;
  vi.format = d.format;
  vi.subresourceRange = {d.aspect, 0, d.mips, 0, d.layers};
  VK_CHECK(vkCreateImageView(ctx.device(), &vi, nullptr, &img.view));
  return img;
}

VkImageView createSubView(VkContext& ctx, const GpuImage& image, VkImageAspectFlags aspect, std::uint32_t mip,
                          std::uint32_t layer) {
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = image.image;
  vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vi.format = image.format;
  vi.subresourceRange = {aspect, mip, 1, layer, 1};
  VkImageView view;
  VK_CHECK(vkCreateImageView(ctx.device(), &vi, nullptr, &view));
  return view;
}

GpuImage createTextureRGBA8(VkContext& ctx, const std::uint8_t* pixels, std::uint32_t w, std::uint32_t h) {
  const VkDeviceSize size = VkDeviceSize(w) * h * 4;
  GpuBuffer staging = createBuffer(ctx, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
  std::memcpy(staging.mapped, pixels, size);
  GpuImage img = createImage(ctx, {w, h}, VK_FORMAT_R8G8B8A8_SRGB,
                             VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
  ctx.immediateSubmit([&](VkCommandBuffer cmd) {
    imageBarrier(cmd, img.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {w, h, 1};
    vkCmdCopyBufferToImage(cmd, staging.buffer, img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    imageBarrier(cmd, img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
  });
  destroy(ctx, staging);
  return img;
}

void destroy(VkContext& ctx, GpuImage& img) {
  if (!img.image) return;
  VkMemoryRequirements req;
  vkGetImageMemoryRequirements(ctx.device(), img.image, &req);
  g_allocated -= req.size;
  vkDestroyImageView(ctx.device(), img.view, nullptr);
  vkDestroyImage(ctx.device(), img.image, nullptr);
  vkFreeMemory(ctx.device(), img.memory, nullptr);
  img = {};
}

void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                  VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                  VkAccessFlags2 dstAccess, VkImageAspectFlags aspect) {
  VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
  b.srcStageMask = srcStage;
  b.srcAccessMask = srcAccess;
  b.dstStageMask = dstStage;
  b.dstAccessMask = dstAccess;
  b.oldLayout = oldLayout;
  b.newLayout = newLayout;
  b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image;
  b.subresourceRange = {aspect, 0, 1, 0, 1};
  VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  di.imageMemoryBarrierCount = 1;
  di.pImageMemoryBarriers = &b;
  vkCmdPipelineBarrier2(cmd, &di);
}

void imageBarrierRange(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                       VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                       VkAccessFlags2 dstAccess, VkImageAspectFlags aspect, std::uint32_t baseMip, std::uint32_t mips,
                       std::uint32_t baseLayer, std::uint32_t layers) {
  VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
  b.srcStageMask = srcStage;
  b.srcAccessMask = srcAccess;
  b.dstStageMask = dstStage;
  b.dstAccessMask = dstAccess;
  b.oldLayout = oldLayout;
  b.newLayout = newLayout;
  b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image;
  b.subresourceRange = {aspect, baseMip, mips, baseLayer, layers};
  VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  di.imageMemoryBarrierCount = 1;
  di.pImageMemoryBarriers = &b;
  vkCmdPipelineBarrier2(cmd, &di);
}

}  // namespace fh::gpu
