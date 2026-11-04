#include "engine/render/vk_context.h"

#include <algorithm>
#include <atomic>
#include <cstring>

#include "engine/core/log.h"
#include "engine/platform/window.h"

namespace fh {

namespace vk {
const char* resultName(VkResult r) {
  switch (r) {
#define FH_CASE(x) case x: return #x
    FH_CASE(VK_SUCCESS); FH_CASE(VK_NOT_READY); FH_CASE(VK_TIMEOUT); FH_CASE(VK_INCOMPLETE);
    FH_CASE(VK_ERROR_OUT_OF_HOST_MEMORY); FH_CASE(VK_ERROR_OUT_OF_DEVICE_MEMORY);
    FH_CASE(VK_ERROR_INITIALIZATION_FAILED); FH_CASE(VK_ERROR_DEVICE_LOST);
    FH_CASE(VK_ERROR_LAYER_NOT_PRESENT); FH_CASE(VK_ERROR_EXTENSION_NOT_PRESENT);
    FH_CASE(VK_ERROR_FEATURE_NOT_PRESENT); FH_CASE(VK_ERROR_INCOMPATIBLE_DRIVER);
    FH_CASE(VK_ERROR_SURFACE_LOST_KHR); FH_CASE(VK_SUBOPTIMAL_KHR); FH_CASE(VK_ERROR_OUT_OF_DATE_KHR);
#undef FH_CASE
    default: return "VK_ERROR_<other>";
  }
}
}  // namespace vk

namespace {
std::atomic<int> g_validationMessages{0};

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT type,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
  if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    // Only the validation layer judges the game. General messages come from the loader, for example
    // about third-party layers installed on the player's PC (overlays, injectors): log them, but
    // they say nothing about our API use.
    if (!(type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)) {
      log::info("vulkan", "{}", data->pMessage);
      return VK_FALSE;
    }
    ++g_validationMessages;
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) log::error("vulkan", "{}", data->pMessage);
    else log::warn("vulkan", "{}", data->pMessage);
  }
  return VK_FALSE;
}

bool hasLayer(const char* name) {
  std::uint32_t n = 0;
  vkEnumerateInstanceLayerProperties(&n, nullptr);
  std::vector<VkLayerProperties> layers(n);
  vkEnumerateInstanceLayerProperties(&n, layers.data());
  return std::any_of(layers.begin(), layers.end(), [&](auto& l) { return std::strcmp(l.layerName, name) == 0; });
}
}  // namespace

int VkContext::validationMessageCount() { return g_validationMessages.load(); }

VkContext::VkContext(Window& window, const VkContextOptions& options) : window_(window), options_(options) {
  createInstance();
  surface_ = window_.createSurface(instance_);
  pickPhysicalDevice();
  createDevice();
  createSwapchain();

  for (auto& f : frames_) {
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = queueFamily_;
    VK_CHECK(vkCreateCommandPool(device_, &pci, nullptr, &f.pool));
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = f.pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(device_, &ai, &f.cmd));
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VK_CHECK(vkCreateSemaphore(device_, &si, nullptr, &f.imageAvailable));
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VK_CHECK(vkCreateFence(device_, &fi, nullptr, &f.inFlight));
  }
  VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  pci.queueFamilyIndex = queueFamily_;
  VK_CHECK(vkCreateCommandPool(device_, &pci, nullptr, &immediatePool_));
  VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  VK_CHECK(vkCreateFence(device_, &fi, nullptr, &immediateFence_));
}

VkContext::~VkContext() {
  if (device_) vkDeviceWaitIdle(device_);
  for (auto& f : frames_) {
    vkDestroyFence(device_, f.inFlight, nullptr);
    vkDestroySemaphore(device_, f.imageAvailable, nullptr);
    vkDestroyCommandPool(device_, f.pool, nullptr);
  }
  vkDestroyFence(device_, immediateFence_, nullptr);
  vkDestroyCommandPool(device_, immediatePool_, nullptr);
  destroySwapchain();
  vkDestroyDevice(device_, nullptr);
  vkDestroySurfaceKHR(instance_, surface_, nullptr);
  if (messenger_) {
    auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
    if (destroy) destroy(instance_, messenger_, nullptr);
  }
  vkDestroyInstance(instance_, nullptr);
}

