#pragma once

#include "nekomposite.h"

class ANativeWindow;

namespace cassiasrv {
class Core {
private:
    Compositor compositor;

public:
    Core() = default;

    void SetSurface(ANativeWindow *window);

    cassia_command_info Dispatch(cassia_command_header header, int sockFd,
                                 std::span<char> recvData, std::span<int> recvFds,
                                 std::span<char> sendData, std::span<int> sendFds);
};

class Server {
private:
    int connSocket{-1};
    Core &core;

    // NOTE: all sent FDs will be closed
    bool HandleMessage(int dataSocket);

public:
    Server(Core &core);

    ~Server();

    void Initialise();

    void Run();
};
}