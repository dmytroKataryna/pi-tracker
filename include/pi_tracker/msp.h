#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// MSP v1 (MultiWii Serial Protocol) — протокол Betaflight/INAV. Модель «запит → відповідь»:
//   запит:     '$' 'M' '<'  розмір  команда  [дані...]  контрольна_сума
//   відповідь: '$' 'M' '>'  розмір  команда  [дані...]  контрольна_сума   ('!' замість '>' — помилка)
// Контрольна сума = XOR байтів «розмір», «команда» і всіх байтів даних. Числа — little-endian
namespace pi_tracker::msp
{

    // Коди команд (лише ті, що читають; команди MSP_SET_* змінюють налаштування контролера)
    inline constexpr std::uint8_t kApiVersion = 1;
    inline constexpr std::uint8_t kFcVariant = 2; // "BTFL" — Betaflight
    inline constexpr std::uint8_t kFcVersion = 3;
    inline constexpr std::uint8_t kRawImu = 102;
    inline constexpr std::uint8_t kAttitude = 108;

    // Готовий кадр запиту: "$M<", розмір, команда, дані, контрольна сума
    std::vector<std::uint8_t> encodeRequest(std::uint8_t command, std::span<const std::uint8_t> payload = {});

    struct Frame
    {
        std::uint8_t command{};
        bool isError{}; // контролер відповів '!': не знає команди або не зміг її виконати
        std::vector<std::uint8_t> payload;
    };

    // Збирає кадри з потоку байтів. На відміну від TF-Luna (завжди 9 байтів), довжина кадру MSP
    // записана в самому кадрі, тому парсер — скінченний автомат: стан підказує, що означає наступний байт
    class Parser
    {
    public:
        // Подати наступний байт. Повертає кадр, коли він зібраний і контрольна сума збіглася
        std::optional<Frame> feed(std::uint8_t byte);
        std::size_t checksumErrors() const { return m_checksumErrors; }

    private:
        enum class State
        {
            Dollar,    // чекаємо '$' — початок кадру
            M,         // чекаємо 'M'
            Direction, // '>' (відповідь) або '!' (помилка)
            Size,      // розмір даних
            Command,   // код команди
            Payload,   // дані: рівно m_size байтів
            Checksum,  // контрольна сума
        };

        State m_state{State::Dollar};
        Frame m_frame;              // кадр, що збирається зараз
        std::uint8_t m_size{};      // скільки байтів даних оголошено в кадрі
        std::uint8_t m_checksum{};  // XOR усіх байтів від «розміру» до кінця даних
        std::size_t m_checksumErrors{};
    };

    struct ApiVersion
    {
        std::uint8_t protocol{};
        std::uint8_t major{};
        std::uint8_t minor{};
    };

    struct FcVersion
    {
        std::uint8_t major{};
        std::uint8_t minor{};
        std::uint8_t patch{};
    };

    struct Attitude
    {
        double rollDeg{};  // у кадрі: int16, десяті частки градуса
        double pitchDeg{}; // у кадрі: int16, десяті частки градуса
        double yawDeg{};   // у кадрі: int16, цілі градуси 0..359
    };

    struct RawImu
    {
        std::array<std::int16_t, 3> acc{};  // x, y, z; для ICM-42688P 2048 одиниць ≈ 1 g
        std::array<std::int16_t, 3> gyro{}; // x, y, z; градуси за секунду
        std::array<std::int16_t, 3> mag{};  // магнітометра на цій платі немає — нулі
    };

    // Розбір даних відповіді. std::nullopt — інша команда, помилка або замало байтів
    std::optional<ApiVersion> decodeApiVersion(const Frame &frame);
    std::optional<std::string> decodeFcVariant(const Frame &frame);
    std::optional<FcVersion> decodeFcVersion(const Frame &frame);
    std::optional<Attitude> decodeAttitude(const Frame &frame);
    std::optional<RawImu> decodeRawImu(const Frame &frame);

} // namespace pi_tracker::msp