void VkContext::createInstance() {
  std::uint32_t apiVersion = 0;
  vkEnumerateInstanceVersion(&apiVersion);
  if (apiVersion < VK_API_VERSION_1_3)
    throw std::runtime_error("Vulkan 1.3 loader required (found " + std::to_string(VK_API_VERSION_MAJOR(apiVersion)) +
                             "." + std::to_string(VK_API_VERSION_MINOR(apiVersion)) + ")");

  std::vector<const char*> exts = window_.requiredVulkanInstanceExtensions();
  std::vector<const char*> layers;
  if (options_.validation) {
    if (hasLayer("VK_LAYER_KHRONOS_validation")) {
      layers.push_back("VK_LAYER_KHRONOS_validation");
      exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
      validationEnabled_ = true;
    } else {
      log::warn("vulkan", "validation requested but VK_LAYER_KHRONOS_validation is not installed");
    }
  }

  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "FARHORIZON";
  app.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
  app.pEngineName = "FARHORIZON engine";
  app.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
  app.apiVersion = VK_API_VERSION_1_3;

  VkDebugUtilsMessengerCreateInfoEXT dbg{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
  dbg.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  dbg.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  dbg.pfnUserCallback = debugCallback;

  VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ci.pApplicationInfo = &app;
  ci.enabledExtensionCount = static_cast<std::uint32_t>(exts.size());
  ci.ppEnabledExtensionNames = exts.data();
  ci.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
  ci.ppEnabledLayerNames = layers.data();
  if (validationEnabled_) ci.pNext = &dbg;
  VK_CHECK(vkCreateInstance(&ci, nullptr, &instance_));

  if (validationEnabled_) {
    auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
    if (create) VK_CHECK(create(instance_, &dbg, nullptr, &messenger_));
    log::info("vulkan", "validation layer enabled");
  }
}

void VkContext::pickPhysicalDevice() {
  std::uint32_t n = 0;
  vkEnumeratePhysicalDevices(instance_, &n, nullptr);
  std::vector<VkPhysicalDevice> devices(n);
  vkEnumeratePhysicalDevices(instance_, &n, devices.data());

  int bestScore = -1;
  for (VkPhysicalDevice pd : devices) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(pd, &props);
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &f13;
    vkGetPhysicalDeviceFeatures2(pd, &f2);
    const bool versionOk = props.apiVersion >= VK_API_VERSION_1_3;
    const bool featuresOk = f13.dynamicRendering && f13.synchronization2;

    std::uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qprops(qn);
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, qprops.data());
    int family = -1;
    for (std::uint32_t i = 0; i < qn; ++i) {
      VkBool32 present = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface_, &present);
      if ((qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
        family = static_cast<int>(i);
        break;
      }
    }
    log::info("vulkan", "candidate GPU '{}' api {}.{} dynamicRendering={} sync2={} queue={}", props.deviceName,
              VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion),
              bool(f13.dynamicRendering), bool(f13.synchronization2), family);
    if (!versionOk || !featuresOk || family < 0) continue;

    int score = 1;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score = 100;
    else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score = 50;
    if (score > bestScore) {
      bestScore = score;
      physical_ = pd;
      queueFamily_ = static_cast<std::uint32_t>(family);
      deviceName_ = props.deviceName;
      timestampPeriod_ = props.limits.timestampPeriod;
      timestampsSupported_ = qprops[size_t(family)].timestampValidBits > 0 && props.limits.timestampPeriod > 0;
    }
  }
  if (!physical_)
    throw std::runtime_error("no GPU with Vulkan 1.3 + dynamicRendering + synchronization2 + presentation found");
  log::info("vulkan", "selected GPU '{}' (GPU timestamps: {})", deviceName_, timestampsSupported_ ? "yes" : "no");
}

