#ifndef __IPC_TYPES_H
#define __IPC_TYPES_H

#include <vulkan/vulkan.h>
#include <stdint.h>

#define CASSIA_SOCKET_ENV "CASSIA_SOCK"
#define CASSIA_MAX_COMMAND_SIZE 0x200
#define CASSIA_MAX_COMMAND_RESPONSE_SIZE 0x100
#define CASSIA_MAX_COMMAND_FD_COUNT 16

enum cassia_command_class {
    CASSIA_COMMAND_CLASS_COMPOSITOR,
    CASSIA_COMMAND_CLASS_MAX
};

struct cassia_command_header {
    enum cassia_command_class target_class;
};

enum cassia_compositor_command_type {
    CASSIA_COMPOSITOR_COMMAND_TYPE_ALLOCATE_SWAPCHAIN,
    CASSIA_COMPOSITOR_COMMAND_TYPE_DEQUEUE,
    CASSIA_COMPOSITOR_COMMAND_TYPE_QUEUE,
};

struct cassia_compositor_command_header {
    struct cassia_command_header header;
    enum cassia_compositor_command_type type;
};

typedef int cassia_window_handle;

struct cassia_compositor_command_allocate_swapchain {
    struct cassia_compositor_command_header header;
    cassia_window_handle window_handle;
    VkFormat format;
    VkExtent2D extent;
    VkImageUsageFlags usage;
    VkCompositeAlphaFlagsKHR composite;
    uint32_t image_count;
};

typedef int cassia_compositor_swapchain_handle;

struct cassia_compositor_command_allocate_swapchain_response {
    VkResult result;
    cassia_compositor_swapchain_handle handle;
    /* hwb textures[image_count] */
};


struct cassia_compositor_command_dequeue {
    struct cassia_compositor_command_header header;
    cassia_compositor_swapchain_handle handle;
    uint64_t timeout;
};

struct cassia_compositor_command_dequeue_response {
    VkResult result;
    uint32_t image_index;
    /* fd dequeue_done_fence */
};


struct cassia_compositor_command_queue {
    struct cassia_compositor_command_header header;
    cassia_compositor_swapchain_handle handle;
    uint32_t image_index;
    /* fd present_ready_fence */
};

struct cassia_compositor_command_queue_response {
    VkResult result;
};

struct cassia_command_info {
    size_t num_bytes;
    int num_fds;
};

#endif /* __IPC_TYPES_H */
