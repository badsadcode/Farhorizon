#pragma once
// Owns the Vulkan instance, device, queue and swapchain, plus per-frame sync objects.
// Minimum feature set (ADR 0001): Vulkan 1.3 with dynamicRendering + synchronization2.
// Thread affinity: main thread only.
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "engine/render/vk_common.h"

namespace fh {

class Window;

struct VkContextOptions {
  bool validation = false;
  bool vsync = true;
};

class VkContext {
 public:
  static constexpr std::uint32_t kFramesInFlight = 2;

  VkContext(Window& window, const VkContextOptions& options);
  ~VkContext();
  VkContext(const VkContext&) = delete;
  VkContext& operator=(const VkContext&) = delete;

  struct FrameSync {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkFence inFlight = VK_NULL_HANDLE;
  };

  // Begins a frame: waits for the frame slot, acquires an image. Returns false if the
  // swapchain is out of date (it has been recreated; skip this frame).
  bool beginFrame(std::uint32_t& imageIndex);
  // Submits the frame's command buffer and presents. Recreates the swapchain if needed.
  void endFrame(std::uint32_t imageIndex);
  void recreateSwapchain();

  // Records and synchronously executes one-off commands (uploads, readbacks).
  void immediateSubmit(const std::function<void(VkCommandBuffer)>& record);

  std::uint32_t findMemoryType(std::uint32_t typeBits, VkMemoryPropertyFlags props) const;

  VkInstance instance() const { return instance_; }
  VkPhysicalDevice physicalDevice() const { return physical_; }
  VkDevice device() const { return device_; }
  VkQueue queue() const { return queue_; }
  std::uint32_t queueFamily() const { return queueFamily_; }
  VkFormat swapchainFormat() const { return swapFormat_; }
  VkExtent2D swapchainExtent() const { return swapExtent_; }
  VkImage swapchainImage(std::uint32_t i) const { return swapImages_[i]; }
  VkImageView swapchainView(std::uint32_t i) const { return swapViews_[i]; }
  bool swapchainSupportsTransferSrc() const { return swapTransferSrc_; }
  FrameSync& frame() { return frames_[frameIndex_]; }
  std::uint32_t frameIndex() const { return frameIndex_; }
  const std::string& deviceName() const { return deviceName_; }
  float timestampPeriodNs() const { return timestampPeriod_; }
  bool timestampsSupported() const { return timestampsSupported_; }
  bool anisotropySupported() const { return anisotropySupported_; }
  bool validationEnabled() const { return validationEnabled_; }
  static int validationMessageCount();
  void setVsync(bool on);
  bool vsync() const { return options_.vsync; }

 private:
  bool anisotropySupported_ = false;
  void createInstance();
  void pickPhysicalDevice();
  void createDevice();
  void createSwapchain();
  void destroySwapchain();

  Window& window_;
  VkContextOptions options_;
  VkInstance instance_ = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  std::uint32_t queueFamily_ = 0;
  std::string deviceName_;
  float timestampPeriod_ = 0;
  bool timestampsSupported_ = false;
  bool validationEnabled_ = false;

  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkFormat swapFormat_ = VK_FORMAT_UNDEFINED;
  VkExtent2D swapExtent_{};
  bool swapTransferSrc_ = false;
  std::vector<VkImage> swapImages_;
  std::vector<VkImageView> swapViews_;
  std::vector<VkSemaphore> renderFinished_;  // one per swapchain image

  std::array<FrameSync, kFramesInFlight> frames_{};
  std::uint32_t frameIndex_ = 0;
  VkCommandPool immediatePool_ = VK_NULL_HANDLE;
  VkFence immediateFence_ = VK_NULL_HANDLE;
};

}  // namespace fh
