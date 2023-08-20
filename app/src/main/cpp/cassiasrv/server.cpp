#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <poll.h>
#include <cstdlib>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <array>
#include <span>
#include <vector>
#include <limits>

#include "log.h"
#include "ipc_types.h"
#include "server.h"

namespace cassiasrv {

void Core::SetSurface(ANativeWindow *window) {
    compositor.RecreateAndroidSwapchain(window);
}

cassia_command_info Core::Dispatch(cassia_command_header header, int sockFd,
                                   std::span<char> recvData, std::span<int> recvFds,
                                   std::span<char> sendData, std::span<int> sendFds) {
    switch (header.target_class) {
        case CASSIA_COMMAND_CLASS_COMPOSITOR:
            return compositor.Dispatch(*reinterpret_cast<cassia_compositor_command_header *>(recvData.data()), sockFd,
                                       recvData, recvFds, sendData, sendFds);
        default:
            throw std::runtime_error("Unexpected command!");
    }
}

bool Server::HandleMessage(int dataSocket) {
    // Receive the message data
    std::array<char, CASSIA_MAX_COMMAND_SIZE> recvBuf;
    struct iovec recvIov{
            .iov_base = recvBuf.data(),
            .iov_len = recvBuf.size()
    };

    std::array<char, CMSG_SPACE(sizeof(int) * CASSIA_MAX_COMMAND_FD_COUNT)> recvCmsgBuf;
    struct msghdr recvMsg{
            .msg_iov = &recvIov,
            .msg_iovlen = 1,
            .msg_control = recvCmsgBuf.data(),
            .msg_controllen = recvCmsgBuf.size()
    };

    ssize_t bytesRead{::recvmsg(dataSocket, &recvMsg, 0)};
    if (bytesRead == -1)
        return false;
    else if (bytesRead == 0)
        return true; // Ignore empty messages, sometimes received when closing the socket

    std::span<char> recvData{recvBuf.data(), static_cast<size_t>(bytesRead)};
    // Initialise all FDs to -1 in case a command is called without an FD that expects an FD, which can happen with semaphores as no FD means a signalled semaphore
    std::array<int, CASSIA_MAX_COMMAND_FD_COUNT> recvFds{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

    struct cmsghdr *cmsg{CMSG_FIRSTHDR(&recvMsg)};
    if (cmsg && cmsg->cmsg_type == SCM_RIGHTS)
        std::memcpy(recvFds.data(), CMSG_DATA(cmsg), cmsg->cmsg_len);

    auto header{*reinterpret_cast<cassia_command_header *>(recvBuf.data())};

    // Setup structures to send the response message so that the dispatcher can write the response
    std::array<char, CASSIA_MAX_COMMAND_SIZE> sendBuf;
    std::array<char, CMSG_SPACE(sizeof(int) * CASSIA_MAX_COMMAND_FD_COUNT)> sendCmsgBuf{};

    struct iovec sendIov{
            .iov_base = sendBuf.data(),
            .iov_len = sendBuf.size()
    };

    struct msghdr sendMsg{
            .msg_iov = &sendIov,
            .msg_iovlen = 1,
            .msg_control = sendCmsgBuf.data(),
            .msg_controllen = sendCmsgBuf.size()
    };

    struct cmsghdr *sendCmsg{CMSG_FIRSTHDR(&sendMsg)};

    std::span sendFds{reinterpret_cast<int *>(CMSG_DATA(sendCmsg)), CASSIA_MAX_COMMAND_FD_COUNT};

    // Dispatch the command, the number of bytes/fds to send will be returned
    cassia_command_info sendInfo{core.Dispatch(header, dataSocket, recvData, {recvFds}, {sendBuf}, sendFds)};

    sendIov.iov_len = sendInfo.num_bytes;
    // Allow for command handlers to return a -1 FD in the 1 FD case to handle semaphores using -1 to represent a signalled semaphore
    // If they do, no FDs will be sent
    if (sendInfo.num_fds > 1 || (sendInfo.num_fds == 1 && sendFds[0] != -1)) {
        sendCmsg->cmsg_type = SCM_RIGHTS;
        sendCmsg->cmsg_level = SOL_SOCKET;
        sendCmsg->cmsg_len = CMSG_LEN(sizeof(int) * sendInfo.num_fds);
        sendMsg.msg_controllen = sendCmsg->cmsg_len;
    } else {
        sendMsg.msg_control = nullptr;
        sendMsg.msg_controllen = 0;
    }


    // Send the response
    ssize_t result{::sendmsg(dataSocket, &sendMsg, 0)};
    for (int i{}; i < sendInfo.num_fds; i++)
        ::close(sendFds[i]);

    return result != -1;
}

Server::Server(Core &core) : core{core} {}

Server::~Server() {
    close(connSocket);
}

void Server::Initialise() {
    if (connSocket = ::socket(AF_UNIX, SOCK_SEQPACKET, 0); connSocket == -1)
        throw std::runtime_error("Failed to create server socket!");

    struct sockaddr_un addr {
        .sun_family = AF_UNIX,
        .sun_path = {'\0', 'c', 'a', 's', 's', 'i', 'a'}
    };

    if (::bind(connSocket, reinterpret_cast<const struct sockaddr *>(&addr), sizeof(addr)) == -1) {
        ::close(connSocket);
        throw std::runtime_error("Failed to bind server socket!");
    }

    if (::listen(connSocket, 64) == -1) {
        ::close(connSocket);
        throw std::runtime_error("Failed begin listening on server socket!");
    }
}

void Server::Run() {
    if (connSocket == -1)
        throw std::logic_error("Attempted to run server before initialisation");

    std::vector<struct pollfd> pollFds;

    pollFds.push_back({connSocket, POLLIN, 0});

    while (true) {
        int numEvents{::poll(pollFds.data(), pollFds.size(), std::numeric_limits<int>::max())};
        if (numEvents == -1 && errno != EINTR) {
            throw std::runtime_error("Failed to poll!");
        } else if (numEvents > 0) {
            // Check for new connections
            if (pollFds[0].revents) {
                numEvents--;

                int clientSocket{::accept(connSocket, nullptr, nullptr)};
                if (clientSocket != -1) {
                    pollFds.push_back({clientSocket, POLLIN, 0});
                    printf("Client connected: %d\n", clientSocket);
                }
            }

            // Loop over all data socket FDs and dispatch any messages as appropriate
            for (auto pollFd{std::next(pollFds.begin())}; pollFd != pollFds.end(); pollFd++) {
                if (numEvents == 0)
                    break;

                if (pollFd->revents) {
                    if (pollFd->revents & POLLIN)
                        if (!HandleMessage(pollFd->fd))
                            printf("Failed to handle message from client: %d\n", pollFd->fd);

                    if (pollFd->revents & (POLLHUP | POLLERR)) {
                        close(pollFd->fd);
                        pollFds.erase(pollFd);
                        printf("Client disconnected: %d\n", pollFd->fd);
                    }

                    pollFd->revents = 0;
                    numEvents--;
                }
            }
        }
    }
}
}
