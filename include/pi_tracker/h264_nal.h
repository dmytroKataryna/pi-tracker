#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace pi_tracker
{

    // Один NAL-блок потоку H.264. НЕ володіє пам'яттю: bytes — view у буфер усього потоку,
    // тож буфер має жити довше за вектор NalUnit
    struct NalUnit
    {
        std::size_t offset{};                // де в потоці починається NAL (одразу після стартового коду)
        std::span<const std::uint8_t> bytes; // байт заголовка + дані, без стартового коду; ніколи не порожній

        // Перший байт: forbidden_zero_bit (1 біт) | nal_ref_idc (2 біти) | nal_unit_type (5 біт)
        std::uint8_t type() const;   // 0…31: 1 — слайс, 5 — IDR, 6 — SEI, 7 — SPS, 8 — PPS
        std::uint8_t refIdc() const; // 0…3: 0 — на цей NAL ніщо не посилається, його можна викинути

        // Чи це перший слайс нового кадру (кадр може складатися з кількох слайсів)
        bool startsFrame() const;
    };

    // Розбиває потік Annex B на NAL-блоки. Стартовий код — 00 00 01 або 00 00 00 01.
    // Байти до першого стартового коду пропускає, нулі в кінці NAL відкидає, порожніх NAL не повертає
    std::vector<NalUnit> splitAnnexB(std::span<const std::uint8_t> stream);

    // "SPS", "IDR", ... для друку
    std::string_view nalTypeName(std::uint8_t type);

} // namespace pi_tracker