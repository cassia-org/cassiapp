// Copyright © 2023 Cassia Developers, all rights reserved.

#include "process.h"
#include <vector>
#include <span>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <android/log.h>

namespace cassia {
Process::Process(std::filesystem::path exe, const std::vector<std::string> &args, const std::vector<std::string> &envVars) {
    /* Android's SELinux policy (execute_no_trans) prevents us from executing executables from the app's data directory.
     * To work around this, we execute /system/bin/linker64 instead, which can link ELF executables in userspace and execute them.
     * While this was originally designed for executing ELFs directly from ZIPs, it works just as well for our use case.
     */
    constexpr const char *LinkerPath{"/system/bin/linker64"};

    int stdoutFds[2], stderrFds[2];
    if (pipe(stdoutFds) == -1 || pipe(stderrFds) == -1)
        throw std::runtime_error("pipe() failed: " + std::string{strerror(errno)});
    stdoutFd = stdoutFds[0];
    stderrFd = stderrFds[0];

    pid = fork();
    if (pid == 0) {
        close(stdoutFds[0]);
        close(stderrFds[0]);
        dup2(stdoutFds[1], STDOUT_FILENO);
        dup2(stderrFds[1], STDERR_FILENO);
        close(stdoutFds[1]);
        close(stderrFds[1]);

        std::vector<const char *> argv;
        argv.reserve(args.size() + 3);
        argv.push_back(LinkerPath);
        argv.push_back(exe.c_str());
        for (const auto &arg: args)
            argv.push_back(arg.c_str());
        argv.push_back(nullptr);

        std::vector<const char *> envp;
        envp.reserve(envVars.size() + 1);
        for (const auto &var: envVars)
            envp.push_back(var.c_str());
        envp.push_back(nullptr);

        int status{execve(LinkerPath, const_cast<char *const *>(argv.data()), const_cast<char *const *>(envp.data()))};
        fprintf(stderr, "execve returned %d (%d = %s)\n", status, errno, strerror(errno));

        _Exit(127); // We can't use exit() here because it'll attempt to run ART atexit() callbacks
    }

    close(stdoutFds[1]);
    close(stderrFds[1]);

    logThread = std::thread{LogThread, exe.filename().string(), pid, stdoutFds[0], stderrFds[0]};
}

Process::~Process() {
    if (IsRunning())
        kill(pid, SIGKILL);
    logThread.join();
    if (stdoutFd != -1)
        close(stdoutFd);
    if (stderrFd != -1)
        close(stderrFd);
}

bool Process::IsRunning() {
    if (pid == -1)
        return false;
    int status;
    bool running{waitpid(pid, &status, WNOHANG) == 0};
    if (!running)
        pid = -1;
    return running;
}

int Process::WaitForExit() {
    if (pid == -1)
        return -1;
    int status;
    waitpid(pid, &status, 0);
    pid = -1;
    return WEXITSTATUS(status);
}

void Process::LogThread(std::string name, pid_t pid, int stdoutFd, int stderrFd) {
    std::string logTag{"cassia.app.child." + name + "." + std::to_string(pid)};
    __android_log_print(ANDROID_LOG_INFO, logTag.c_str(), "%s launched with PID %d", name.c_str(), pid);

    int epollFd{epoll_create1(0)};
    if (epollFd == -1)
        throw std::runtime_error("epoll_create1() failed: " + std::string{strerror(errno)});

    epoll_event stdoutEvent{.events = EPOLLIN, .data = {.fd = stdoutFd}};
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, stdoutFd, &stdoutEvent) == -1)
        throw std::runtime_error("epoll_ctl() failed: " + std::string{strerror(errno)});

    epoll_event stderrEvent{.events = EPOLLIN, .data = {.fd = stderrFd}};
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, stderrFd, &stderrEvent) == -1)
        throw std::runtime_error("epoll_ctl() failed: " + std::string{strerror(errno)});

    bool stdoutOpen{true}, stderrOpen{true};
    std::vector<char> stdoutBuf(1024 * 1024, 0), stderrBuf(1024 * 1024, 0);
    std::span<char> stdoutSpan{}, stderrSpan{};
    std::string outStr;
    while (stdoutOpen || stderrOpen) {
        epoll_event events[2];
        int numEvents{epoll_wait(epollFd, events, 2, -1)};
        if (numEvents == -1)
            throw std::runtime_error("epoll_wait() failed: " + std::string{strerror(errno)});
        if (numEvents == 0)
            break;
        for (int i{0}; i < numEvents; i++) {
            epoll_event &event{events[i]};

            /**
             * @brief Reads a string from a file descriptor, attempts to stop at a newline.
             * @note We reuse outStr to avoid unnecessary allocations as the capacity is preserved.
             */
            auto readString{[&](int fd, std::vector<char> &readBuf, std::span<char> &readSpan) {
                outStr.clear();
                outStr.append(readSpan.data(), readSpan.size());
                readSpan = {};
                while (true) {
                    ssize_t len{read(fd, readBuf.data(), readBuf.size())};
                    if (len == -1)
                        throw std::runtime_error("read() failed: " + std::string{strerror(errno)});
                    if (len == 0)
                        break;
                    size_t lastNewline{std::string_view{readBuf.data(), static_cast<size_t>(len)}.find_last_of('\n')};
                    if (lastNewline == std::string::npos) {
                        outStr.append(readBuf.data(), len);
                    } else {
                        outStr.append(readBuf.data(), lastNewline + 1);
                        readSpan = std::span<char>{readBuf.data() + lastNewline + 1, len - lastNewline - 1};
                        break;
                    }
                }
                return outStr;
            }};

            if (event.events & EPOLLIN) {
                if (event.data.fd == stdoutFd) {
                    readString(stdoutFd, stdoutBuf, stdoutSpan);
                    __android_log_print(ANDROID_LOG_INFO, logTag.c_str(), "%s", outStr.c_str());
                } else if (event.data.fd == stderrFd) {
                    readString(stderrFd, stderrBuf, stderrSpan);
                    __android_log_print(ANDROID_LOG_ERROR, logTag.c_str(), "%s", outStr.c_str());
                }
            }

            if (event.events & EPOLLHUP) {
                if (event.data.fd == stdoutFd)
                    stdoutOpen = false;
                else if (event.data.fd == stderrFd)
                    stderrOpen = false;
            }
        }
    }

    int status;
    waitpid(pid, &status, 0);
    __android_log_print(ANDROID_LOG_ERROR, logTag.c_str(), "%s exited with code %d", name.c_str(), WEXITSTATUS(status));
}
}
