#pragma once
#include <stdexcept>
#include <string>

#include <vulkan/vulkan.h>

namespace fh::vk {

const char* resultName(VkResult r);

}  // namespace fh::vk

// Fatal Vulkan error check for initialisation/creation paths. Throws with call-site context.
#define VK_CHECK(expr)                                                                         \
  do {                                                                                         \
    const VkResult fh_vk_r = (expr);                                                           \
    if (fh_vk_r != VK_SUCCESS)                                                                 \
      throw std::runtime_error(std::string(#expr) + " failed: " + ::fh::vk::resultName(fh_vk_r) + \
                               " (" __FILE__ ":" + std::to_string(__LINE__) + ")");           \
  } while (0)
