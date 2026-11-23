#include <pi_tracker/length_framing.h>
#include <pi_tracker/tcp_io.h>
#include <pi_tracker/unique_fd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

using namespace std::chrono_literals;
using pi_tracker::UniqueFd;

// ---------- допоміжне: TCP-з'єднання через localhost ----------

void check(int result, const char *what) {
  if (result == -1) {
    throw std::system_error(errno, std::generic_category(), what);
  }
}

struct Link {
  UniqueFd drone;  // сокет на боці дрона
  UniqueFd ground; // сокет на боці наземної станції
};

Link makeLink() {
  UniqueFd server{::socket(AF_INET, SOCK_STREAM, 0)};
  check(server.get(), "socket");
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0; // 0 = будь-який вільний порт
  check(::bind(server.get(), reinterpret_cast<sockaddr *>(&addr), sizeof addr),
        "bind");
  check(::listen(server.get(), 1), "listen");
  socklen_t len = sizeof addr;
  check(::getsockname(server.get(), reinterpret_cast<sockaddr *>(&addr), &len),
        "getsockname");

  UniqueFd drone{::socket(AF_INET, SOCK_STREAM, 0)};
  check(drone.get(), "socket");
  check(
      ::connect(drone.get(), reinterpret_cast<sockaddr *>(&addr), sizeof addr),
      "connect");
  UniqueFd ground{::accept(server.get(), nullptr, nullptr)};
  check(ground.get(), "accept");
  return {std::move(drone), std::move(ground)};
}

// Читає, доки не набереться total байт, і друкує, скільки повернув кожен recv.
void receiveAndReport(int fd, std::size_t bufferSize, std::size_t total) {
  std::vector<std::uint8_t> buffer(bufferSize);
  std::size_t received = 0;
  while (received < total) {
    const std::size_t n = pi_tracker::receiveSome(fd, buffer);
    if (n == 0) {
      break;
    }
    std::printf("recv -> %zu\n", n);
    received += n;
  }
}

// ---------- Частина 1 ----------

void scenario1() {
  Link link = makeLink();
  const std::vector<std::uint8_t> message(100, 'A');
  for (int i = 0; i < 3; ++i) {
    pi_tracker::sendAll(link.drone.get(),
                        message); // три окремі send по 100 байт
  }
  std::this_thread::sleep_for(100ms); // станція починає читати трохи пізніше
  receiveAndReport(link.ground.get(), 1024, 300);
}
// 300

void scenario2() {
  Link link = makeLink();
  const std::vector<std::uint8_t> message(100, 'A');
  for (int i = 0; i < 3; ++i) {
    pi_tracker::sendAll(link.drone.get(), message);
  }
  std::this_thread::sleep_for(100ms);
  receiveAndReport(link.ground.get(), 150, 300); // буфер станції — 150 байт
}
// 150 150

void scenario3() {
  Link link = makeLink();
  // Дрон відправляє ОДНЕ повідомлення на 100 байт, але двома частинами з
  // паузою.
  std::jthread drone{[fd = link.drone.get()] {
    const std::vector<std::uint8_t> half(50, 'B');
    pi_tracker::sendAll(fd, half);
    std::this_thread::sleep_for(200ms);
    pi_tracker::sendAll(fd, half);
  }};
  receiveAndReport(link.ground.get(), 1024, 100); // станція читає одразу
}
// 50 50

// ---------- Частина 2 ----------

// Дрон: кожне повідомлення = [4 байти довжини big-endian][текст] (ваш
// encodeFrame). «Радіоканал» ріже потік на шматки по 7 байт із паузами — як
// справжня мережа, що ріже як хоче.
void drone(UniqueFd fd) {
  const std::vector<std::string> messages{"ALT 120 m", "BAT 15.8 V",
                                          "GPS 50.4501 30.5234", "MODE LOITER",
                                          "RSSI -71 dBm"};

  std::vector<std::uint8_t> air;
  for (const std::string &text : messages) {
    const std::span<const std::uint8_t> payload{
        reinterpret_cast<const std::uint8_t *>(text.data()), text.size()};
    const std::vector<std::uint8_t> frame = pi_tracker::encodeFrame(payload);
    air.insert(air.end(), frame.begin(), frame.end());
  }

  const std::span<const std::uint8_t> bytes{air};
  for (std::size_t offset = 0; offset < bytes.size(); offset += 7) {
    const std::size_t size = std::min<std::size_t>(7, bytes.size() - offset);
    pi_tracker::sendAll(fd.get(), bytes.subspan(offset, size));
    std::this_thread::sleep_for(20ms);
  }
  // fd знищується тут → з'єднання закривається → у станції recv поверне 0
}

void printMessage(std::span<const std::uint8_t> payload) {
  const std::string_view text{reinterpret_cast<const char *>(payload.data()),
                              payload.size()};
  std::printf("message: \"%.*s\"\n", static_cast<int>(text.size()),
              text.data());
}

void groundStation(int fd) {
  std::array<std::uint8_t, 64> buffer{};
  pi_tracker::FrameDecoder decoder(1024);
  while (true) {
    const std::size_t n = pi_tracker::receiveSome(fd, buffer);
    if (n == 0) {
      break; // дрон закрив з'єднання
    }

    std::span<std::uint8_t> subspan = std::span{buffer}.subspan(0, n);
    std::vector<std::vector<std::uint8_t>> frames = decoder.feed(subspan);
    for (const std::vector<std::uint8_t> &frame : frames) {
      printMessage(frame);
    }
  }
}

void scenario4() {
  Link link = makeLink();
  std::jthread droneThread{drone, std::move(link.drone)};
  groundStation(link.ground.get());
}

int main(int argc, char **argv) {
  const int n = argc > 1 ? std::atoi(argv[1]) : 0;
  switch (n) {
  case 1:
    scenario1();
    break;
  case 2:
    scenario2();
    break;
  case 3:
    scenario3();
    break;
  case 4:
    scenario4();
    break;
  default:
    std::puts("usage: r3 <1..4>");
  }
}