void VkContext::createDevice() {
  const float priority = 1.0f;
  VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qci.queueFamilyIndex = queueFamily_;
  qci.queueCount = 1;
  qci.pQueuePriorities = &priority;

  VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  f13.dynamicRendering = VK_TRUE;
  f13.synchronization2 = VK_TRUE;
  f13.shaderDemoteToHelperInvocation = VK_TRUE;  // SPIR-V 1.6 `discard` (core and mandatory in 1.3)
  f13.shaderDemoteToHelperInvocation = VK_TRUE;  // SPIR-V 1.6 `discard` (core and mandatory in 1.3)
  VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  f2.pNext = &f13;

  const char* exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  ci.pNext = &f2;
  ci.queueCreateInfoCount = 1;
  ci.pQueueCreateInfos = &qci;
  ci.enabledExtensionCount = 1;
  ci.ppEnabledExtensionNames = exts;
  VK_CHECK(vkCreateDevice(physical_, &ci, nullptr, &device_));
  vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
}

void VkContext::createSwapchain() {
  VkSurfaceCapabilitiesKHR caps;
  VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps));

  std::uint32_t n = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &n, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(n);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &n, formats.data());
  VkSurfaceFormatKHR chosen = formats.at(0);
  for (const auto& f : formats)
    if ((f.format == VK_FORMAT_B8G8R8A8_SRGB || f.format == VK_FORMAT_R8G8B8A8_SRGB) &&
        f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      chosen = f;
      break;
    }

  vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &n, nullptr);
  std::vector<VkPresentModeKHR> modes(n);
  vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &n, modes.data());
  VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;  // always available, vsync
  if (!options_.vsync) {
    for (auto m : modes)
      if (m == VK_PRESENT_MODE_MAILBOX_KHR) mode = m;
    for (auto m : modes)
      if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) mode = m;
  }

  int w = 0, h = 0;
  window_.pixelSize(w, h);
  VkExtent2D extent = caps.currentExtent;
  if (extent.width == UINT32_MAX) {
    extent.width = std::clamp(static_cast<std::uint32_t>(w), caps.minImageExtent.width, caps.maxImageExtent.width);
    extent.height = std::clamp(static_cast<std::uint32_t>(h), caps.minImageExtent.height, caps.maxImageExtent.height);
  }
  std::uint32_t imageCount = caps.minImageCount + 1;
  if (caps.maxImageCount > 0) imageCount = std::min(imageCount, caps.maxImageCount);

  swapTransferSrc_ = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
  VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  ci.surface = surface_;
  ci.minImageCount = imageCount;
  ci.imageFormat = chosen.format;
  ci.imageColorSpace = chosen.colorSpace;
  ci.imageExtent = extent;
  ci.imageArrayLayers = 1;
  ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (swapTransferSrc_ ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
  ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ci.preTransform = caps.currentTransform;
  ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  ci.presentMode = mode;
  ci.clipped = VK_TRUE;
  VK_CHECK(vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_));
  swapFormat_ = chosen.format;
  swapExtent_ = extent;

  vkGetSwapchainImagesKHR(device_, swapchain_, &n, nullptr);
  swapImages_.resize(n);
  vkGetSwapchainImagesKHR(device_, swapchain_, &n, swapImages_.data());
  for (VkImage img : swapImages_) {
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = img;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = swapFormat_;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView view;
    VK_CHECK(vkCreateImageView(device_, &vi, nullptr, &view));
    swapViews_.push_back(view);
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore sem;
    VK_CHECK(vkCreateSemaphore(device_, &si, nullptr, &sem));
    renderFinished_.push_back(sem);
  }
  log::info("vulkan", "swapchain {}x{} images={} format={} present={}", extent.width, extent.height, n,
            int(swapFormat_), mode == VK_PRESENT_MODE_FIFO_KHR ? "FIFO(vsync)" : "no-vsync");
}

