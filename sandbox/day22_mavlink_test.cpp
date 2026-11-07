#include "pi_tracker/mavlink.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <format>
#include <iostream>
#include <string>
#include <vector>

namespace mav = pi_tracker::mavlink;

namespace
{

    int g_failures = 0;

    void check(bool ok, const std::string &message)
    {
        if (!ok)
        {
            ++g_failures;
            std::cout << "FAIL " << message << '\n';
        }
    }

    bool near(double a, double b, double tolerance)
    {
        return std::abs(a - b) < tolerance;
    }

    template <typename Test>
    void runCase(const std::string &name, Test test)
    {
        try
        {
            test();
        }
        catch (const std::exception &e)
        {
            check(false, std::format("{}: unexpected exception: {}", name, e.what()));
        }
    }

    // Справжні датаграми з SITL на висоті 10 м (записані 2026-11-06), очікувані значення — з pymavlink
    const std::vector<std::uint8_t> kHeartbeat{0xFD, 0x09, 0x00, 0x00, 0xB8, 0x01, 0x01, 0x00, 0x00, 0x00, 0x04,
                                               0x00, 0x00, 0x00, 0x02, 0x03, 0xD9, 0x04, 0x03, 0x95, 0xF8};
    const std::vector<std::uint8_t> kSysStatus{0xFD, 0x1F, 0x00, 0x00, 0xB9, 0x01, 0x01, 0x01, 0x00, 0x00, 0x2F,
                                               0xFC, 0x71, 0x53, 0x2F, 0xFC, 0x61, 0x53, 0x2F, 0xFC, 0x71, 0x57,
                                               0x00, 0x00, 0x18, 0x30, 0x3B, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00,
                                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x26, 0xF0};
    const std::vector<std::uint8_t> kAttitude{0xFD, 0x1C, 0x00, 0x00, 0x8D, 0x01, 0x01, 0x1E, 0x00, 0x00,
                                              0xF2, 0x15, 0x01, 0x00, 0x76, 0x80, 0x5F, 0x3B, 0xF5, 0xEE,
                                              0x5B, 0x3B, 0xB0, 0xF6, 0x01, 0xBE, 0xE9, 0xD6, 0xED, 0x3A,
                                              0xC4, 0x74, 0x22, 0x3B, 0xC5, 0x39, 0x89, 0x3A, 0x7B, 0x84};
    const std::vector<std::uint8_t> kGlobalPosition{0xFD, 0x1C, 0x00, 0x00, 0x90, 0x01, 0x01, 0x21, 0x00, 0x00,
                                                    0x54, 0x16, 0x01, 0x00, 0x94, 0xFE, 0xEB, 0xEA, 0x13, 0xCF,
                                                    0xE8, 0x58, 0x8C, 0x10, 0x09, 0x00, 0x09, 0x27, 0x00, 0x00,
                                                    0xFF, 0xFF, 0x02, 0x00, 0x00, 0x00, 0xC9, 0x89, 0x2B, 0x81};

    std::vector<mav::Frame> parseOne(const std::vector<std::uint8_t> &datagram, mav::ParseStats &stats)
    {
        return mav::parseDatagram(datagram, stats);
    }

} // namespace

