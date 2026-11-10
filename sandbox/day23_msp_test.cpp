#include "pi_tracker/msp.h"

#include <cmath>
#include <cstdint>
#include <exception>
#include <format>
#include <iostream>
#include <string>
#include <vector>

namespace msp = pi_tracker::msp;

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

    // Справжні відповіді SpeedyBee F405 V4 (Betaflight 4.5.1), записані 2026-11-09 через USB на Mac
    const std::vector<std::uint8_t> kApiVersion{0x24, 0x4d, 0x3e, 0x03, 0x01, 0x00, 0x01, 0x2e, 0x2d};
    const std::vector<std::uint8_t> kFcVariant{0x24, 0x4d, 0x3e, 0x04, 0x02, 0x42, 0x54, 0x46, 0x4c, 0x1a};
    const std::vector<std::uint8_t> kFcVersion{0x24, 0x4d, 0x3e, 0x03, 0x03, 0x04, 0x05, 0x01, 0x00};
    const std::vector<std::uint8_t> kAttitude{0x24, 0x4d, 0x3e, 0x06, 0x6c, 0x02, 0x00,
                                              0x07, 0x00, 0x88, 0x00, 0xe7};
    const std::vector<std::uint8_t> kRawImu{0x24, 0x4d, 0x3e, 0x12, 0x66, 0xe6, 0xff, 0x09, 0x00, 0x0d, 0x08, 0x00,
                                            0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60};
    // Відповідь без даних (розмір 0) — так Betaflight відповідає на команду, якої не підтримує
    const std::vector<std::uint8_t> kEmpty{0x24, 0x4d, 0x3e, 0x00, 0x25, 0x25};
    // Кадр помилки: '!' замість '>' (складений вручну за специфікацією)
    const std::vector<std::uint8_t> kError{0x24, 0x4d, 0x21, 0x00, 0x6c, 0x6c};

    // Подає байти по одному й збирає всі кадри, що повернув парсер
    std::vector<msp::Frame> feedAll(msp::Parser &parser, const std::vector<std::uint8_t> &bytes)
    {
        std::vector<msp::Frame> frames;
        for (const std::uint8_t byte : bytes)
        {
            if (auto frame = parser.feed(byte))
            {
                frames.push_back(std::move(*frame));
            }
        }
        return frames;
    }

    std::vector<std::uint8_t> concat(std::vector<std::uint8_t> a, const std::vector<std::uint8_t> &b)
    {
        a.insert(a.end(), b.begin(), b.end());
        return a;
    }

} // namespace

