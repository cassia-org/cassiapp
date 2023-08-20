#ifndef __IPC_CLIENT_H
#define __IPC_CLIENT_H

#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>

#ifdef __ANDROID__
#include <android/hardware_buffer.h>
#else
struct AHardwareBuffer {
    int dummy;
};

typedef struct AHardwareBuffer AHardwareBuffer;

int AHardwareBuffer_recvHandleFromUnixSocket( int socketFd, AHardwareBuffer **outBuffer )
{
    return 0;
}
#endif

#include "ipc_types.h"

static inline int cassiaclt_connect( void )
{
    int sockfd;
    struct sockaddr_un addr;
    sockfd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (sockfd == -1)
        return -1;

    memset(&addr, 0, sizeof(addr));

    addr.sun_family = AF_UNIX;
    addr.sun_path[0] = '\0';
    memcpy(&addr.sun_path[1], "cassia", 6);
    return (connect(sockfd, (const struct sockaddr *)&addr, sizeof(addr)) == -1) ? -1 : sockfd;
}

static inline void cassiaclt_disconnect( int sockfd ) 
{
    close(sockfd);
}

/* Returns true on success */
static inline bool cassiaclt_transact_raw( int sockfd, void *command, size_t command_size,
                                           void *response, size_t response_size,
                                           int send_fd_count, int *send_fds, int recv_fd_count, int *recv_fds,
                                           int recv_hardware_buffer_count, AHardwareBuffer **recv_hardware_buffers )
{
    struct iovec iov;
    struct msghdr msg;
    struct cmsghdr *cmsg;
    int i, max_fd_count = (send_fd_count > recv_fd_count) ? send_fd_count : recv_fd_count;
    int cmsgbuf_size = CMSG_SPACE(sizeof(int) * max_fd_count);
    char *cmsgbuf;

    memset(&msg, 0, sizeof(msg));
    memset(&iov, 0, sizeof(iov));

    if (cmsgbuf_size)
    {
        cmsgbuf = calloc(1, cmsgbuf_size);
        if (!cmsgbuf)
            return false;
    }

    if (send_fd_count && !(send_fd_count == 1 && send_fds[0] == -1))
    {
        msg.msg_control = cmsgbuf;
        msg.msg_controllen = cmsgbuf_size;

        cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int) * send_fd_count);
        memcpy(CMSG_DATA(cmsg), send_fds, sizeof(int) * send_fd_count);
        msg.msg_controllen = cmsg->cmsg_len;
    }

    iov.iov_base = command;
    iov.iov_len = command_size;

    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    if (sendmsg(sockfd, &msg, 0) == -1)
        goto err;

    /* Android requires hardware buffers are sent over a socket using a specific NDK function, so receive them 
       inbetween the usual data send/receive to avoid the need for special server-side handling */ 
    for (i = 0; i < recv_hardware_buffer_count; i++)
        if (AHardwareBuffer_recvHandleFromUnixSocket(sockfd, recv_hardware_buffers + i))
            goto err;

    memset(&msg, 0, sizeof(msg));

    iov.iov_base = response;
    iov.iov_len = response_size;

    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    if (recv_fd_count)
    {
        msg.msg_control = cmsgbuf;
        msg.msg_controllen = cmsgbuf_size;
    }

    if (recvmsg(sockfd, &msg, 0) == -1)
        goto err;

    cmsg = CMSG_FIRSTHDR(&msg);
    if (recv_fd_count && cmsg)
    {
        if (cmsg->cmsg_type != SCM_RIGHTS)
            goto err;

        memcpy(recv_fds, CMSG_DATA(cmsg), sizeof(int) * recv_fd_count);
    }
    else if (recv_fd_count == 1)
    {
        // Allow a special case where no FDs being sent is used to indicate a value of -1 (to represent signalled semaphores)
        *recv_fds = -1;
    }
    else if (recv_fd_count)
    {
        goto err;
    }

    return true;

err:
    if (cmsgbuf_size)
        free(cmsgbuf);

    return false;
}


static inline bool cassiaclt_compositor_allocate_swapchain( int sockfd, cassia_window_handle window_handle,
                                                            VkFormat format, VkExtent2D extent,
                                                            VkImageUsageFlags usage, 
                                                            VkCompositeAlphaFlagsKHR composite,
                                                            uint32_t image_count, 
                                                            VkResult *result, 
                                                            cassia_compositor_swapchain_handle *handle,
                                                            AHardwareBuffer **image_hardware_buffers )
{

    struct cassia_compositor_command_allocate_swapchain command = { 0 };
    struct cassia_compositor_command_allocate_swapchain_response response;

    command.header.header.target_class = CASSIA_COMMAND_CLASS_COMPOSITOR;
    command.header.type = CASSIA_COMPOSITOR_COMMAND_TYPE_ALLOCATE_SWAPCHAIN;
    command.window_handle = window_handle;
    command.format = format;
    command.extent = extent;
    command.usage = usage;
    command.composite = composite;
    command.image_count = image_count;

    if (!cassiaclt_transact_raw(sockfd, &command, sizeof(command), &response, sizeof(response), 0, NULL, 0, NULL, 
                                image_count, image_hardware_buffers))
        return false;

    *result = response.result;
    *handle = response.handle;
    /* image_hardware_buffers is set in transact */

    return true;
}


static inline bool cassiaclt_compositor_command_dequeue( int sockfd, cassia_compositor_swapchain_handle handle, 
                                                         uint64_t timeout,
                                                         VkResult *result, 
                                                         uint32_t *image_index,
                                                         int *dequeue_done_fence )
{

    struct cassia_compositor_command_dequeue command = { 0 };
    struct cassia_compositor_command_dequeue_response response;

    command.header.header.target_class = CASSIA_COMMAND_CLASS_COMPOSITOR;
    command.header.type = CASSIA_COMPOSITOR_COMMAND_TYPE_DEQUEUE;
    command.handle = handle;
    command.timeout = timeout;

    if (!cassiaclt_transact_raw(sockfd, &command, sizeof(command), &response, sizeof(response), 0, NULL, 
                                1, dequeue_done_fence, 0, NULL))
        return false;

    *result = response.result;
    *image_index = response.image_index;
    /* dequeue_done_fence is set in transact */

    return true;
}

static inline bool cassiaclt_compositor_command_queue( int sockfd, cassia_compositor_swapchain_handle handle, 
                                                       uint32_t image_index, int present_ready_fence,
                                                       VkResult *result )
{

    struct cassia_compositor_command_queue command = { 0 };
    struct cassia_compositor_command_queue_response response;

    command.header.header.target_class = CASSIA_COMMAND_CLASS_COMPOSITOR;
    command.header.type = CASSIA_COMPOSITOR_COMMAND_TYPE_QUEUE;
    command.handle = handle;
    command.image_index = image_index;

    if (!cassiaclt_transact_raw(sockfd, &command, sizeof(command), &response, sizeof(response), 
                                1, &present_ready_fence, 0, NULL, 0, NULL))
        return false;

    *result = response.result;

    return true;
}

#endif /* __IPC_CLIENT_H */
