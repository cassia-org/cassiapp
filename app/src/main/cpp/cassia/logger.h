// Copyright © 2023 Cassia Developers, all rights reserved.
#pragma once

#include "util/fd.h"
#include <thread>

namespace cassia {
/**
 * @brief A pair of pipes for stdout/stderr from any process.
 */
struct LogPipe {
    SharedFd out;
    SharedFd err;
};

/**
 * @brief A class to handle logging from stdout/stderr pipes (of the main process, along with any other pipes via GetPipe(...)) to logcat.
 * @note This class holds a global instance of itself and will be initialized automatically while running static initializers, this includes taking over the process's stdout/stderr pipes.
 */
struct Logger {
  private:
    UniqueFd epollFd; //!< Used to wait for events from the pipes on the log thread.
    UniqueFd wakeEventFd; //!< Used to wake the epoll loop when the thread needs to join.
    std::thread logThread;

    struct LogStream {
        SharedFd fd;
        std::vector<char> overflow; //!< Any data that was read from the pipe but couldn't be logged yet.
        int androidLogPriority; //!< The Android log priority to use for this stream, this will be reflected in logcat.

        LogStream(SharedFd fd, int androidLogPriority);

        /**
         * @brief Reads data from the pipe and logs it to logcat.
         * @note This will block until data is available.
         * @param tag The tag to use for the logcat tag.
         * @param readBuffer A buffer to use for reading data from the pipe, this should have a (large) non-zero size.
         */
        void ReadAndLog(const char *tag, std::vector<char> &readBuffer);
    };

    /**
     * @brief A channel is a collection of out/err streams of logs with an associated tag.
     */
    struct LogChannel {
        std::string tag;
        LogStream out, err;

        LogChannel(std::string tag, LogPipe pipe);

        /**
         * @return The stream for the given file descriptor, or nullptr if it doesn't match any of the streams.
         */
        LogStream *GetStream(int fd) {
            if (fd == out.fd.Get())
                return &out;
            if (fd == err.fd.Get())
                return &err;
            return nullptr;
        }

        /**
         * @return If this channel is valid, i.e. if it has any valid streams.
         */
        bool Valid() {
            return out.fd.Valid() || err.fd.Valid();
        }
    };

    std::mutex mutex; //!< Used to synchronize access to the list of channels.
    std::vector<LogChannel> channels;

    void LogThread();

    Logger();

    ~Logger();

    LogPipe GetPipeImpl(const std::string &name);

    static Logger instance; //!< The global instance of the logger.

  public:
    /**
     * @return Log pipes that will be redirected into logcat with the given name as a tag.
     * @note There can be multiple streams with the same name, they will all be logged to the same tag.
     */
    static LogPipe GetPipe(const std::string &name) {
        return instance.GetPipeImpl(name);
    }
};
}
