#include "pi_tracker/mavlink.h"
#include "telemetry_link.h"

#include <QCoreApplication>
#include <QTimer>

#include <cstdio>
#include <exception>
#include <format>
#include <numbers>
#include <optional>
#include <string>

namespace mav = pi_tracker::mavlink;

namespace {

double degrees(float radians) {
  return static_cast<double>(radians) * 180.0 / std::numbers::pi;
}

// Останнє відоме про дрон. Пишуть і читають його лише лямбди в головному потоці
// — mutex не потрібен
struct Snapshot {
  std::optional<mav::Heartbeat> heartbeat;
  std::optional<mav::Attitude> attitude;
  std::optional<mav::GlobalPosition> position;
  bool linkLost{false};
  int attitudesPerSecond{};
};

std::string describe(const Snapshot &s) {
  if (s.linkLost || !s.heartbeat) {
    return "NO LINK";
  }
  std::string text =
      std::format("{:9} | {:8}", mav::copterModeName(s.heartbeat->customMode),
                  s.heartbeat->armed() ? "ARMED" : "disarmed");
  if (s.position) {
    text += std::format(" | alt {:5.1f} m", s.position->relativeAltM);
  }
  if (s.attitude) {
    text += std::format(" | roll {:+5.1f} pitch {:+5.1f} yaw {:+6.1f} deg",
                        degrees(s.attitude->roll), degrees(s.attitude->pitch),
                        degrees(s.attitude->yaw));
  }
  return text + std::format(" | ATTITUDE {}/s", s.attitudesPerSecond);
}

} // namespace

int main(int argc, char *argv[]) {
  // Цикл подій без вікон. app.exec() нижче — це Looper.loop(): чекає подій і
  // розсилає їх об'єктам
  QCoreApplication app{argc, argv};

  try {
    TelemetryLink link{14550};
    Snapshot snapshot;

    // Лямбда замість слота: [&snapshot] — бо snapshot живе в main довше за всі
    // підключення
    QObject::connect(&link, &TelemetryLink::heartbeatReceived,
                     [&snapshot](const mav::Heartbeat &hb) {
                       snapshot.heartbeat = hb;
                       snapshot.linkLost = false;
                     });
    QObject::connect(&link, &TelemetryLink::attitudeReceived,
                     [&snapshot](const mav::Attitude &attitude) {
                       snapshot.attitude = attitude;
                       ++snapshot.attitudesPerSecond;
                     });

    QObject::connect(&link, &TelemetryLink::positionReceived,
                     [&snapshot](const mav::GlobalPosition &pos) {
                       snapshot.position = pos;
                     });

    QObject::connect(&link, &TelemetryLink::linkLost,
                     [&snapshot](void) { snapshot.linkLost = true; });

    // Раз на секунду друкуємо. Таймер — теж подія циклу, як
    // handler.postDelayed()
    QTimer printTimer;
    QObject::connect(&printTimer, &QTimer::timeout, [&snapshot] {
      std::printf("%s\n", describe(snapshot).c_str());
      std::fflush(stdout);
      snapshot.attitudesPerSecond = 0;
    });
    printTimer.start(1000);

    std::printf("[gcs] listening on UDP 14550\n");
    return app.exec();
  } catch (const std::exception &e) {
    std::fprintf(stderr, "fatal: %s\n", e.what());
    return 1;
  }
}
