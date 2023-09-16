#include <cstdio>
#include <exception>
#include <stdexcept>
#include <array>
#include <span>
#include <vector>
#include <limits>
#include <thread>
#include <optional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <android/native_window.h>
#include <android/hardware_buffer.h>
#include <unordered_map>
#include <unistd.h>
#include <vulkan/vulkan_raii.hpp>
#include <dlfcn.h>

#include "log.h"
#include "vk_helpers.h"
#include "ipc_types.h"
#include "nekomposite.h"

namespace cassiasrv {

static PFN_vkGetInstanceProcAddr LoadVulkanDriver() {
    void *libvulkanHandle{dlopen("libvulkan.so", RTLD_NOW)};
    return reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(libvulkanHandle, "vkGetInstanceProcAddr"));
}

void Compositor::RecreateAndroidSwapchain(ANativeWindow *window) {
    {
        std::scoped_lock lock{surfaceMutex};
        vkSurface = vkInstance.createAndroidSurfaceKHR({.window = window});

        auto caps{vkPhysicalDevice.getSurfaceCapabilitiesKHR(**vkSurface)};

        // TODO checking
        vkSwapchain.emplace(vkDevice, vk::SwapchainCreateInfoKHR{
            .surface = **vkSurface,
            .minImageCount = FRAMES_IN_FLIGHT,
            .imageFormat = vk::Format::eR8G8B8A8Unorm,
            .imageColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear,
            .imageExtent = caps.currentExtent,
            .imageArrayLayers = 1,
            .imageUsage = vk::ImageUsageFlagBits::eTransferDst,
            .imageSharingMode = vk::SharingMode::eExclusive,
            .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eInherit,
            .presentMode = vk::PresentModeKHR::eFifo,
            .clipped = true,
        });

        surfaceExtent = caps.currentExtent;

        vkSwapchainImages = vkSwapchain->getImages();
        if (vkSwapchainImages.size() >= MAX_SWAPCHAIN_IMAGES)
            throw std::runtime_error("bad size");

        needSwapchainImageLayoutTransition = true;
    }
    surfaceCv.notify_one();
}

