#include "pi_tracker/length_framing.h"
#include "pi_tracker/tcp_io.h"
#include "pi_tracker/unique_fd.h"

#include <gtest/gtest.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <future>
#include <random>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

using pi_tracker::encodeFrame;
using pi_tracker::FrameDecoder;
using pi_tracker::receiveSome;
using pi_tracker::sendAll;
using pi_tracker::UniqueFd;
using Bytes = std::vector<std::uint8_t>;

namespace {

void throwIfFailed(int result, const char *what) {
  if (result < 0) {
    throw std::system_error(errno, std::generic_category(), what);
  }
}

// Два кінці одного TCP-з'єднання через 127.0.0.1. Порт 0 — вільний порт обирає ядро
struct TcpPair {
  UniqueFd client;
  UniqueFd server;
};

TcpPair connectLoopback() {
  UniqueFd listener{::socket(AF_INET, SOCK_STREAM, 0)};
  throwIfFailed(listener.get(), "socket");

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  throwIfFailed(::bind(listener.get(), reinterpret_cast<const sockaddr *>(&address), sizeof(address)), "bind");
  throwIfFailed(::listen(listener.get(), 1), "listen");
  socklen_t length = sizeof(address);
  throwIfFailed(::getsockname(listener.get(), reinterpret_cast<sockaddr *>(&address), &length), "getsockname");

  TcpPair pair{UniqueFd{::socket(AF_INET, SOCK_STREAM, 0)}, UniqueFd{}};
  throwIfFailed(pair.client.get(), "socket");
  throwIfFailed(::connect(pair.client.get(), reinterpret_cast<const sockaddr *>(&address), sizeof(address)), "connect");
  pair.server.reset(::accept(listener.get(), nullptr, nullptr));
  throwIfFailed(pair.server.get(), "accept");
  return pair;
}

// Читає, доки інший бік не закриє з'єднання (receiveSome поверне 0)
Bytes receiveUntilClosed(int fd, std::size_t chunkSize) {
  Bytes all;
  Bytes chunk(chunkSize);
  while (const std::size_t count = receiveSome(fd, chunk)) {
    all.insert(all.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(count));
  }
  return all;
}

Bytes randomBytes(std::size_t count, unsigned seed) {
  std::mt19937 random{seed};
  std::uniform_int_distribution<int> byteValue{0, 255};
  Bytes bytes(count);
  for (auto &b : bytes) {
    b = static_cast<std::uint8_t>(byteValue(random));
  }
  return bytes;
}

// Поки об'єкт живий, кожну 1 мс приходить сигнал SIGALRM і перериває ::send/::recv,
// як це буває в справжній програмі. Обробник нічого не робить, SA_RESTART не встановлено:
// перерваний виклик повертає те, що встиг передати, або -1 з errno == EINTR
class InterruptEveryMillisecond {
public:
  InterruptEveryMillisecond() {
    struct sigaction action {};
    action.sa_handler = [](int) {};
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    ::sigaction(SIGALRM, &action, &m_previous);
    const itimerval every1ms{{0, 1000}, {0, 1000}};
    ::setitimer(ITIMER_REAL, &every1ms, nullptr);
  }
  ~InterruptEveryMillisecond() {
    const itimerval off{};
    ::setitimer(ITIMER_REAL, &off, nullptr);
    ::sigaction(SIGALRM, &m_previous, nullptr);
  }
  InterruptEveryMillisecond(const InterruptEveryMillisecond &) = delete;
  InterruptEveryMillisecond &operator=(const InterruptEveryMillisecond &) = delete;

private:
  struct sigaction m_previous {};
};

// Сигнали від таймера мають переривати саме відправника, тож у потоці-отримувачі їх блокуємо
void blockAlarmInThisThread() {
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGALRM);
  ::pthread_sigmask(SIG_BLOCK, &set, nullptr);
}

} // namespace

TEST(TcpIo, SmallMessageArrives) {
  TcpPair tcp = connectLoopback();
  const Bytes hello{'h', 'e', 'l', 'l', 'o'};
  sendAll(tcp.client.get(), hello);

  Bytes buffer(1024);
  const std::size_t count = receiveSome(tcp.server.get(), buffer);
  ASSERT_EQ(count, 5u) << "receiveSome повертає стільки, скільки прийшло, і не чекає, доки заповниться буфер";
  EXPECT_EQ(Bytes(buffer.begin(), buffer.begin() + 5), hello);
}

TEST(TcpIo, SendAllOfNothingReturnsImmediately) {
  TcpPair tcp = connectLoopback();
  sendAll(tcp.client.get(), Bytes{});
  tcp.client.reset(); // закриваємо: якщо sendAll щось надіслав, отримувач це побачить
  Bytes buffer(16);
  EXPECT_EQ(receiveSome(tcp.server.get(), buffer), 0u);
}

