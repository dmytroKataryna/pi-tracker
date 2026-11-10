// Живий клієнт MSP: питає польотний контролер про положення 10 разів на
// секунду. Mac:  ./day23_msp /dev/cu.usbmodem0x80000001      Pi:  ./day23_msp
// /dev/ttyACM0
#include "pi_tracker/msp.h"
#include "pi_tracker/serial_port.h"

#include <array>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <optional>
#include <thread>

namespace msp = pi_tracker::msp;
using namespace std::chrono_literals;

namespace {

// Обробник сигналу може лише записати прапорець типу sig_atomic_t (див.
// review-queue: сигнали)
volatile std::sig_atomic_t g_stop = 0;

void onSigint(int) { g_stop = 1; }

struct Stats {
  int requests{};
  int timeouts{};
};

// Запит → відповідь: надсилаємо кадр і читаємо байти, доки не прийде відповідь
// на ЦЮ команду або не мине тайм-аут. Відповіді на інші команди (запізнілі)
// просто пропускаємо
std::optional<msp::Frame> request(const pi_tracker::SerialPort &port,
                                  msp::Parser &parser, std::uint8_t command,
                                  Stats &stats) {
  ++stats.requests;
  port.write(msp::encodeRequest(command));

  std::array<std::uint8_t, 64> buffer{};
  const auto deadline = std::chrono::steady_clock::now() + 200ms;
  while (std::chrono::steady_clock::now() < deadline) {
    const std::size_t count = port.read(buffer); // чекає не довше 0.1 с
    for (std::size_t i = 0; i < count; ++i) {
      if (auto frame = parser.feed(buffer[i]);
          frame && frame->command == command) {
        return frame;
      }
    }
  }
  ++stats.timeouts;

  parser.reset();

  return std::nullopt;
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <serial port>\n", argv[0]);
    return 2;
  }
  std::signal(SIGINT, onSigint);

  try {
    // На USB (віртуальний COM-порт) швидкість ні на що не впливає, але termios
    // вимагає якусь задати
    const pi_tracker::SerialPort port{argv[1], 115200};
    msp::Parser parser;
    Stats stats;

    // «Рукостискання»: хто на тому боці
    const auto variantFrame = request(port, parser, msp::kFcVariant, stats);
    const auto versionFrame = request(port, parser, msp::kFcVersion, stats);
    const auto apiFrame = request(port, parser, msp::kApiVersion, stats);
    const auto variant =
        variantFrame ? msp::decodeFcVariant(*variantFrame) : std::nullopt;
    const auto version =
        versionFrame ? msp::decodeFcVersion(*versionFrame) : std::nullopt;
    const auto api = apiFrame ? msp::decodeApiVersion(*apiFrame) : std::nullopt;
    if (!variant || !version || !api) {
      std::fprintf(
          stderr,
          "no MSP answer: is it a flight controller with Betaflight?\n");
      return 1;
    }
    std::printf("%s %u.%u.%u, MSP API %u.%u\n", variant->c_str(),
                version->major, version->minor, version->patch, api->major,
                api->minor);
    std::printf("tilt the board; Ctrl+C to stop\n");

    while (!g_stop) {
      const auto attitudeFrame = request(port, parser, msp::kAttitude, stats);
      const auto imuFrame = request(port, parser, msp::kRawImu, stats);
      const auto attitude =
          attitudeFrame ? msp::decodeAttitude(*attitudeFrame) : std::nullopt;
      const auto imu = imuFrame ? msp::decodeRawImu(*imuFrame) : std::nullopt;
      if (attitude && imu) {
        std::printf("roll %+6.1f  pitch %+6.1f  yaw %5.0f deg | gyro %+5d %+5d "
                    "%+5d deg/s | acc z %5d\n",
                    attitude->rollDeg, attitude->pitchDeg, attitude->yawDeg,
                    imu->gyro[0], imu->gyro[1], imu->gyro[2], imu->acc[2]);
        std::fflush(stdout);
      }
      std::this_thread::sleep_for(100ms);
    }

    std::printf("\n%d requests, %d timeouts, %zu checksum errors\n",
                stats.requests, stats.timeouts, parser.checksumErrors());
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "fatal: %s\n", e.what());
    return 1;
  }
}