void Compositor::ThreadFunc() {
    vk::raii::CommandPool commandPool{vkDevice, vk::CommandPoolCreateInfo{
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer
    }};

    vk::raii::CommandBuffers commandBuffers{vkDevice, vk::CommandBufferAllocateInfo{
        .commandPool = *commandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = FRAMES_IN_FLIGHT,
    }};

    std::array<vk::raii::Semaphore, MAX_SWAPCHAIN_IMAGES> presentReadySemaphores{
        {vkDevice.createSemaphore({}), vkDevice.createSemaphore({}), vkDevice.createSemaphore({}), vkDevice.createSemaphore({}),
         vkDevice.createSemaphore({}), vkDevice.createSemaphore({})}
    };

    std::array<vk::raii::Semaphore, FRAMES_IN_FLIGHT> clientPresentDoneSemaphores{
        {vkDevice.createSemaphore({}), vkDevice.createSemaphore({}), vkDevice.createSemaphore({})}
    };
    std::array<vk::raii::Semaphore, FRAMES_IN_FLIGHT> imageAcquiredSemaphores{
        {vkDevice.createSemaphore({}), vkDevice.createSemaphore({}), vkDevice.createSemaphore({})}
    };
    std::array<vk::raii::Fence, FRAMES_IN_FLIGHT> compositeDoneFences{{
        vkDevice.createFence({.flags = vk::FenceCreateFlagBits::eSignaled}),
        vkDevice.createFence({.flags = vk::FenceCreateFlagBits::eSignaled}),
        vkDevice.createFence({.flags = vk::FenceCreateFlagBits::eSignaled}),
    }};
    std::vector<vk::Semaphore> waitSemaphores;
    std::vector<VirtualSwapchain::Buffer *> frameUsedBuffers;

    std::size_t frameIndex{0};
    std::size_t frameCount{0};
    std::size_t pastFrameCount{0};

    std::chrono::time_point<std::chrono::steady_clock> lastFrameTime{std::chrono::steady_clock::now()};
    while (true) {
        cv.notify_all();
        waitSemaphores.clear();
        frameUsedBuffers.clear();

        auto &compositeDoneFence{compositeDoneFences[frameIndex]};
        auto &commandBuffer{commandBuffers[frameIndex]};
        auto &clientPresentDoneSemaphore{clientPresentDoneSemaphores[frameIndex]};
        auto &imageAcquireSemaphore{imageAcquiredSemaphores[frameIndex]};

        std::ignore = vkDevice.waitForFences({*compositeDoneFence}, VK_TRUE, std::numeric_limits<uint64_t>::max());
        commandBuffer.begin({});

        std::unique_lock surfaceLock{surfaceMutex};

        if (!vkSwapchain)
            surfaceCv.wait(surfaceLock, [this] { return vkSwapchain.has_value(); });

        auto acquireResult{vkSwapchain->acquireNextImage(std::numeric_limits<uint64_t>::max(),
                                                         *imageAcquireSemaphore, {})};
        if (acquireResult.first != vk::Result::eSuccess)
            throw std::runtime_error("Acquire failed");

        waitSemaphores.push_back(*imageAcquireSemaphore);

        auto &presentReadySemaphore{presentReadySemaphores[acquireResult.second]};
        auto presentTargetImage{vkSwapchainImages[acquireResult.second]};

        if (needSwapchainImageLayoutTransition) {
            std::vector<vk::ImageMemoryBarrier> imageBarriers;
            std::transform(vkSwapchainImages.begin(), vkSwapchainImages.end(),
                           std::back_inserter(imageBarriers), [] (auto image) {
                return vk::ImageMemoryBarrier{
                    .srcAccessMask = {},
                    .dstAccessMask = {},
                    .oldLayout = vk::ImageLayout::eUndefined,
                    .newLayout = vk::ImageLayout::ePresentSrcKHR,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = image,
                    .subresourceRange = {
                        .aspectMask = vk::ImageAspectFlagBits::eColor,
                        .levelCount = 1,
                        .layerCount = 1
                    }
                };
            });
            commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                                          vk::PipelineStageFlagBits::eBottomOfPipe, {},
                                          {}, {}, imageBarriers);

            needSwapchainImageLayoutTransition = false;
        }

        auto currentTime{std::chrono::steady_clock::now()};
        std::scoped_lock lock{mutex};

        for (auto &virtualSwapchainEntry : virtualSwapchains) {
            auto &virtualSwapchain{virtualSwapchainEntry.second};
            if (!virtualSwapchain.queue.empty()) {
                virtualSwapchain.frameCount[0]++;
                if (virtualSwapchain.lastFrameCountRefreshTime + std::chrono::seconds{1} < currentTime) {
                    LOGI("frame rate: %u\n", (virtualSwapchain.frameCount[0] + virtualSwapchain.frameCount[1]) / 2);
                    virtualSwapchain.frameCount[1] = virtualSwapchain.frameCount[0];
                    virtualSwapchain.frameCount[0] = 0;
                    virtualSwapchain.lastFrameCountRefreshTime = currentTime;
                }

                uint32_t idx{virtualSwapchain.queue.front()};
                virtualSwapchain.lastPresentedImageIndex = idx;
                virtualSwapchain.queue.pop();

                auto &buffer{virtualSwapchain.buffers[idx]};
                waitSemaphores.push_back(*buffer.queueSemaphore);
                buffer.state = VirtualSwapchain::Buffer::State::Free;
            }

            auto &buffer{virtualSwapchain.buffers[virtualSwapchain.lastPresentedImageIndex]};
            if (buffer.state != VirtualSwapchain::Buffer::State::Free)
                continue;

            frameUsedBuffers.push_back(&buffer);

            commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,
                                          {}, {vk::MemoryBarrier{
                                              .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
                                              .dstAccessMask = vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eTransferWrite
                                          }}, {}, {});

            vk::Offset3D blitExtent{.x = static_cast<int32_t>(virtualSwapchain.extent.width),
                                    .y = static_cast<int32_t>(virtualSwapchain.extent.height),
                                    .z = 1};
            vk::ImageBlit blit{
                .srcSubresource = {
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
                .srcOffsets{{
                    vk::Offset3D{0,0 ,0},
                    blitExtent
                }},
                .dstSubresource = {
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
                .dstOffsets{{
                    vk::Offset3D{0, 0, 0},
                    vk::Offset3D{static_cast<int32_t>(surfaceExtent.width),
                                 static_cast<int32_t>(surfaceExtent.height),
                                 1}
                }},
            };
            commandBuffer.blitImage(*buffer.image, vk::ImageLayout::eGeneral,
                                    presentTargetImage, vk::ImageLayout::ePresentSrcKHR,
                                    {blit}, vk::Filter::eNearest);

        }

        commandBuffer.end();

        {
            static constexpr vk::PipelineStageFlags SUBMIT_STAGE_MASK{vk::PipelineStageFlagBits::eAllCommands};

            std::array<vk::Semaphore, 2> signalSemaphores{{
                *clientPresentDoneSemaphore,
                *presentReadySemaphore
            }};
            std::scoped_lock queueLock{queueMutex};
            vk::SubmitInfo submit{
                .waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size()),
                .pWaitSemaphores = waitSemaphores.data(),
                .pWaitDstStageMask = &SUBMIT_STAGE_MASK,
                .commandBufferCount = 1,
                .pCommandBuffers = &(*commandBuffer),
                .signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size()),
                .pSignalSemaphores = signalSemaphores.data(),
            };
            vkQueue.submit({submit}, *compositeDoneFence);
            std::ignore = vkQueue.presentKHR({
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = &(*presentReadySemaphore),
                .swapchainCount = 1,
                .pSwapchains = &(**vkSwapchain),
                .pImageIndices = &acquireResult.second
            });
        }

        int clientPresentDoneSemaphoreFd{vkDevice.getSemaphoreFdKHR(vk::SemaphoreGetFdInfoKHR{
            .semaphore = *clientPresentDoneSemaphore,
            .handleType = vk::ExternalSemaphoreHandleTypeFlagBits::eSyncFd
        })};

        // QCOM BUG?
        if (clientPresentDoneSemaphoreFd == 0)
            clientPresentDoneSemaphoreFd = -1;

        for (auto &buffer : frameUsedBuffers) {
            close(buffer->acquireFence);
            buffer->acquireFence = dup(clientPresentDoneSemaphoreFd);
        }

        close(clientPresentDoneSemaphoreFd);


        frameIndex = (frameIndex + 1) % FRAMES_IN_FLIGHT;
    }
}

