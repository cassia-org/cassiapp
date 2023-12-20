// Copyright © 2023 Cassia Developers, all rights reserved.
#pragma once

#include <memory>

namespace cassia {
/**
 * @brief A RAII wrapper for a Unix file descriptor.
 */
struct UniqueFd {
  private:
    int fd;

  public:
    UniqueFd(int fd);

    UniqueFd(const UniqueFd &) = delete;

    /**
     * @note The other UniqueFd will be invalid after this is called.
     */
    UniqueFd(UniqueFd &&other) noexcept;

    UniqueFd &operator=(const UniqueFd &) = delete;

    /**
     * @note The other UniqueFd will be invalid after this is called.
     */
    UniqueFd &operator=(UniqueFd &&other) noexcept;

    ~UniqueFd();

    /**
     * @note This will be -1 if this UniqueFd is invalid.
     */
    [[nodiscard]] constexpr int Get() {
        return fd;
    }

    /**
     * @brief Closes the file descriptor.
     * @note After this is called, this UniqueFd will be invalid.
     */
    void Reset();

    /**
     * @return If this UniqueFd holds a valid file descriptor.
     */
    constexpr bool Valid() {
        return fd != -1;
    }

    /**
     * @return A new file descriptor that refers to the same underlying file, these have independent lifetimes.
     */
    [[nodiscard]] UniqueFd Duplicate();
};

/**
 * @brief A reference-counting RAII wrapper for a Unix file descriptor.
 */
struct SharedFd {
  private:
    std::shared_ptr<UniqueFd> fd;

  public:
    SharedFd(int pFd);

    SharedFd(UniqueFd &&other) noexcept;

    SharedFd(const SharedFd &) = default;

    SharedFd(SharedFd &&) = default;

    SharedFd &operator=(const SharedFd &) = default;

    SharedFd &operator=(SharedFd &&) = default;

    /**
     * @note This will be -1 if this SharedFd is invalid.
     */
    [[nodiscard]] constexpr int Get() {
        if (!fd)
            return -1;
        return fd->Get();
    }

    /**
     * @brief Resets this reference to the file descriptor, closing the file descriptor if this was the last reference.
     * @note After this is called, this SharedFd will be invalid.
     */
    constexpr void Reset() {
        fd.reset();
    }

    /**
     * @return If this SharedFd is referring to a valid file descriptor.
     */
    constexpr bool Valid() {
        return fd != nullptr;
    }
};
}
