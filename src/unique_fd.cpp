#include <pi_tracker/unique_fd.h>
#include <unistd.h>

namespace pi_tracker {

UniqueFd::UniqueFd(UniqueFd &&other) noexcept : m_fd{other.m_fd} {
  other.m_fd = -1;
}

UniqueFd &UniqueFd::operator=(UniqueFd &&other) noexcept {
  if (this == &other) {
    return *this;
  }

  if (m_fd != -1) {
    ::close(m_fd);
  }

  m_fd = other.m_fd;
  other.m_fd = -1;
  return *this;
}

UniqueFd::~UniqueFd() {
  if (m_fd != -1) {
    ::close(m_fd);
  }
}

int UniqueFd::get() const noexcept { return m_fd; }

UniqueFd::operator bool() const noexcept { return m_fd != -1; }

int UniqueFd::release() noexcept {
  int returnValue = m_fd;
  m_fd = -1;
  return returnValue;
}

void UniqueFd::reset(int fd) noexcept {
  if (m_fd != -1) {
    ::close(m_fd);
  }
  this->m_fd = fd;
}

} // namespace pi_tracker