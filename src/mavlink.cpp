#include "pi_tracker/mavlink.h"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>

namespace pi_tracker::mavlink
{

    namespace
    {
        // Рамка MAVLink 2:
        // [0] 0xFD  [1] довжина payload  [2] incompat-прапорці  [3] compat-прапорці  [4] seq  [5] sysid  [6] compid
        // [7..9] msgid (3 байти, little-endian)  [10 …] payload  [ще 2] CRC  [ще 13] підпис, якщо incompat & 0x01
        constexpr std::uint8_t kStx = 0xFD;
        constexpr std::size_t kHeaderLen = 10;
        constexpr std::size_t kCrcLen = 2;
        constexpr std::size_t kSignatureLen = 13;
        constexpr std::uint8_t kIncompatSigned = 0x01;

        // Для кожного повідомлення: CRC_EXTRA (хеш його опису, щоб обидві сторони мали однакову версію)
        // і повна довжина payload. Кінцеві нульові байти MAVLink 2 не передає, тому кадр буває коротшим
        struct MessageInfo
        {
            std::uint32_t id;
            std::uint8_t crcExtra;
            std::uint8_t length;
        };

        constexpr std::array<MessageInfo, 4> kKnownMessages{{
            {0, 50, 9},   // HEARTBEAT
            {1, 124, 43}, // SYS_STATUS (31 байт + розширення)
            {30, 39, 28}, // ATTITUDE
            {33, 104, 28} // GLOBAL_POSITION_INT
        }};

        const MessageInfo *findMessage(std::uint32_t id)
        {
            const auto it = std::ranges::find(kKnownMessages, id, &MessageInfo::id);
            return it == kKnownMessages.end() ? nullptr : &*it;
        }

        bool isMessage(const Frame &frame, std::uint32_t id)
        {
            const MessageInfo *info = findMessage(id);
            return frame.messageId == id && info != nullptr && frame.payload.size() >= info->length;
        }
    } // namespace

