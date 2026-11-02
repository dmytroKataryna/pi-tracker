#include "pi_tracker/h264_nal.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{

    int g_failures = 0;

    void fail(const std::string &message)
    {
        ++g_failures;
        std::cout << "FAIL " << message << '\n';
    }

    void expectCount(const std::vector<pi_tracker::NalUnit> &units, std::size_t expected, std::string_view test)
    {
        if (units.size() != expected)
        {
            fail(std::format("{}: {} units, expected {}", test, units.size(), expected));
        }
    }

    void expectUnit(const std::vector<pi_tracker::NalUnit> &units, std::size_t index, std::size_t offset,
                    std::size_t size, int type, int refIdc, std::string_view test)
    {
        if (index >= units.size())
        {
            fail(std::format("{}: unit {} missing", test, index));
            return;
        }
        const pi_tracker::NalUnit &unit = units[index];
        if (unit.offset != offset || unit.bytes.size() != size || unit.type() != type || unit.refIdc() != refIdc)
        {
            fail(std::format("{}: unit {} is offset {} size {} type {} ref {}, expected {} {} {} {}", test, index,
                             unit.offset, unit.bytes.size(), unit.type(), unit.refIdc(), offset, size, type, refIdc));
        }
    }

} // namespace

int main()
{
    // Кожен потік — іменована змінна: NalUnit::bytes дивиться в неї
    const std::vector<std::uint8_t> empty;
    expectCount(pi_tracker::splitAnnexB(empty), 0, "empty");

    const std::vector<std::uint8_t> noStartCode{0x12, 0x34, 0x00, 0x00, 0x02, 0x67};
    expectCount(pi_tracker::splitAnnexB(noStartCode), 0, "no start code");

    // SPS після 4-байтового стартового коду. 0x67 = 0 11 00111: ref 3, type 7
    const std::vector<std::uint8_t> sps{0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xC0, 0x1F};
    const auto spsUnits = pi_tracker::splitAnnexB(sps);
    expectCount(spsUnits, 1, "sps");
    expectUnit(spsUnits, 0, 4, 4, 7, 3, "sps");

    // 4-байтовий, 3-байтовий і знову 4-байтовий коди: нуль перед 00 00 01 не належить попередньому NAL
    const std::vector<std::uint8_t> mixed{
        0x00,
        0x00,
        0x00,
        0x01,
        0x67,
        0xAA, // SPS
        0x00,
        0x00,
        0x01,
        0x68,
        0xBB, // PPS
        0x00,
        0x00,
        0x00,
        0x01,
        0x65,
        0xCC,
        0xDD, // IDR
    };
    const auto mixedUnits = pi_tracker::splitAnnexB(mixed);
    expectCount(mixedUnits, 3, "mixed");
    expectUnit(mixedUnits, 0, 4, 2, 7, 3, "mixed");
    expectUnit(mixedUnits, 1, 9, 2, 8, 3, "mixed");
    expectUnit(mixedUnits, 2, 15, 3, 5, 3, "mixed");

    // Сміття до першого стартового коду пропускаємо. 0x41 = 0 10 00001: ref 2, type 1
    const std::vector<std::uint8_t> garbage{0x12, 0x34, 0x00, 0x00, 0x01, 0x41, 0x9A};
    const auto garbageUnits = pi_tracker::splitAnnexB(garbage);
    expectCount(garbageUnits, 1, "garbage");
    expectUnit(garbageUnits, 0, 5, 2, 1, 2, "garbage");

    // Нулі в кінці NAL і стартовий код у самому кінці потоку. 0x06 = 0 00 00110: ref 0, type 6
    const std::vector<std::uint8_t> tail{0x00, 0x00, 0x01, 0x06, 0x05, 0x00, 0x00, 0x00, 0x01};
    const auto tailUnits = pi_tracker::splitAnnexB(tail);
    expectCount(tailUnits, 1, "tail");
    expectUnit(tailUnits, 0, 3, 2, 6, 0, "tail");

    // Два стартові коди поспіль: порожній NAL не повертаємо
    const std::vector<std::uint8_t> emptyNal{0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x67, 0xAA};
    const auto emptyNalUnits = pi_tracker::splitAnnexB(emptyNal);
    expectCount(emptyNalUnits, 1, "empty nal");
    expectUnit(emptyNalUnits, 0, 6, 2, 7, 3, "empty nal");

    // 00 00 03 — emulation prevention: усередині NAL послідовність 00 00 01 неможлива, кодер вставляє 03
    const std::vector<std::uint8_t> emulation{0x00, 0x00, 0x01, 0x65, 0x00, 0x00, 0x03, 0x01, 0xFF};
    const auto emulationUnits = pi_tracker::splitAnnexB(emulation);
    expectCount(emulationUnits, 1, "emulation");
    expectUnit(emulationUnits, 0, 3, 6, 5, 3, "emulation");

    // startsFrame: старший біт байта після заголовка слайсу. 0x9A = 1…, 0x3F = 0…
    const std::vector<std::uint8_t> slices{
        0x00,
        0x00,
        0x01,
        0x41,
        0x9A, // перший слайс кадру
        0x00,
        0x00,
        0x01,
        0x41,
        0x3F, // продовження того самого кадру
        0x00,
        0x00,
        0x01,
        0x67,
        0xFF, // SPS — не кадр, хоч старший біт і 1
    };
    const auto sliceUnits = pi_tracker::splitAnnexB(slices);
    expectCount(sliceUnits, 3, "slices");
    if (sliceUnits.size() == 3 && !(sliceUnits[0].startsFrame() && !sliceUnits[1].startsFrame() && !sliceUnits[2].startsFrame()))
    {
        fail("slices: startsFrame should be true, false, false");
    }

    if (g_failures == 0)
    {
        std::cout << "all tests passed\n";
        return EXIT_SUCCESS;
    }
    std::cout << std::format("{} check(s) failed\n", g_failures);
    return EXIT_FAILURE;
}