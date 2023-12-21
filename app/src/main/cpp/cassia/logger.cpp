// Copyright © 2023 Cassia Developers, all rights reserved.

#include "logger.h"
#include "util/error.h"
#include <unistd.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <android/log.h>

namespace cassia {
Logger Logger::instance;

constexpr std::string_view BaseLogTag{"cassia.app."};
/**
 * @brief The maximum length of a single logcat message, anything past this will be truncated.
 * @url https://cs.android.com/android/platform/superproject/main/+/main:system/logging/liblog/include/log/log.h;l=71;drc=d3eebf83c7dc240c02dd7e7d846e8a07aaf92d18
 * @note This has been reduced from the default of 4068 to 4000 to account for the tag length and future changes.
 */
constexpr size_t LoggerEntryMaxPayload{4000};

Logger::LogStream::LogStream(SharedFd fd, int androidLogPriority) : fd{std::move(fd)}, androidLogPriority{androidLogPriority} {}

void Logger::LogStream::ReadAndLog(const char *tag, std::vector<char> &readBuffer) {
    if (overflow.size() > readBuffer.size())
        throw Exception{"Overflow buffer ({}) is larger than read buffer ({})", overflow.size(), readBuffer.size()};

    std::copy(overflow.begin(), overflow.end(), readBuffer.begin());
    overflow.clear();

    ssize_t len{read(fd.Get(), readBuffer.data() + overflow.size(), readBuffer.size() - (overflow.size() + 1))};
    if (len == -1)
        throw Exception{"read({} [{}]) failed: {}", fd.Get(), tag, strerror(errno)};

    size_t lastNewline{std::string_view{readBuffer.data(), static_cast<size_t>(len)}.find_last_of('\n')};
    if (lastNewline == std::string::npos) {
        readBuffer[len] = '\0'; // Null-terminate the buffer to make it a valid C string.
    } else {
        readBuffer[lastNewline] = '\0'; // Replace the newline with a null terminator.
        overflow.assign(readBuffer.begin() + lastNewline + 1, readBuffer.end());
    }

    __android_log_write(androidLogPriority, tag, readBuffer.data());
}

Logger::LogChannel::LogChannel(std::string tag, LogPipe pipe)
        : tag{std::move(tag)},
          out{std::move(pipe.out), ANDROID_LOG_INFO},
          err{std::move(pipe.err), ANDROID_LOG_ERROR} {}

void Logger::LogThread() {
    std::vector<char> readBuffer(LoggerEntryMaxPayload, 0);
    while (true) {
        std::array<epoll_event, 10> events{};
        int numEvents{epoll_wait(epollFd.Get(), events.data(), events.size(), -1)};
        if (numEvents == -1) {
            if (errno == EINTR)
                continue;
            else
                throw Exception{"epoll_wait() failed: {}", strerror(errno)};
        } else if (numEvents == 0) {
            throw Exception{"epoll_wait() returned 0 with no timeout"};
        }

        for (int i{}; i < numEvents; i++) {
            epoll_event &event{events[i]};

            int fd{event.data.fd};
            if (fd == wakeEventFd.Get())
                return; // Any events on the wake event fd mean we should exit.

            std::lock_guard lock{mutex};
            auto channelIt{std::find_if(channels.begin(), channels.end(), [&](auto &channel) { return channel.GetStream(fd) != nullptr; })};
            if (channelIt == channels.end())
                throw Exception{"epoll_wait() returned an unknown fd: {} (Event: 0x{:X})", fd, event.events};

            auto &channel{*channelIt};
            auto &stream{*channel.GetStream(fd)};

            if (event.events & EPOLLIN)
                stream.ReadAndLog(channel.tag.c_str(), readBuffer);

            if (event.events & EPOLLHUP) {
                epoll_event deleteEvent{.events = EPOLLIN, .data = {.fd = stream.fd.Get()}};
                if (epoll_ctl(epollFd.Get(), EPOLL_CTL_DEL, stream.fd.Get(), &deleteEvent) == -1)
                    throw Exception{"epoll_ctl({}, {} [{}]) failed: {}", epollFd.Get(), stream.fd.Get(), channel.tag, strerror(errno)};

                stream.fd.Reset();
                if (!channel.Valid())
                    channels.erase(channelIt);
            }
        }
    }
}

static UniqueFd CreateEpollFd() {
    int epollFd{epoll_create1(EPOLL_CLOEXEC)};
    if (epollFd == -1)
        throw Exception{"epoll_create1 failed: {}", strerror(errno)};
    return UniqueFd{epollFd};
}

static UniqueFd CreateEventFdWithEpoll(UniqueFd &epollFd) {
    int eventFd{eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)};
    if (eventFd == -1)
        throw Exception{"eventfd failed: {}", strerror(errno)};

