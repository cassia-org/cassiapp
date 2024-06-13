// Copyright © 2023 Cassia Developers, all rights reserved.

#include "process.h"
#include "util/error.h"
#include <unistd.h>
#include <sys/wait.h>

namespace cassia {
Process::Process(std::filesystem::path exe, const std::vector<std::string> &args, const std::vector<std::string> &envVars, std::optional<LogPipe> logPipe) {
    /* Android's SELinux policy (execute_no_trans) prevents us from executing executables from the app's data directory.
     * To work around this, we execute /system/bin/linker64 instead, which can link ELF executables in userspace and execute them.
     * While this was originally designed for executing ELFs directly from ZIPs, it works just as well for our use case.
     */
    constexpr const char *LinkerPath{"/system/bin/linker64"};

    fmt::println(stderr, "Launching '{} {}'", exe.string(), fmt::join(args, " "));

    pid = fork();
    if (pid == 0) {
        if (logPipe.has_value()) {
            dup2(logPipe->out.Get(), STDOUT_FILENO);
            dup2(logPipe->err.Get(), STDERR_FILENO);
        }

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
}

Process::Process(Process &&other) noexcept: pid{other.pid} { other.pid = -1; }

Process &Process::operator=(Process &&other) noexcept {
    pid = other.pid;
    other.pid = -1;
    return *this;
}

Process::~Process() {
    TerminateIf(IsRunning(), "Process {} is still running", pid);
}

void Process::Detach() {
    pid = -1;
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
    
    fmt::println(stderr, "Process {} exited with status {}", pid, WEXITSTATUS(status));
    pid = -1;
    return WEXITSTATUS(status);
}
}
