// Copyright © 2023 Cassia Developers, all rights reserved.

#include "fd.h"
#include <unistd.h>

namespace cassia {
UniqueFd::UniqueFd(int fd) : fd{fd} {}

UniqueFd::UniqueFd(UniqueFd &&other) noexcept {
    fd = other.fd;
    other.fd = -1;
}

UniqueFd &UniqueFd::operator=(UniqueFd &&other) noexcept {
    fd = other.fd;
    other.fd = -1;
    return *this;
}

void UniqueFd::Reset() {
    if (fd != -1) {
        close(fd);
        fd = -1;
    }
}

UniqueFd::~UniqueFd() {
    Reset();
}

UniqueFd UniqueFd::Duplicate() {
    return UniqueFd(dup(fd));
}

SharedFd::SharedFd(int pFd) : fd{std::make_shared<UniqueFd>(pFd)} {}

SharedFd::SharedFd(UniqueFd &&other) noexcept: fd{std::make_shared<UniqueFd>(std::move(other))} {}
}