void VkContext::destroySwapchain() {
  for (auto v : swapViews_) vkDestroyImageView(device_, v, nullptr);
  for (auto s : renderFinished_) vkDestroySemaphore(device_, s, nullptr);
  swapViews_.clear();
  renderFinished_.clear();
  swapImages_.clear();
  if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
  swapchain_ = VK_NULL_HANDLE;
}

void VkContext::recreateSwapchain() {
  vkDeviceWaitIdle(device_);
  destroySwapchain();
  createSwapchain();
}

void VkContext::setVsync(bool on) {
  if (options_.vsync == on) return;
  options_.vsync = on;
  recreateSwapchain();
}

bool VkContext::beginFrame(std::uint32_t& imageIndex) {
  FrameSync& f = frames_[frameIndex_];
  VK_CHECK(vkWaitForFences(device_, 1, &f.inFlight, VK_TRUE, UINT64_MAX));
  const VkResult r = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, f.imageAvailable, VK_NULL_HANDLE, &imageIndex);
  if (r == VK_ERROR_OUT_OF_DATE_KHR) {
    recreateSwapchain();
    return false;
  }
  if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) VK_CHECK(r);
  VK_CHECK(vkResetFences(device_, 1, &f.inFlight));
  VK_CHECK(vkResetCommandBuffer(f.cmd, 0));
  return true;
}

void VkContext::endFrame(std::uint32_t imageIndex) {
  FrameSync& f = frames_[frameIndex_];
  VkCommandBufferSubmitInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
  cbi.commandBuffer = f.cmd;
  VkSemaphoreSubmitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
  waitInfo.semaphore = f.imageAvailable;
  waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSemaphoreSubmitInfo signalInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
  signalInfo.semaphore = renderFinished_[imageIndex];
  signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
  VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
  si.waitSemaphoreInfoCount = 1;
  si.pWaitSemaphoreInfos = &waitInfo;
  si.commandBufferInfoCount = 1;
  si.pCommandBufferInfos = &cbi;
  si.signalSemaphoreInfoCount = 1;
  si.pSignalSemaphoreInfos = &signalInfo;
  VK_CHECK(vkQueueSubmit2(queue_, 1, &si, f.inFlight));

  VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  pi.waitSemaphoreCount = 1;
  pi.pWaitSemaphores = &renderFinished_[imageIndex];
  pi.swapchainCount = 1;
  pi.pSwapchains = &swapchain_;
  pi.pImageIndices = &imageIndex;
  const VkResult r = vkQueuePresentKHR(queue_, &pi);
  frameIndex_ = (frameIndex_ + 1) % kFramesInFlight;
  if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) recreateSwapchain();
  else if (r != VK_SUCCESS) VK_CHECK(r);
}

void VkContext::immediateSubmit(const std::function<void(VkCommandBuffer)>& record) {
  VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ai.commandPool = immediatePool_;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = 1;
  VkCommandBuffer cmd;
  VK_CHECK(vkAllocateCommandBuffers(device_, &ai, &cmd));
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
  record(cmd);
  VK_CHECK(vkEndCommandBuffer(cmd));
  VkCommandBufferSubmitInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
  cbi.commandBuffer = cmd;
  VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
  si.commandBufferInfoCount = 1;
  si.pCommandBufferInfos = &cbi;
  VK_CHECK(vkQueueSubmit2(queue_, 1, &si, immediateFence_));
  VK_CHECK(vkWaitForFences(device_, 1, &immediateFence_, VK_TRUE, UINT64_MAX));
  VK_CHECK(vkResetFences(device_, 1, &immediateFence_));
  vkFreeCommandBuffers(device_, immediatePool_, 1, &cmd);
}

std::uint32_t VkContext::findMemoryType(std::uint32_t typeBits, VkMemoryPropertyFlags props) const {
  VkPhysicalDeviceMemoryProperties mp;
  vkGetPhysicalDeviceMemoryProperties(physical_, &mp);
  for (std::uint32_t i = 0; i < mp.memoryTypeCount; ++i)
    if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
  throw std::runtime_error("no suitable Vulkan memory type");
}

}  // namespace fh
