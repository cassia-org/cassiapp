// Copyright © 2023 Cassia Developers, all rights reserved.
#pragma once

#include "process.h"

namespace cassia {
/**
 * @brief A class consolidating all Wine-related processes/state for a specific prefix with convenience wrappers.
 */
class WineContext {
  private:
    std::filesystem::path runtimePath;
    std::filesystem::path prefixPath;
    std::vector<std::string> envVars;
    Process serverProcess;

  public:
    /**
     * @details This will start the wineserver process and initialize the Wine prefix with wineboot.
     */
    WineContext(std::filesystem::path runtimePath, std::filesystem::path prefixPath, std::filesystem::path cassiaExtPath);

    /**
     * @brief Launches a Windows executable in the Wine environment.
     * @param exe The path to the executable to launch, this doesn't need to be an absolute path for executables in Wine's PATH (eg. cmd.exe, wineboot.exe, etc).
     * @param logPipe Same as Process::Process.
     */
    Process Launch(std::string exe, std::vector<std::string> args = {}, std::vector<std::string> envVars = {}, std::optional<LogPipe> logPipe = std::nullopt);

    /**
     * @details This will attempt to shutdown the Wine prefix with wineboot and use wineserver to kill all other wine processes.
     */
    ~WineContext();
};
}