static AHardwareBuffer_Format VkFormatToHardwareBuffer(vk::Format format) {
    switch (format) {
        case vk::Format::eR8G8B8A8Unorm:
        case vk::Format::eR8G8B8A8Srgb:
        case vk::Format::eB8G8R8A8Unorm:
        case vk::Format::eB8G8R8A8Srgb:
            return AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
        case vk::Format::eR8G8B8Unorm:
            return AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM;
        case vk::Format::eR5G6B5UnormPack16:
            return AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM;
        case vk::Format::eR16G16B16A16Sfloat:
            return AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT;
        case vk::Format::eA2R10G10B10UnormPack32:
            return AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM;
        case vk::Format::eR8Unorm:
            return AHARDWAREBUFFER_FORMAT_R8_UNORM;
        default:
            throw std::runtime_error("Unsupported format");
    }
}

void Compositor::AllocateSwapchain(cassia_compositor_command_allocate_swapchain command, int sockFd,
                                   cassia_compositor_command_allocate_swapchain_response &response) {
    auto handle{nextVirtualSwapchainHandle++};
    LOGI("allocateSwapchain: handle: %d image_count: %u, width: %u height: %u\n", handle, command.image_count, command.extent.width, command.extent.height);

    {
        std::scoped_lock lock{mutex};
        vk::Extent2D extent{.width = command.extent.width, .height = command.extent.height};
        auto virtualSwapchain{virtualSwapchains.emplace(handle, VirtualSwapchain{extent}).first};

        for (uint32_t i{}; i < command.image_count; i++) {
            auto image{vk_helpers::CreateSwapchainImage(vkDevice, static_cast<vk::Format>(command.format), extent,
                                                                vk::ImageUsageFlags{command.usage})};

            AHardwareBuffer *backing;
            AHardwareBuffer_Desc desc{
                .width = command.extent.width,
                .height = command.extent.height,
                .layers = 1,
                .format = VkFormatToHardwareBuffer(static_cast<vk::Format>(command.format)),
                .usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT | AHARDWAREBUFFER_USAGE_CPU_READ_NEVER | AHARDWAREBUFFER_USAGE_CPU_WRITE_NEVER,
            };
            AHardwareBuffer_allocate(&desc, &backing);

            AHardwareBuffer_sendHandleToUnixSocket(backing, sockFd);
            vk::MemoryRequirements requirements{image.getMemoryRequirements()};

            vk::ImportAndroidHardwareBufferInfoANDROID importInfo{
                .buffer = backing,
            };

            auto deviceMemory{vkDevice.allocateMemory(vk::MemoryAllocateInfo{
                .pNext = &importInfo,
                .allocationSize = requirements.size,
                .memoryTypeIndex = 0, // TODO FIXME USE opaque fd
            })};

            image.bindMemory(*deviceMemory, 0);
            virtualSwapchain->second.buffers.push_back({std::move(image), std::move(deviceMemory), backing, vkDevice.createSemaphore({}), VirtualSwapchain::Buffer::State::Free, -1});
        }

    }
    response.result = VK_SUCCESS;
    response.handle = handle;
}

