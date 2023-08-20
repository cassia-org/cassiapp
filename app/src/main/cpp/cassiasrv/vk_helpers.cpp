#include <vector>
#include <string>
#include <string_view>
#include <stdexcept>
#include <dlfcn.h>
#include <algorithm>
#include <string_view>
#include <cstdint>
#include <vulkan/vulkan_raii.hpp>

namespace vk_helpers {
vk::raii::Instance CreateInstance(vk::ApplicationInfo applicationInfo, bool enableValidation, const vk::raii::Context &context) {
    std::vector<const char *> requiredLayers{};
    if (enableValidation)
        requiredLayers.push_back("VK_LAYER_KHRONOS_validation");

    auto instanceLayers{context.enumerateInstanceLayerProperties()};
    if (std::any_of(requiredLayers.begin(), requiredLayers.end(), [&](const char *requiredLayer) {
        return std::none_of(instanceLayers.begin(), instanceLayers.end(), [&](const vk::LayerProperties &instanceLayer) {
            return std::string_view{instanceLayer.layerName} == std::string_view{requiredLayer};
    }); }))
        throw std::runtime_error("Required Vulkan layers are not available");

    constexpr std::array<const char *, 3> requiredInstanceExtensions{
        VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
    };

    auto instanceExtensions{context.enumerateInstanceExtensionProperties()};
    if (std::any_of(requiredInstanceExtensions.begin(), requiredInstanceExtensions.end(), [&](const char *requiredInstanceExtension) {
        return std::none_of(instanceExtensions.begin(), instanceExtensions.end(), [&](const vk::ExtensionProperties &instanceExtension) {
            return std::string_view{instanceExtension.extensionName} == std::string_view{requiredInstanceExtension};
    }); }))
        throw std::runtime_error("Required Vulkan instance extensions are not available");

    return vk::raii::Instance{context, vk::InstanceCreateInfo{
        .pApplicationInfo = &applicationInfo,
        .enabledLayerCount = static_cast<uint32_t>(requiredLayers.size()),
        .ppEnabledLayerNames = requiredLayers.data(),
        .enabledExtensionCount = requiredInstanceExtensions.size(),
        .ppEnabledExtensionNames = requiredInstanceExtensions.data(),
    }};
}

static VkBool32 VKAPI_ATTR DebugCallback(vk::DebugReportFlagsEXT flags, vk::DebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode,
                                const char *layerPrefix, const char *messageCStr, void *userData) {
    printf("DebugCallback: %s\n", messageCStr);
    return VK_FALSE;
}

vk::raii::DebugReportCallbackEXT CreateDebugReportCallback(const vk::raii::Instance &instance) {
    return vk::raii::DebugReportCallbackEXT(instance, vk::DebugReportCallbackCreateInfoEXT{
        .flags = vk::DebugReportFlagBitsEXT::eError | vk::DebugReportFlagBitsEXT::eWarning | vk::DebugReportFlagBitsEXT::ePerformanceWarning | vk::DebugReportFlagBitsEXT::eInformation | vk::DebugReportFlagBitsEXT::eDebug,
        .pfnCallback = reinterpret_cast<PFN_vkDebugReportCallbackEXT>(&DebugCallback),
    });
}

vk::raii::PhysicalDevice CreatePhysicalDevice(const vk::raii::Instance &instance) {
    auto devices{vk::raii::PhysicalDevices(instance)};
    if (devices.empty())
        throw std::runtime_error("No Vulkan physical devices found");
    return std::move(devices.front()); // We just select the first device as we aren't expecting multiple GPUs
}

vk::raii::Device CreateDevice(const vk::raii::Context &context,
                                        const vk::raii::PhysicalDevice &physicalDevice,
                                        uint32_t &vkQueueFamilyIndex) {
    auto deviceFeatures2{physicalDevice.getFeatures2()};
    decltype(deviceFeatures2) enabledFeatures2{}; // We only want to enable features we required due to potential overhead from unused features

    auto deviceExtensions{physicalDevice.enumerateDeviceExtensionProperties()};
    std::vector<std::array<char, VK_MAX_EXTENSION_NAME_SIZE>> enabledExtensions{
        {
                {VK_KHR_SWAPCHAIN_EXTENSION_NAME},
                {VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME},
                {VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME},
                {VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME},
                {VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME},
        }
    };

    for (const auto &requiredExtension : enabledExtensions) {
        if (std::none_of(deviceExtensions.begin(), deviceExtensions.end(), [&](const vk::ExtensionProperties &deviceExtension) {
            return std::string_view{deviceExtension.extensionName} == std::string_view{requiredExtension.data()};
        }))
            throw std::runtime_error("Cannot find Vulkan device extension!");
    }

    auto deviceProperties2{physicalDevice.getProperties2()};

    std::vector<const char *> pEnabledExtensions;
    pEnabledExtensions.reserve(enabledExtensions.size());
    for (auto &extension : enabledExtensions)
        pEnabledExtensions.push_back(extension.data());

    auto queueFamilies{physicalDevice.getQueueFamilyProperties()};
    float queuePriority{1.0f}; //!< The priority of the only queue we use, it's set to the maximum of 1.0
    vk::DeviceQueueCreateInfo queueCreateInfo{[&] {
        uint32_t index{};
        for (const auto &queueFamily : queueFamilies) {
            if (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics && queueFamily.queueFlags & vk::QueueFlagBits::eCompute) {
                vkQueueFamilyIndex = index;
                return vk::DeviceQueueCreateInfo{
                    .queueFamilyIndex = index,
                    .queueCount = 1,
                    .pQueuePriorities = &queuePriority,
                };
            }
            index++;
        }
        throw std::runtime_error("Cannot find a queue family with both eGraphics and eCompute bits set");
    }()};


    return vk::raii::Device{physicalDevice, vk::DeviceCreateInfo{
        .pNext = &enabledFeatures2,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCreateInfo,
        .enabledExtensionCount = static_cast<uint32_t>(pEnabledExtensions.size()),
        .ppEnabledExtensionNames = pEnabledExtensions.data(),
    }};
}

vk::raii::Image CreateSwapchainImage(const vk::raii::Device &device, vk::Format format, vk::Extent2D extent, vk::ImageUsageFlags usage) {
    vk::ExternalMemoryImageCreateInfo externalMemoryInfo{
            .handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eAndroidHardwareBufferANDROID,
    };
    return device.createImage(vk::ImageCreateInfo{
            .pNext = &externalMemoryInfo,
            .imageType = vk::ImageType::e2D,
            .format = format,
            .extent = { .width = extent.width, .height = extent.height, .depth = 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = usage | vk::ImageUsageFlagBits::eTransferSrc,
            .sharingMode = vk::SharingMode::eExclusive,
            .initialLayout = vk::ImageLayout::eUndefined
    });
}
}
