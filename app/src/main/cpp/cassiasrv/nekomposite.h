#pragma once

#include <array>
#include <span>
#include <vector>
#include <thread>
#include <optional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <android/native_window.h>
#include <map>
#include <vulkan/vulkan_raii.hpp>

#include "ipc_types.h"

namespace cassiasrv {

class Compositor {
private:
    static constexpr uint32_t MAX_SWAPCHAIN_IMAGES{6};
    static constexpr uint32_t FRAMES_IN_FLIGHT{3};

    vk::raii::Context vkContext;
    vk::raii::Instance vkInstance;
    vk::raii::DebugReportCallbackEXT vkDebugReportCallback;
    vk::raii::PhysicalDevice vkPhysicalDevice;
    vk::raii::Device vkDevice;
    std::mutex queueMutex;
    uint32_t queueFamilyIndex{};
    vk::raii::Queue vkQueue;
    std::optional<vk::raii::SurfaceKHR> vkSurface;
    vk::Extent2D surfaceExtent;
    std::condition_variable surfaceCv;
    std::mutex surfaceMutex;
    std::optional<vk::raii::SwapchainKHR> vkSwapchain;
    std::vector<vk::Image> vkSwapchainImages;
    bool needSwapchainImageLayoutTransition{};

    std::condition_variable cv;
    std::mutex mutex;

    struct VirtualSwapchain {
        struct Buffer {
            enum class State {
                Free,
                Dequeued,
                Queued
            };

            vk::raii::Image image;
            vk::raii::DeviceMemory memory;
            AHardwareBuffer *hwb;
            vk::raii::Semaphore queueSemaphore;
            State state{State::Free};
            int acquireFence{-1};
        };

        vk::Extent2D extent;
        std::vector<Buffer> buffers;
        std::queue<uint32_t> queue;
        uint32_t lastPresentedImageIndex{};
        std::chrono::time_point<std::chrono::steady_clock> lastFrameCountRefreshTime{};
        uint32_t frameCount[2]{};
    };

    cassia_compositor_swapchain_handle nextVirtualSwapchainHandle{1};
    std::map<cassia_compositor_swapchain_handle, VirtualSwapchain> virtualSwapchains;

    std::thread thread;

    void ThreadFunc();

    void AllocateSwapchain(cassia_compositor_command_allocate_swapchain command, int sockFd,
                           cassia_compositor_command_allocate_swapchain_response &response);


    void Dequeue(cassia_compositor_command_dequeue command,
                 cassia_compositor_command_dequeue_response &response, int &acquireFence);


    void Queue(cassia_compositor_command_queue command, int queueSemaphore,
               cassia_compositor_command_queue_response &response);
public:
    Compositor();

    void RecreateAndroidSwapchain(ANativeWindow *window);

    cassia_command_info Dispatch(cassia_compositor_command_header header, int sockFd,
                                 std::span<char> recvData, std::span<int> recvFds,
                                 std::span<char> sendData, std::span<int> sendFds);
};

}