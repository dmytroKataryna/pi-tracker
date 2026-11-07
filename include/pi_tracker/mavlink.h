#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// Мінімальний розбір MAVLink 2: рамка, контрольна сума і чотири повідомлення телеметрії.
// Справжні проєкти беруть згенеровану бібліотеку MAVLink; тут пишемо самі, щоб побачити, як влаштований протокол
namespace pi_tracker::mavlink
{

    // Числа в MAVLink — little-endian: молодший байт іде першим
    std::uint16_t readU16Le(std::span<const std::uint8_t> bytes, std::size_t offset);
    std::uint32_t readU32Le(std::span<const std::uint8_t> bytes, std::size_t offset);
    std::int16_t readI16Le(std::span<const std::uint8_t> bytes, std::size_t offset);
    std::int32_t readI32Le(std::span<const std::uint8_t> bytes, std::size_t offset);
    float readFloatLe(std::span<const std::uint8_t> bytes, std::size_t offset);

    // Контрольна сума MAVLink (CRC-16/MCRF4XX): починається з 0xFFFF, додаємо по байту
    std::uint16_t crcAccumulate(std::uint8_t byte, std::uint16_t crc);

    // Один перевірений кадр
    struct Frame
    {
        std::uint8_t sequence{};    // лічильник кадрів відправника: пропуск = загублений пакет
        std::uint8_t systemId{};    // хто відправив: 1 — дрон
        std::uint8_t componentId{}; // який компонент: 1 — автопілот
        std::uint32_t messageId{};
        std::vector<std::uint8_t> payload; // уже доповнений нулями до повної довжини повідомлення
    };

    struct ParseStats
    {
        std::size_t frames{};       // кадри з правильною контрольною сумою
        std::size_t badCrc{};       // кадри, що не пройшли перевірку CRC
        std::size_t unknown{};      // кадри повідомлень, яких ми не знаємо (їх пропускаємо)
        std::size_t skippedBytes{}; // байти поза кадрами (наприклад, текст завантаження)
    };

    // Розбирає одну UDP-датаграму: у ній може бути кілька кадрів і сміття між ними
    std::vector<Frame> parseDatagram(std::span<const std::uint8_t> datagram, ParseStats &stats);

    // HEARTBEAT (id 0), раз на секунду: «я живий» + режим і стан
    struct Heartbeat
    {
        std::uint32_t customMode{}; // режим польоту ArduPilot: 0 — STABILIZE, 4 — GUIDED, 9 — LAND, ...
        std::uint8_t type{};        // 2 — квадрокоптер
        std::uint8_t autopilot{};   // 3 — ArduPilot
        std::uint8_t baseMode{};    // прапорці; біт 7 (0x80) — мотори увімкнені (armed)
        std::uint8_t systemStatus{};
        bool armed() const { return (baseMode & 0x80) != 0; }
    };

    // ATTITUDE (id 30): кути в радіанах
    struct Attitude
    {
        std::uint32_t timeBootMs{};
        float roll{};
        float pitch{};
        float yaw{};
    };

    // GLOBAL_POSITION_INT (id 33): позиція і швидкість, уже в звичних одиницях
    struct GlobalPosition
    {
        std::uint32_t timeBootMs{};
        double latDeg{};
        double lonDeg{};
        float altM{};         // над рівнем моря
        float relativeAltM{}; // над точкою зльоту
        float vxMS{};         // на північ
        float vyMS{};         // на схід
        float vzMS{};         // униз (!)
        std::optional<float> headingDeg; // курс 0…360; std::nullopt — невідомий
    };

    // SYS_STATUS (id 1): батарея
    struct SysStatus
    {
        float voltageV{};
        std::optional<float> currentA;   // std::nullopt — датчик струму не підключений
        std::optional<int> batteryPercent; // std::nullopt — невідомо
    };

    // std::nullopt, якщо кадр — інше повідомлення
    std::optional<Heartbeat> decodeHeartbeat(const Frame &frame);
    std::optional<Attitude> decodeAttitude(const Frame &frame);
    std::optional<GlobalPosition> decodeGlobalPosition(const Frame &frame);
    std::optional<SysStatus> decodeSysStatus(const Frame &frame);

    // "GUIDED", "LAND", ... для ArduCopter
    std::string_view copterModeName(std::uint32_t customMode);

} // namespace pi_tracker::mavlink