void Compositor::Dequeue(cassia_compositor_command_dequeue command,
                         cassia_compositor_command_dequeue_response &response, int &acquireFence) {

    std::unique_lock lock{mutex};
    auto &virtualSwapchain{virtualSwapchains.at(command.handle)};

    decltype(virtualSwapchain.buffers)::iterator bufferIt{};
    auto pred{[&]() {
        static size_t bufferIdx{0};
        bufferIdx = (bufferIdx + 1) % virtualSwapchain.buffers.size();
        bufferIt = std::next(virtualSwapchain.buffers.begin(), bufferIdx);
        if (bufferIt->state == VirtualSwapchain::Buffer::State::Free)
            return true;

        bufferIt = std::find_if(virtualSwapchain.buffers.begin(), virtualSwapchain.buffers.end(), [](const auto &buffer) {
            return buffer.state == VirtualSwapchain::Buffer::State::Free;
        });

        return bufferIt != virtualSwapchain.buffers.end();
    }};
    if (command.timeout >= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        cv.wait(lock, pred);
    } else {
        auto timeout{std::chrono::nanoseconds{command.timeout}};
        if (!cv.wait_for(lock, timeout, pred)) {
            response.result = VK_TIMEOUT;
            bufferIt->acquireFence = -1;
            return;
        }
    }


    bufferIt->state = VirtualSwapchain::Buffer::State::Dequeued;
    response.result = VK_SUCCESS;
    response.image_index = std::distance(virtualSwapchain.buffers.begin(), bufferIt);
    acquireFence = bufferIt->acquireFence;
    bufferIt->acquireFence = -1;
}

void Compositor::Queue(cassia_compositor_command_queue command, int queueSemaphore,
                       cassia_compositor_command_queue_response &response) {

    std::scoped_lock lock{mutex};
    auto &virtualSwapchain{virtualSwapchains.at(command.handle)};
    auto &buffer{virtualSwapchain.buffers.at(command.image_index)};
    buffer.state = VirtualSwapchain::Buffer::State::Queued;
    vkDevice.importSemaphoreFdKHR(vk::ImportSemaphoreFdInfoKHR{
        .semaphore = *buffer.queueSemaphore,
        .flags = vk::SemaphoreImportFlagBits::eTemporary,
        .handleType = vk::ExternalSemaphoreHandleTypeFlagBits::eSyncFd,
        .fd = queueSemaphore,
    });

    virtualSwapchain.queue.emplace(command.image_index);

    response.result = VK_SUCCESS;
}

Compositor::Compositor()
    : vkContext{LoadVulkanDriver()},
      vkInstance{vk_helpers::CreateInstance(vk::ApplicationInfo{
        .pApplicationName = "Cassia Compositor",
        .applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .pEngineName = "nekomposite",
        .engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .apiVersion = VK_API_VERSION_1_1,
      }, false, vkContext)},
      vkDebugReportCallback{vk_helpers::CreateDebugReportCallback(vkInstance)},
      vkPhysicalDevice{vk_helpers::CreatePhysicalDevice(vkInstance)},
      vkDevice{vk_helpers::CreateDevice(vkContext, vkPhysicalDevice, queueFamilyIndex)},
      vkQueue{vkDevice, queueFamilyIndex, 0},
      thread{&Compositor::ThreadFunc, this} {}

cassia_command_info Compositor::Dispatch(cassia_compositor_command_header header, int sockFd,
                                         std::span<char> recvData, std::span<int> recvFds,
                                         std::span<char> sendData, std::span<int> sendFds) {

    switch (header.type) {
        case CASSIA_COMPOSITOR_COMMAND_TYPE_ALLOCATE_SWAPCHAIN:
            AllocateSwapchain(*reinterpret_cast<cassia_compositor_command_allocate_swapchain *>(recvData.data()), sockFd,
                              *reinterpret_cast<cassia_compositor_command_allocate_swapchain_response *>(sendData.data()));
            return {sizeof(cassia_compositor_command_allocate_swapchain_response), 0};
        case CASSIA_COMPOSITOR_COMMAND_TYPE_DEQUEUE:
            Dequeue(*reinterpret_cast<cassia_compositor_command_dequeue *>(recvData.data()),
                    *reinterpret_cast<cassia_compositor_command_dequeue_response *>(sendData.data()), sendFds[0]);
            return {sizeof(cassia_compositor_command_dequeue_response), 1};
        case CASSIA_COMPOSITOR_COMMAND_TYPE_QUEUE:
            Queue(*reinterpret_cast<cassia_compositor_command_queue *>(recvData.data()), recvFds[0],
                  *reinterpret_cast<cassia_compositor_command_queue_response *>(sendData.data()));
            return {sizeof(cassia_compositor_command_queue_response), 0};
        default:
            throw std::runtime_error("Unexpected command!");
    }
}

}
