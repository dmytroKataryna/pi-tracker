#include "pi_tracker/fd.h"
#include "pi_tracker/mavlink.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <format>
#include <iostream>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <system_error>

namespace mav = pi_tracker::mavlink;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

namespace
{

    static_assert(std::atomic<bool>::is_always_lock_free);
    std::atomic<bool> g_stopRequested{false};

    void onSignal(int)
    {
        g_stopRequested = true;
    }

    // UDP-сокет, що слухає порт на всіх інтерфейсах. recv() чекає не довше за 100 мс, щоб помічати Ctrl+C
    pi_tracker::Fd openUdpListener(std::uint16_t port)
    {
        pi_tracker::Fd fd{::socket(AF_INET, SOCK_DGRAM, 0)};
        if (!fd.isValid())
        {
            throw std::system_error(errno, std::generic_category(), "socket");
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        if (::bind(fd.get(), reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0)
        {
            throw std::system_error(errno, std::generic_category(), std::format("bind UDP port {}", port));
        }
        const timeval timeout{0, 100'000};
        if (::setsockopt(fd.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0)
        {
            throw std::system_error(errno, std::generic_category(), "setsockopt SO_RCVTIMEO");
        }
        return fd;
    }

    double degrees(float radians)
    {
        return static_cast<double>(radians) * 180.0 / std::numbers::pi;
    }

    // Останнє відоме про дрон + скільки повідомлень прийшло за секунду
    struct Telemetry
    {
        std::optional<mav::Heartbeat> heartbeat;
        std::optional<mav::Attitude> attitude;
        std::optional<mav::GlobalPosition> position;
        std::optional<mav::SysStatus> status;
        Clock::time_point lastHeartbeat{};
        int heartbeats{};
        int attitudes{};
        int positions{};

        void apply(const mav::Frame &frame)
        {
            if (auto hb = mav::decodeHeartbeat(frame))
            {
                heartbeat = hb;
                lastHeartbeat = Clock::now();
                ++heartbeats;
            }
            else if (auto att = mav::decodeAttitude(frame))
            {
                attitude = att;
                ++attitudes;
            }
            else if (auto pos = mav::decodeGlobalPosition(frame))
            {
                position = pos;
                ++positions;
            }
            else if (auto st = mav::decodeSysStatus(frame))
            {
                status = st;
            }
        }

        std::string summary(const mav::ParseStats &stats) const
        {
            if (!heartbeat || Clock::now() - lastHeartbeat > 3s)
            {
                return std::format("NO LINK (no HEARTBEAT for 3 s) | bad CRC {}, skipped bytes {}", stats.badCrc,
                                   stats.skippedBytes);
            }
            std::string text = std::format("{:9} | {:8}", mav::copterModeName(heartbeat->customMode),
                                           heartbeat->armed() ? "ARMED" : "disarmed");
            if (position)
            {
                text += std::format(" | alt {:5.1f} m", position->relativeAltM);
            }
            if (attitude)
            {
                text += std::format(" | roll {:+5.1f} pitch {:+5.1f} yaw {:+6.1f} deg", degrees(attitude->roll),
                                    degrees(attitude->pitch), degrees(attitude->yaw));
            }
            if (status)
            {
                text += std::format(" | {:.2f} V {}", status->voltageV,
                                    status->batteryPercent ? std::format("{}%", *status->batteryPercent) : "?%");
            }
            text += std::format(" | {}/{}/{} msg/s | bad CRC {}", heartbeats, attitudes, positions, stats.badCrc);
            return text;
        }
    };

} // namespace

int main(int argc, char *argv[])
{
    const auto port = static_cast<std::uint16_t>(argc > 1 ? std::stoi(argv[1]) : 14550);
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    try
    {
        const pi_tracker::Fd socket = openUdpListener(port);
        std::cerr << std::format("[mavlink] listening on UDP {}\n", port);

        std::array<std::uint8_t, 2048> buffer{}; // кадр MAVLink 2 — не більше 280 байтів
        mav::ParseStats stats;
        Telemetry telemetry;
        Clock::time_point nextReport = Clock::now() + 1s;
        while (!g_stopRequested)
        {
            const ssize_t received = ::recv(socket.get(), buffer.data(), buffer.size(), 0);
            if (received > 0)
            {
                const auto datagram = std::span{buffer}.first(static_cast<std::size_t>(received));
                for (const mav::Frame &frame : mav::parseDatagram(datagram, stats))
                {
                    telemetry.apply(frame);
                }
            }
            else if (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            {
                throw std::system_error(errno, std::generic_category(), "recv");
            }

            if (Clock::now() >= nextReport)
            {
                std::cout << telemetry.summary(stats) << std::endl;
                telemetry.heartbeats = telemetry.attitudes = telemetry.positions = 0;
                nextReport += 1s;
            }
        }
        std::cerr << std::format("[mavlink] stopped: {} frames, {} bad CRC, {} unknown, {} skipped bytes\n",
                                 stats.frames, stats.badCrc, stats.unknown, stats.skippedBytes);
    }
    catch (const std::exception &e)
    {
        std::cerr << "fatal: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
