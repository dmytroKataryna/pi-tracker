#pragma once

namespace pi_tracker {

class UniqueFd {
public:
  UniqueFd() noexcept = default;
  explicit UniqueFd(int fd) noexcept : m_fd(fd) {}

  ~UniqueFd();

  UniqueFd(const UniqueFd &) = delete;
  UniqueFd &operator=(const UniqueFd &) = delete;
  UniqueFd(UniqueFd &&other) noexcept;
  UniqueFd &operator=(UniqueFd &&other) noexcept;

  int get() const noexcept;
  explicit operator bool() const noexcept;
  int release() noexcept;
  void reset(int fd = -1) noexcept;

private:
  int m_fd{-1};
};

} // namespace pi_tracker