    epoll_event event{.events = EPOLLIN, .data = {.fd = eventFd}};
    if (epoll_ctl(epollFd.Get(), EPOLL_CTL_ADD, eventFd, &event) == -1)
        throw Exception{"epoll_ctl({}, {} [EVENT]) failed: {}", epollFd.Get(), eventFd, strerror(errno)};

    return UniqueFd{eventFd};
}

static void SetProcessPipe(LogPipe &pipe) {
    if (dup2(pipe.out.Get(), STDOUT_FILENO) == -1)
        throw Exception{"dup2({}, STDOUT) failed: {}", pipe.out.Get(), strerror(errno)};
    if (dup2(pipe.err.Get(), STDERR_FILENO) == -1)
        throw Exception{"dup2({}, STDERR) failed: {}", pipe.err.Get(), strerror(errno)};
}

Logger::Logger() : epollFd{CreateEpollFd()},
                   wakeEventFd{CreateEventFdWithEpoll(epollFd)},
                   logThread{&Logger::LogThread, this} {
    auto processPipe{GetPipeImpl("main")};
    SetProcessPipe(processPipe);
}

Logger::~Logger() {
    if (logThread.joinable()) {
        int result{eventfd_write(wakeEventFd.Get(), 1)};
        TerminateIf(result == -1 && errno != EAGAIN, "eventfd_write({}) failed: {}", wakeEventFd.Get(), strerror(errno));

        logThread.join();
    }
}

static std::pair<LogPipe, LogPipe> CreateLogPipes() {
    int stdoutPipe[2], stderrPipe[2];
    if (pipe(stdoutPipe) == -1)
        throw std::runtime_error("pipe() failed: " + std::string{strerror(errno)});
    if (pipe(stderrPipe) == -1)
        throw std::runtime_error("pipe() failed: " + std::string{strerror(errno)});

    return {LogPipe{.out = stdoutPipe[0], .err = stderrPipe[0]},
            LogPipe{.out = stdoutPipe[1], .err = stderrPipe[1]}};
}

static void SetCloseOnExec(LogPipe &pipe) {
    if (fcntl(pipe.out.Get(), F_SETFD, FD_CLOEXEC) == -1)
        throw Exception("fcntl({}, FD_CLOEXEC) failed: {}", pipe.out.Get(), strerror(errno));
    if (fcntl(pipe.err.Get(), F_SETFD, FD_CLOEXEC) == -1)
        throw Exception("fcntl({}, FD_CLOEXEC) failed: {}", pipe.err.Get(), strerror(errno));
}

static void AddLogPipe(UniqueFd &epollFd, LogPipe &pipe) {
    epoll_event stdoutEvent{.events = EPOLLIN, .data = {.fd = pipe.out.Get()}};
    if (epoll_ctl(epollFd.Get(), EPOLL_CTL_ADD, pipe.out.Get(), &stdoutEvent) == -1)
        throw Exception("epoll_ctl({}, {} [STDOUT]) failed: {}", epollFd.Get(), pipe.out.Get(), strerror(errno));

    epoll_event stderrEvent{.events = EPOLLIN, .data = {.fd = pipe.err.Get()}};
    if (epoll_ctl(epollFd.Get(), EPOLL_CTL_ADD, pipe.err.Get(), &stderrEvent) == -1)
        throw Exception("epoll_ctl({}, {} [STDERR]) failed: {}", epollFd.Get(), pipe.err.Get(), strerror(errno));
}

LogPipe Logger::GetPipeImpl(const std::string &name) {
    auto [consumerPipes, producerPipes]{CreateLogPipes()};
    SetCloseOnExec(consumerPipes); // We don't want the consumer pipes to be inherited by child processes whatsoever, they should only be held by the logger.
    SetCloseOnExec(producerPipes); // We don't want the producer pipes to be inherited by child processes automatically, they should be dup'd manually after forking.
    {
        std::lock_guard lock{mutex};
        AddLogPipe(epollFd, consumerPipes);
        channels.emplace_back(std::string{BaseLogTag} + name, std::move(consumerPipes));
    }
    return std::move(producerPipes);
}
}
