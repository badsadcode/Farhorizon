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
// RGBA8 sRGB texture uploaded via staging, left in SHADER_READ_ONLY_OPTIMAL.
GpuImage createTextureRGBA8(VkContext& ctx, const std::uint8_t* pixels, std::uint32_t w, std::uint32_t h);
void destroy(VkContext& ctx, GpuImage& img);

void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                  VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                  VkAccessFlags2 dstAccess, VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);

// Bytes of device memory currently allocated through these helpers (developer overlay).
std::uint64_t allocatedBytes();

}  // namespace gpu
}  // namespace fh
