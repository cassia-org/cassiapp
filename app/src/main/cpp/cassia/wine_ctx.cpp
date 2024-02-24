// Copyright © 2023 Cassia Developers, all rights reserved.

#include "wine_ctx.h"
#include <sys/system_properties.h>

namespace cassia {
std::string GetWineDebug() {
    auto property{__system_property_find("cassia.wine.debug")};
    if (property) {
        char value[PROP_VALUE_MAX];
        int len{__system_property_read(property, nullptr, value)};
        if (len > 0)
            return "WINEDEBUG=" + std::string{value, static_cast<size_t>(len)};
    }
    return "";
}

WineContext::WineContext(std::filesystem::path pRuntimePath, std::filesystem::path pPrefixPath)
        : runtimePath{std::move(pRuntimePath)}, prefixPath{std::move(pPrefixPath)},
          envVars{"WINEPREFIX=" + (prefixPath / "pfx").string(), "HOME=" + (prefixPath / "home").string(), "LD_LIBRARY_PATH=" + (runtimePath / "lib").string(), "PATH=" + (runtimePath / "bin").string(), "WINELOADER=" + (runtimePath / "bin/wine").string(), GetWineDebug()},
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