TEST(TcpIo, ReceiveSomeReturnsZeroWhenPeerClosed) {
  TcpPair tcp = connectLoopback();
  sendAll(tcp.client.get(), Bytes{1, 2, 3});
  tcp.client.reset();

  Bytes buffer(16);
  EXPECT_EQ(receiveSome(tcp.server.get(), buffer), 3u) << "спершу дочитуємо те, що встигли надіслати";
  EXPECT_EQ(receiveSome(tcp.server.get(), buffer), 0u) << "0 = інший бік закрив з'єднання, це не помилка";
}

TEST(TcpIo, BadDescriptorThrowsSystemErrorWithErrno) {
  Bytes buffer(16);
  try {
    sendAll(-1, Bytes{1});
    FAIL() << "sendAll(-1) має кинути std::system_error";
  } catch (const std::system_error &e) {
    EXPECT_EQ(e.code(), std::errc::bad_file_descriptor) << "у винятку має бути errno (EBADF)";
  }
  try {
    receiveSome(-1, buffer);
    FAIL() << "receiveSome(-1) має кинути std::system_error";
  } catch (const std::system_error &e) {
    EXPECT_EQ(e.code(), std::errc::bad_file_descriptor) << "у винятку має бути errno (EBADF)";
  }
}

TEST(TcpIo, SendAllSurvivesInterruptsAndPartialSends) {
  // 16 МБ не влазять у буфери ядра, а отримувач перші 100 мс узагалі не читає: send чекає,
  // і сигнали кожну 1 мс змушують його повертати ЧАСТКОВУ кількість байтів або -1 з EINTR
  const Bytes data = randomBytes(16 * 1024 * 1024, 7);
  TcpPair tcp = connectLoopback();

  auto reader = std::async(std::launch::async, [&] {
    blockAlarmInThisThread();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return receiveUntilClosed(tcp.server.get(), 4096);
  });
  std::exception_ptr sendError;
  {
    InterruptEveryMillisecond interrupts;
    try {
      sendAll(tcp.client.get(), data);
    } catch (...) {
      sendError = std::current_exception();
    }
  }
  tcp.client.reset(); // отримувач побачить кінець потоку навіть після помилки
  const Bytes received = reader.get();
  if (sendError) {
    std::rethrow_exception(sendError);
  }
  ASSERT_EQ(received.size(), data.size()) << "sendAll має надіслати ВСЕ, навіть якщо send повертає менше";
  EXPECT_TRUE(received == data) << "байти мають прийти ті самі й у тому самому порядку";
}

TEST(TcpIo, ReceiveSomeRetriesOnInterrupt) {
  // Отримувач чекає в recv, а сигнали кожну 1 мс його переривають; дані приходять через 50 мс
  TcpPair tcp = connectLoopback();
  auto writer = std::async(std::launch::async, [&] {
    blockAlarmInThisThread();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sendAll(tcp.client.get(), Bytes{42});
  });
  Bytes buffer(16);
  std::size_t count = 0;
  {
    InterruptEveryMillisecond interrupts;
    count = receiveSome(tcp.server.get(), buffer);
  }
  writer.get();
  ASSERT_EQ(count, 1u) << "EINTR — не помилка і не кінець потоку: recv треба повторити";
  EXPECT_EQ(buffer[0], 42);
}

TEST(TcpIo, SendToClosedPeerThrows) {
  // Без цього запис у закрите з'єднання вбив би весь процес сигналом SIGPIPE
  const auto previous = std::signal(SIGPIPE, SIG_IGN);
  TcpPair tcp = connectLoopback();
  tcp.server.reset();
  const Bytes data(16 * 1024 * 1024, 0x55);
  EXPECT_THROW(sendAll(tcp.client.get(), data), std::system_error)
      << "з'єднання закрито: send поверне -1 (EPIPE або ECONNRESET), і це має стати винятком";
  std::signal(SIGPIPE, previous);
}

TEST(TcpIo, FramesOverRealTcp) {
  // Частина 1 + частина 2: кадри з довжиною через справжній TCP, читаємо дрібними шматками
  std::vector<Bytes> sent;
  std::mt19937 random{3};
  std::uniform_int_distribution<std::size_t> payloadSize{0, 2000};
  for (unsigned i = 0; i < 500; ++i) {
    sent.push_back(randomBytes(payloadSize(random), i));
  }

  TcpPair tcp = connectLoopback();
  auto writer = std::async(std::launch::async, [&] {
    try {
      for (const Bytes &payload : sent) {
        sendAll(tcp.client.get(), encodeFrame(payload));
      }
    } catch (...) {
      tcp.client.reset();
      throw;
    }
    tcp.client.reset();
  });

  FrameDecoder decoder{2000};
  std::vector<Bytes> received;
  std::array<std::uint8_t, 777> chunk{};
  while (const std::size_t count = receiveSome(tcp.server.get(), chunk)) {
    for (auto &frame : decoder.feed(std::span{chunk}.first(count))) {
      received.push_back(std::move(frame));
    }
  }
  writer.get();

  EXPECT_EQ(received.size(), sent.size());
  EXPECT_TRUE(received == sent);
  EXPECT_EQ(decoder.buffered(), 0u) << "після закриття не має лишитися половини кадру";
}
