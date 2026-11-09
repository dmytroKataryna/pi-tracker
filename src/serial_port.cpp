#include "pi_tracker/serial_port.h"

#include <cerrno>
#include <stdexcept>
#include <system_error>

#include <termios.h> // termios, cfmakeraw, tcsetattr...
#include <unistd.h>  // read, write

namespace pi_tracker {

namespace {

// Число → константа termios. Довільне число передати не можна, лише фіксовані
// значення B9600, B115200...
speed_t toSpeed(int baudRate) {
  switch (baudRate) {
  case 9600:
    return B9600;
  case 115200:
    return B115200;
  case 230400:
    return B230400;
#ifdef B460800
  case 460800:
    return B460800;
#endif
#ifdef B921600
  case 921600:
    return B921600;
#endif
  default:
    throw std::invalid_argument("SerialPort: unsupported baud rate");
  }
}

[[noreturn]] void throwErrno(const char *what) // [[noreturn]]: функція ніколи
                                               // не повертається, лише кидає
{
  throw std::system_error(errno, std::generic_category(), what);
}

} // namespace

SerialPort::SerialPort(const char *path, int baudRate)
    : m_fd{path, O_RDWR | O_NOCTTY}
// O_NOCTTY: порт не стає «керуючим терміналом» процесу
{
  termios tty{};
  if (::tcgetattr(m_fd.get(), &tty) < 0) // прочитати поточні налаштування порту
  {
    throwErrno("tcgetattr");
  }

  ::cfmakeraw(&tty); // сирий режим: без рядків, ехо, перетворень і
                     // спецсимволів; 8 біт, без парності

  const speed_t speed = toSpeed(baudRate);
  ::cfsetispeed(&tty, speed); // швидкість прийому
  ::cfsetospeed(&tty, speed); // швидкість передачі

  tty.c_cflag |= static_cast<tcflag_t>(CLOCAL | CREAD);
  tty.c_cflag &= ~static_cast<tcflag_t>(CSTOPB);
  tty.c_cflag &= ~static_cast<tcflag_t>(CRTSCTS);

  tty.c_cc[VMIN] = 0; // read() не чекає мінімальної кількості байтів...
  tty.c_cc[VTIME] =
      1; // ...але чекає до 0.1 с (одиниця = десята частка секунди)

  if (::tcsetattr(m_fd.get(), TCSANOW, &tty) < 0) // застосувати негайно
  {
    throwErrno("tcsetattr");
  }
  ::tcflush(m_fd.get(),
            TCIFLUSH); // викинути старі байти, що накопичились до відкриття
}

std::size_t SerialPort::read(std::span<std::uint8_t> buffer) const {
  const ssize_t n = ::read(m_fd.get(), buffer.data(), buffer.size());
  if (n < 0) {
    throwErrno("read");
  }
  return static_cast<std::size_t>(n);
}

void SerialPort::write(std::span<const std::uint8_t> data) const {
  if (::write(m_fd.get(), data.data(), data.size()) < 0) {
    throwErrno("write");
  }
}

} // namespace pi_tracker