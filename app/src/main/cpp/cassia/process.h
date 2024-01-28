// Copyright © 2023 Cassia Developers, all rights reserved.
#pragma once

#include "logger.h"
#include <string>
#include <vector>
#include <filesystem>
#include <thread>

namespace cassia {
/**
 * @brief A wrapper around a child process with pipes for stdout and stderr, and ensuring the child process is killed when destroyed.
 * @note A workaround for Android's limitation of being unable to launch executables from the app's data directory is included.
 * @attention Any process tracked by this class must exit before the destruction unless detached beforehand, failing to do so will result in std::terminate being called.
 */
struct Process {
    pid_t pid{-1};

    Process() = default;

    /**
     * @brief Launches a child process with the provided arguments and environment variables.
     * @param logPipe A pair of pipes to redirect the stdout and stderr of the child process into.
     */
    Process(std::filesystem::path exe, const std::vector<std::string> &args = {}, const std::vector<std::string> &envVars = {}, std::optional<LogPipe> logPipe = std::nullopt);

    Process(const Process &) = delete;

    Process &operator=(const Process &) = delete;

    Process(Process &&other) noexcept;

    Process &operator=(Process &&other) noexcept;

    ~Process();

    /**
     * @brief Detaches the child process from this object, this will allow the child process to continue running after this object is destroyed.
     */
    void Detach();

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
