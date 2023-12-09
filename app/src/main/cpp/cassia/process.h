// Copyright © 2023 Cassia Developers, all rights reserved.
#pragma once

#include <unistd.h>
#include <string>
#include <vector>
#include <filesystem>
#include <thread>

namespace cassia {
    /**
     * @brief A wrapper around a child process with pipes for stdout and stderr, and ensuring the child process is killed when destroyed.
     * @note A workaround for Android's limitation of being unable to launch executables from the app's data directory is included.
     * @note This will log stdout and stderr from the child process to logcat from a launched thread.
     */
    class Process {
    private:
        std::thread logThread;

        static void LogThread(std::string name, pid_t pid, int stdoutFd, int stderrFd);

    public:
        pid_t pid{-1};
        int stdoutFd{-1}, stderrFd{-1};

        Process() = default;

        Process(std::filesystem::path exe, const std::vector<std::string> &args = {}, const std::vector<std::string> &envVars = {});

        ~Process();

        /**
         * @return If the child process is still running.
         * @note This will update the pid to -1 if the child process has exited.
         */
        bool IsRunning();

        /**
         * @brief Waits for the child process to exit.
         * @return The exit code of the child process.
         * @note This will update the pid to -1.
         */
        int WaitForExit();
    };
}
