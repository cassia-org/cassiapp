#pragma once

#include <vulkan/vulkan_raii.hpp>
#include <cstdint>

namespace vk_helpers {
vk::raii::Instance CreateInstance(vk::ApplicationInfo applicationInfo, bool enableValidation, const vk::raii::Context &context);

vk::raii::DebugReportCallbackEXT CreateDebugReportCallback(const vk::raii::Instance &instance);

vk::raii::PhysicalDevice CreatePhysicalDevice(const vk::raii::Instance &instance);

vk::raii::Device CreateDevice(const vk::raii::Context &context, const vk::raii::PhysicalDevice &physicalDevice,
                              uint32_t &vkQueueFamilyIndex);

vk::raii::Image CreateSwapchainImage(const vk::raii::Device &device, vk::Format format, vk::Extent2D extent,
                                     vk::ImageUsageFlags usage);
}