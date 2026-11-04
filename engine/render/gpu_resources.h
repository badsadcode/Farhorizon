#pragma once
// Plain GPU buffer/image helpers. One allocation per resource for now (Stage 1 has a handful
// of resources); a sub-allocator is a tracked follow-up once resource counts grow.
#include <cstdint>

#include "engine/render/vk_common.h"

namespace fh {

class VkContext;

struct GpuBuffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
  void* mapped = nullptr;  // non-null for host-visible buffers (persistently mapped)
};

struct GpuImage {
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkExtent2D extent{};
};

namespace gpu {

GpuBuffer createBuffer(VkContext& ctx, VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible);
// Device-local buffer filled through a staging copy.
GpuBuffer createBufferWithData(VkContext& ctx, const void* data, VkDeviceSize size, VkBufferUsageFlags usage);
void destroy(VkContext& ctx, GpuBuffer& b);

GpuImage createImage(VkContext& ctx, VkExtent2D extent, VkFormat format, VkImageUsageFlags usage,
                     VkImageAspectFlags aspect);
// Layered and/or mipped image with its own view type (cube maps, shadow arrays).
struct ImageDesc {
  VkExtent2D extent{};
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkImageUsageFlags usage = 0;
  VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
  std::uint32_t mips = 1;
  std::uint32_t layers = 1;
  bool cubeCompatible = false;
  VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
};
GpuImage createImage(VkContext& ctx, const ImageDesc& desc);
// A view of part of an image (one shadow cascade, one cube face). The caller destroys it.
VkImageView createSubView(VkContext& ctx, const GpuImage& image, VkImageAspectFlags aspect, std::uint32_t mip,
                          std::uint32_t layer);
// RGBA8 sRGB texture uploaded via staging, left in SHADER_READ_ONLY_OPTIMAL.
GpuImage createTextureRGBA8(VkContext& ctx, const std::uint8_t* pixels, std::uint32_t w, std::uint32_t h);
void destroy(VkContext& ctx, GpuImage& img);

void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                  VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                  VkAccessFlags2 dstAccess, VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);

// Barrier over a mip and layer range.
void imageBarrierRange(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                       VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                       VkAccessFlags2 dstAccess, VkImageAspectFlags aspect, std::uint32_t baseMip, std::uint32_t mips,
                       std::uint32_t baseLayer, std::uint32_t layers);

// Bytes of device memory currently allocated through these helpers (developer overlay).
std::uint64_t allocatedBytes();

}  // namespace gpu
}  // namespace fh