int main()
{
    // 1. Запит: той самий байт у байт, що надсилає Betaflight Configurator
    runCase("encode", [] {
        const auto request = msp::encodeRequest(msp::kAttitude);
        const std::vector<std::uint8_t> expected{0x24, 0x4d, 0x3c, 0x00, 0x6c, 0x6c};
        check(request == expected, "encode: MSP_ATTITUDE request must be 24 4d 3c 00 6c 6c");
    });

    // 2. Найпростіший кадр: 3 байти даних
    runCase("api version", [] {
        msp::Parser parser;
        const auto frames = feedAll(parser, kApiVersion);
        check(frames.size() == 1, std::format("api version: {} frames, expected 1", frames.size()));
        if (frames.size() != 1)
        {
            return;
        }
        check(frames[0].command == msp::kApiVersion && !frames[0].isError && frames[0].payload.size() == 3,
              std::format("api version: command {} error {} size {}, expected 1 / false / 3", frames[0].command,
                          frames[0].isError, frames[0].payload.size()));
        const auto api = msp::decodeApiVersion(frames[0]);
        check(api && api->protocol == 0 && api->major == 1 && api->minor == 46, "api version: expected 0 / 1.46");
    });

    // 3. Текст і версія прошивки
    runCase("variant and version", [] {
        msp::Parser parser;
        const auto frames = feedAll(parser, concat(kFcVariant, kFcVersion));
        check(frames.size() == 2, std::format("variant+version: {} frames, expected 2", frames.size()));
        if (frames.size() != 2)
        {
            return;
        }
        const auto variant = msp::decodeFcVariant(frames[0]);
        check(variant && *variant == "BTFL", "variant: expected BTFL");
        const auto version = msp::decodeFcVersion(frames[1]);
        check(version && version->major == 4 && version->minor == 5 && version->patch == 1, "version: expected 4.5.1");
    });

    // 4. Положення: roll/pitch у десятих частках градуса, yaw у градусах
    runCase("attitude", [] {
        msp::Parser parser;
        const auto frames = feedAll(parser, kAttitude);
        check(frames.size() == 1, std::format("attitude: {} frames, expected 1", frames.size()));
        if (frames.size() != 1)
        {
            return;
        }
        const auto attitude = msp::decodeAttitude(frames[0]);
        check(attitude.has_value(), "attitude: decodeAttitude returned nullopt");
        if (attitude)
        {
            check(std::abs(attitude->rollDeg - 0.2) < 1e-9 && std::abs(attitude->pitchDeg - 0.7) < 1e-9 &&
                      std::abs(attitude->yawDeg - 136.0) < 1e-9,
                  std::format("attitude: roll {} pitch {} yaw {}, expected 0.2 / 0.7 / 136", attitude->rollDeg,
                              attitude->pitchDeg, attitude->yawDeg));
        }
        check(!msp::decodeRawImu(frames[0]).has_value(), "attitude: decodeRawImu must return nullopt for ATTITUDE");
    });

    // 5. Сирі дані IMU: плата лежить рівно, тож прискорення ≈ 1 g по осі z
    runCase("raw imu", [] {
        msp::Parser parser;
        const auto frames = feedAll(parser, kRawImu);
        const auto imu = frames.size() == 1 ? msp::decodeRawImu(frames[0]) : std::nullopt;
        check(imu.has_value(), "raw imu: no frame or decodeRawImu returned nullopt");
        if (imu)
        {
            check(imu->acc[0] == -26 && imu->acc[1] == 9 && imu->acc[2] == 2061,
                  std::format("raw imu: acc {} {} {}, expected -26 9 2061", imu->acc[0], imu->acc[1], imu->acc[2]));
            check(imu->gyro[0] == 0 && imu->gyro[1] == 1 && imu->gyro[2] == 0, "raw imu: gyro expected 0 1 0");
        }
    });

    // 6. Кадр без даних: після «команди» одразу йде контрольна сума
    runCase("empty payload", [] {
        msp::Parser parser;
        const auto frames = feedAll(parser, kEmpty);
        check(frames.size() == 1 && frames[0].command == 37 && frames[0].payload.empty(),
              "empty payload: expected 1 frame, command 37, no data");
    });

    // 7. Кадр помилки '!'
    runCase("error frame", [] {
        msp::Parser parser;
        const auto frames = feedAll(parser, kError);
        check(frames.size() == 1 && frames[0].isError && frames[0].command == msp::kAttitude,
              "error frame: expected 1 frame with isError == true, command 108");
        if (frames.size() == 1)
        {
            check(!msp::decodeAttitude(frames[0]).has_value(), "error frame: decodeAttitude must return nullopt");
        }
    });

    // 8. Сміття перед кадром (у тому числі '$' і "$M" без продовження) — парсер має знайти справжній початок
    runCase("garbage", [] {
        msp::Parser parser;
        const auto frames = feedAll(parser, concat({0x00, 0xff, '$', 0x13, '$', 'M', 'x', 0x42}, kAttitude));
        check(frames.size() == 1 && frames[0].command == msp::kAttitude,
              std::format("garbage: {} frames, expected 1 ATTITUDE", frames.size()));
    });

    // 9. Зіпсований байт даних: кадр відкидається, лічильник помилок росте, наступний кадр приймається
    runCase("bad checksum", [] {
        msp::Parser parser;
        auto broken = kAttitude;
        broken[6] ^= 0x01;
        const auto frames = feedAll(parser, concat(broken, kApiVersion));
        check(frames.size() == 1 && frames[0].command == msp::kApiVersion,
              std::format("bad checksum: {} frames, expected only API_VERSION", frames.size()));
        check(parser.checksumErrors() == 1,
              std::format("bad checksum: {} checksum errors, expected 1", parser.checksumErrors()));
    });

    // 10. Кадр приходить двома шматками (як буває з read() з порту) — парсер зберігає стан між викликами
    runCase("split", [] {
        msp::Parser parser;
        const std::vector<std::uint8_t> first(kRawImu.begin(), kRawImu.begin() + 10);
        const std::vector<std::uint8_t> second(kRawImu.begin() + 10, kRawImu.end());
        const auto a = feedAll(parser, first);
        const auto b = feedAll(parser, second);
        check(a.empty() && b.size() == 1 && b[0].command == msp::kRawImu,
              std::format("split: {} + {} frames, expected 0 + 1", a.size(), b.size()));
    });

    if (g_failures == 0)
    {
        std::cout << "all tests passed\n";
        return 0;
    }
    std::cout << g_failures << " check(s) failed\n";
    return 1;
}