    std::uint16_t readU16Le(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
        std::span<const std::uint8_t> a = bytes.subspan(offset, 2);
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(a[1]) << 8) | a[0]);
    }

    std::uint32_t readU32Le(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
        std::span<const std::uint8_t> a = bytes.subspan(offset, 4);
        return static_cast<std::uint32_t>((static_cast<std::uint32_t>(a[3]) << 24) | (static_cast<std::uint32_t>(a[2]) << 16) | (static_cast<std::uint32_t>(a[1]) << 8) | a[0]);
    }

    std::int16_t readI16Le(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
        // З C++20 перетворення беззнакового в знаковий визначене: ті самі біти в доповняльному коді
        return static_cast<std::int16_t>(readU16Le(bytes, offset));
    }

    std::int32_t readI32Le(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
        return static_cast<std::int32_t>(readU32Le(bytes, offset));
    }

    float readFloatLe(std::span<const std::uint8_t> bytes, std::size_t offset)
    {
        // Ті самі 32 біти, прочитані як float. bit_cast — законний спосіб (reinterpret_cast тут був би UB)
        return std::bit_cast<float>(readU32Le(bytes, offset));
    }

    std::uint16_t crcAccumulate(std::uint8_t byte, std::uint16_t crc)
    {
        auto tmp = static_cast<std::uint8_t>(byte ^ static_cast<std::uint8_t>(crc & 0xFF));
        tmp = static_cast<std::uint8_t>(tmp ^ (tmp << 4));
        return static_cast<std::uint16_t>((crc >> 8) ^ (tmp << 8) ^ (tmp << 3) ^ (tmp >> 4));
    }

    std::vector<Frame> parseDatagram(std::span<const std::uint8_t> datagram, ParseStats &stats)
    {
        std::vector<Frame> frames;
        std::size_t pos = 0;
        while (pos < datagram.size())
        {
            if (datagram[pos] != kStx)
            {
                ++stats.skippedBytes; // не початок кадру: шукаємо далі
                ++pos;
                continue;
            }
            if (datagram.size() - pos < kHeaderLen + kCrcLen)
            {
                stats.skippedBytes += datagram.size() - pos; // обрізаний хвіст
                break;
            }
            const std::size_t length = datagram[pos + 1];
            const bool isSigned = (datagram[pos + 2] & kIncompatSigned) != 0;
            const std::size_t total = kHeaderLen + length + kCrcLen + (isSigned ? kSignatureLen : 0);
            if (pos + total > datagram.size())
            {
                ++stats.skippedBytes; // 0xFD виявився випадковим байтом, а не кадром
                ++pos;
                continue;
            }

            const std::uint32_t messageId = static_cast<std::uint32_t>(datagram[pos + 7]) |
                                            (static_cast<std::uint32_t>(datagram[pos + 8]) << 8) |
                                            (static_cast<std::uint32_t>(datagram[pos + 9]) << 16);
            const MessageInfo *info = findMessage(messageId);
            if (info == nullptr)
            {
                ++stats.unknown; // не знаємо CRC_EXTRA — перевірити не можемо, пропускаємо кадр цілком
                pos += total;
                continue;
            }

            // CRC рахується по всьому після 0xFD: заголовок + payload, і наприкінці CRC_EXTRA
            std::uint16_t crc = 0xFFFF;
            for (std::size_t i = pos + 1; i < pos + kHeaderLen + length; ++i)
            {
                crc = crcAccumulate(datagram[i], crc);
            }
            crc = crcAccumulate(info->crcExtra, crc);
            if (crc != readU16Le(datagram, pos + kHeaderLen + length))
            {
                ++stats.badCrc;
                ++pos;
                continue;
            }

            Frame frame;
            frame.sequence = datagram[pos + 4];
            frame.systemId = datagram[pos + 5];
            frame.componentId = datagram[pos + 6];
            frame.messageId = messageId;
            const auto payload = datagram.subspan(pos + kHeaderLen, length);
            frame.payload.assign(payload.begin(), payload.end());
            frame.payload.resize(std::max<std::size_t>(length, info->length), 0); // повертаємо обрізані нулі
            frames.push_back(std::move(frame));
            ++stats.frames;
            pos += total;
        }
        return frames;
    }

    // Поля в payload MAVLink відсортовані за розміром типу (спершу 4-байтові, потім 2-, потім 1-байтові)

    std::optional<Heartbeat> decodeHeartbeat(const Frame &frame)
    {
        if (!isMessage(frame, 0))
        {
            return std::nullopt;
        }
        const std::span<const std::uint8_t> p = frame.payload;
        return Heartbeat{readU32Le(p, 0), p[4], p[5], p[6], p[7]};
    }

    std::optional<Attitude> decodeAttitude(const Frame &frame)
    {
        if (!isMessage(frame, 30))
        {
            return std::nullopt;
        }
        const std::span<const std::uint8_t> p = frame.payload;
        return Attitude{readU32Le(p, 0), readFloatLe(p, 4), readFloatLe(p, 8), readFloatLe(p, 12)};
    }

    std::optional<GlobalPosition> decodeGlobalPosition(const Frame &frame)
    {
        if (!isMessage(frame, 33))
        {
            return std::nullopt;
        }
        const std::span<const std::uint8_t> p = frame.payload;
        GlobalPosition position;

        std::uint32_t time_boot_ms = readU32Le(p, 0);
        std::int32_t lat = readI32Le(p, 4);
        std::int32_t lon = readI32Le(p, 8);
        std::int32_t alt = readI32Le(p, 12);
        std::int32_t relative_alt = readI32Le(p, 16);
        std::int16_t vx = readI16Le(p, 20);
        std::int16_t vy = readI16Le(p, 22);
        std::int16_t vz = readI16Le(p, 24);
        std::uint16_t hdg = readU16Le(p, 26);

        position.timeBootMs = time_boot_ms;
        position.latDeg = lat / 1e7;
        position.lonDeg = lon / 1e7;
        position.altM = static_cast<float>(alt) / 1000;
        position.relativeAltM = static_cast<float>(relative_alt) / 1000;
        position.vxMS = static_cast<float>(vx) / 100;
        position.vyMS = static_cast<float>(vy) / 100;
        position.vzMS = static_cast<float>(vz) / 100;
        if (hdg == 65535)
        {
            position.headingDeg = std::nullopt;
        }
        else
        {
            position.headingDeg = static_cast<float>(hdg) / 100;
        }
        return position;
    }

    std::optional<SysStatus> decodeSysStatus(const Frame &frame)
    {
        if (!isMessage(frame, 1))
        {
            return std::nullopt;
        }
        const std::span<const std::uint8_t> p = frame.payload;
        SysStatus status;
        status.voltageV = static_cast<float>(readU16Le(p, 14)) / 1000.0f;
        const std::int16_t current = readI16Le(p, 16);
        if (current != -1)
        {
            status.currentA = static_cast<float>(current) / 100.0f;
        }
        const auto remaining = static_cast<std::int8_t>(p[30]);
        if (remaining != -1)
        {
            status.batteryPercent = remaining;
        }
        return status;
    }

    std::string_view copterModeName(std::uint32_t customMode)
    {
        switch (customMode)
        {
        case 0:
            return "STABILIZE";
        case 1:
            return "ACRO";
        case 2:
            return "ALT_HOLD";
        case 3:
            return "AUTO";
        case 4:
            return "GUIDED";
        case 5:
            return "LOITER";
        case 6:
            return "RTL";
        case 9:
            return "LAND";
        case 16:
            return "POSHOLD";
        default:
            return "OTHER";
        }
    }

} // namespace pi_tracker::mavlink
