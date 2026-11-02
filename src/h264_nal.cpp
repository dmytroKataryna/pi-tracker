#include "pi_tracker/h264_nal.h"
#include <cstddef>

namespace pi_tracker
{

    namespace
    {
        std::size_t findStartCode(std::span<const std::uint8_t> stream, std::size_t from)
        {
            for (std::size_t i = from; i + 2 < stream.size(); ++i)
            {
                if (stream[i] == 0x00 && stream[i + 1] == 0x00 && stream[i + 2] == 0x01)
                {
                    return i;
                }
            }
            return stream.size();
        }
    }

    std::uint8_t NalUnit::type() const
    {
        return bytes[0] & 0b00011111;
    }

    std::uint8_t NalUnit::refIdc() const
    {
        return (bytes[0] & 0b01100000) >> 5;
    }

    bool NalUnit::startsFrame() const
    {
        const std::uint8_t t = type();
        if ((t != 1 && t != 5) || bytes.size() < 2)
        {
            return false;
        }
        // Заголовок слайсу починається з first_mb_in_slice у коді Exp-Golomb.
        // Значення 0 кодується одним бітом «1», тож старший біт = 1 ⇔ слайс починається з першого макроблока кадру
        return (bytes[1] & 0x80) != 0;
    }

    std::vector<NalUnit> splitAnnexB(std::span<const std::uint8_t> stream)
    {
        std::vector<NalUnit> result;

        std::size_t start = findStartCode(stream, 0);
        while (start < stream.size())
        {
            const std::size_t begin = start + 3;
            const std::size_t next = findStartCode(stream, begin);
            std::size_t end = next;

            while (end > begin && stream[end - 1] == 0x00)
            {
                --end;
            }

            if (begin < end)
            {
                result.push_back(NalUnit{begin, stream.subspan(begin, end - begin)});
            }
            start = next;
        }

        return result;
    }

    std::string_view nalTypeName(std::uint8_t type)
    {
        switch (type)
        {
        case 1:
            return "slice";
        case 5:
            return "IDR";
        case 6:
            return "SEI";
        case 7:
            return "SPS";
        case 8:
            return "PPS";
        case 9:
            return "AUD";
        default:
            return "other";
        }
    }

} // namespace pi_tracker