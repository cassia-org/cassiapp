// Copyright © 2023 Cassia Developers, all rights reserved.

#include "wine_ctx.h"

namespace cassia {
WineContext::WineContext(std::filesystem::path pRuntimePath, std::filesystem::path pPrefixPath)
        : runtimePath{std::move(pRuntimePath)}, prefixPath{std::move(pPrefixPath)},
          envVars{"WINEPREFIX=" + (prefixPath / "pfx").string(), "HOME=" + (prefixPath / "home").string(), "LD_LIBRARY_PATH=" + (runtimePath / "lib").string(), "PATH=" + (runtimePath / "bin").string(), "WINELOADER=" + (runtimePath / "bin/wine").string()},
          serverProcess{runtimePath / "bin/wineserver", {"--foreground", "--persistent"}, envVars, Logger::GetPipe("wineserver")} {
    Launch("wineboot.exe", {"--init"}, {}, Logger::GetPipe("wineboot")).WaitForExit();
}

Process WineContext::Launch(std::string exe, std::vector<std::string> args, std::vector<std::string> pEnvVars, std::optional<LogPipe> logPipe) {
    pEnvVars.insert(pEnvVars.end(), envVars.begin(), envVars.end());
    args.insert(args.begin(), exe);
    return Process{runtimePath / "bin/wine", args, pEnvVars, logPipe};
}

WineContext::~WineContext() {
    Launch("wineboot.exe", {"--end-session", "--shutdown"}, {}, Logger::GetPipe("wineboot")).WaitForExit();
    Process{runtimePath / "bin/wineserver", {"--kill"}, envVars, Logger::GetPipe("wineserver")}.WaitForExit();
}
}
