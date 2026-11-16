#include "pi_tracker/tcp_io.h"

#include <cerrno>
#include <cstddef>
#include <sys/socket.h>
#include <system_error>

namespace pi_tracker {

void sendAll(int fd, std::span<const std::uint8_t> bytes) {
  std::size_t byteSend = 0;
  while (byteSend < bytes.size()) {
    std::span<const std::uint8_t> offsetBytes =
        bytes.subspan(byteSend, bytes.size() - byteSend);
    ssize_t result = ::send(fd, offsetBytes.data(), offsetBytes.size(), 0);
    if (result == -1) {
      if (errno == EINTR) {
        continue;
      } else {
        throw std::system_error(errno, std::generic_category(), "send");
      }
    }
    byteSend = byteSend + static_cast<std::size_t>(result);
  }
}

std::size_t receiveSome(int fd, std::span<std::uint8_t> buffer) {
  while (true) {
    const ssize_t result = ::recv(fd, buffer.data(), buffer.size(), 0);
    if (result >= 0) {
      return static_cast<std::size_t>(result); // дані або 0 = закрито
    }
    if (errno != EINTR) {
      throw std::system_error(errno, std::generic_category(), "recv");
    }
    // EINTR: нічого не сталося → наступне коло, recv ще раз
  }
}
} // namespace pi_tracker