int main()
{
    // 1. Порядок байтів: молодший байт першим
    runCase("little-endian", [] {
        const std::vector<std::uint8_t> bytes{0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0xFE, 0xFF};
        check(mav::readU16Le(bytes, 0) == 0x1234, std::format("readU16Le: 0x{:X}, expected 0x1234", mav::readU16Le(bytes, 0)));
        check(mav::readU32Le(bytes, 2) == 0x12345678, std::format("readU32Le: 0x{:X}, expected 0x12345678", mav::readU32Le(bytes, 2)));
        check(mav::readU16Le(bytes, 6) == 0xFFFE, std::format("readU16Le: 0x{:X}, expected 0xFFFE", mav::readU16Le(bytes, 6)));
        check(mav::readI16Le(bytes, 6) == -2, std::format("readI16Le: {}, expected -2", mav::readI16Le(bytes, 6)));
    });

    // 2. HEARTBEAT: GUIDED, мотори увімкнені
    runCase("heartbeat", [] {
        mav::ParseStats stats;
        const auto frames = parseOne(kHeartbeat, stats);
        check(frames.size() == 1 && stats.badCrc == 0, std::format("heartbeat: {} frames, {} bad CRC, expected 1 / 0", frames.size(), stats.badCrc));
        if (frames.size() != 1)
        {
            return;
        }
        check(frames[0].systemId == 1 && frames[0].messageId == 0, "heartbeat: sysid 1, msgid 0");
        const auto hb = mav::decodeHeartbeat(frames[0]);
        check(hb.has_value(), "heartbeat: decodeHeartbeat returned nullopt");
        if (hb)
        {
            check(hb->customMode == 4 && hb->armed() && hb->type == 2 && hb->autopilot == 3,
                  std::format("heartbeat: mode {} armed {} type {} autopilot {}, expected 4 / true / 2 / 3",
                              hb->customMode, hb->armed(), hb->type, hb->autopilot));
        }
        check(!mav::decodeAttitude(frames[0]).has_value(), "heartbeat: decodeAttitude must return nullopt for a HEARTBEAT");
    });

    // 3. ATTITUDE: float-и
    runCase("attitude", [] {
        mav::ParseStats stats;
        const auto frames = parseOne(kAttitude, stats);
        const auto att = frames.empty() ? std::nullopt : mav::decodeAttitude(frames[0]);
        check(att.has_value(), "attitude: no ATTITUDE decoded");
        if (att)
        {
            check(att->timeBootMs == 71154 && near(att->yaw, -0.12691760063171387, 1e-7) && near(att->roll, 0.003410366829484701, 1e-9),
                  std::format("attitude: time {} roll {} yaw {}, expected 71154 / 0.0034104 / -0.1269176", att->timeBootMs, att->roll, att->yaw));
        }
    });

    // 4. GLOBAL_POSITION_INT: знакові цілі і масштаби
    runCase("global position", [] {
        mav::ParseStats stats;
        const auto frames = parseOne(kGlobalPosition, stats);
        const auto pos = frames.empty() ? std::nullopt : mav::decodeGlobalPosition(frames[0]);
        check(pos.has_value(), "position: no GLOBAL_POSITION_INT decoded");
        if (pos)
        {
            check(near(pos->latDeg, -35.3632620, 1e-9) && near(pos->lonDeg, 149.1652371, 1e-9),
                  std::format("position: lat {:.7f} lon {:.7f}, expected -35.3632620 / 149.1652371", pos->latDeg, pos->lonDeg));
            check(near(pos->altM, 594.06, 1e-4) && near(pos->relativeAltM, 9.993, 1e-4),
                  std::format("position: alt {:.3f} relative {:.3f}, expected 594.060 / 9.993", pos->altM, pos->relativeAltM));
            check(near(pos->vxMS, -0.01, 1e-6) && near(pos->vyMS, 0.02, 1e-6) && near(pos->vzMS, 0.0, 1e-6),
                  std::format("position: v {:.2f} {:.2f} {:.2f}, expected -0.01 0.02 0.00", pos->vxMS, pos->vyMS, pos->vzMS));
            check(pos->headingDeg.has_value() && near(*pos->headingDeg, 352.73, 1e-4),
                  std::format("position: heading {}, expected 352.73", pos->headingDeg ? std::format("{:.2f}", *pos->headingDeg) : "nullopt"));
        }
    });

    // 5. SYS_STATUS: кадр обрізаний (31 байт замість 43) — нулі в кінці не передаються
    runCase("sys status", [] {
        mav::ParseStats stats;
        const auto frames = parseOne(kSysStatus, stats);
        const auto status = frames.empty() ? std::nullopt : mav::decodeSysStatus(frames[0]);
        check(status.has_value(), "sys status: no SYS_STATUS decoded");
        if (status)
        {
            check(frames[0].payload.size() == 43, std::format("sys status: payload {} bytes, expected 43 after zero-fill", frames[0].payload.size()));
            check(near(status->voltageV, 12.312, 1e-4) && status->currentA && near(*status->currentA, 28.75, 1e-4) &&
                      status->batteryPercent == 96,
                  std::format("sys status: {:.3f} V, {} A, {} %, expected 12.312 / 28.75 / 96", status->voltageV,
                              status->currentA ? *status->currentA : -999.0f, status->batteryPercent.value_or(-999)));
        }
    });

    // 6. Сміття перед кадром (текст завантаження ArduPilot) і два кадри в одній датаграмі
    runCase("garbage and two frames", [] {
        const std::string text = "Init ArduCopter\n";
        std::vector<std::uint8_t> datagram(text.begin(), text.end());
        datagram.insert(datagram.end(), kHeartbeat.begin(), kHeartbeat.end());
        datagram.insert(datagram.end(), kAttitude.begin(), kAttitude.end());
        mav::ParseStats stats;
        const auto frames = mav::parseDatagram(datagram, stats);
        check(frames.size() == 2 && stats.skippedBytes == text.size(),
              std::format("garbage: {} frames, {} skipped bytes, expected 2 / {}", frames.size(), stats.skippedBytes, text.size()));
    });

    // 7. Зіпсований байт у payload: CRC не збігається — кадр відкидаємо
    runCase("bad crc", [] {
        std::vector<std::uint8_t> damaged = kAttitude;
        damaged[20] ^= 0x01;
        mav::ParseStats stats;
        const auto frames = mav::parseDatagram(damaged, stats);
        check(frames.empty() && stats.badCrc == 1, std::format("bad crc: {} frames, {} bad CRC, expected 0 / 1", frames.size(), stats.badCrc));
    });

    if (g_failures == 0)
    {
        std::cout << "all tests passed\n";
        return EXIT_SUCCESS;
    }
    std::cout << std::format("{} check(s) failed\n", g_failures);
    return EXIT_FAILURE;
}
