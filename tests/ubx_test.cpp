#include "pi_tracker/ubx.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

using pi_tracker::ubx::Checksum;
using pi_tracker::ubx::encodeFrame;
using pi_tracker::ubx::fletcher8;
using pi_tracker::ubx::readI32Le;
using pi_tracker::ubx::readU16Le;
using pi_tracker::ubx::readU32Le;
using Bytes = std::vector<std::uint8_t>;

// ---------- читання little-endian ----------

TEST(UbxRead, U16LowByteFirst) {
  // 06 01 → 0x0106 = 262: молодший байт першим
  EXPECT_EQ(readU16Le(Bytes{0x06, 0x01}, 0), 0x0106);
}

TEST(UbxRead, U16AtOffset) {
  EXPECT_EQ(readU16Le(Bytes{0xAA, 0x34, 0x12}, 1), 0x1234) << "читати треба з offset, а не з початку";
}

TEST(UbxRead, U32AllFourBytes) {
  EXPECT_EQ(readU32Le(Bytes{0x78, 0x56, 0x34, 0x12}, 0), 0x12345678u);
  EXPECT_EQ(readU32Le(Bytes{0x00, 0x00, 0x00, 0x80}, 0), 0x80000000u) << "старший байт зсувається на 24";
  EXPECT_EQ(readU32Le(Bytes{0xEE, 0x78, 0x56, 0x34, 0x12}, 1), 0x12345678u);
}

TEST(UbxRead, I32Signed) {
  // Широта в UBX — int32 у одиницях 1e-7 градуса
  EXPECT_EQ(readI32Le(Bytes{0x08, 0x13, 0x12, 0x1E}, 0), 504501000) << "50.4501° — Київ";
  EXPECT_EQ(readI32Le(Bytes{0x00, 0x08, 0xD0, 0xEB}, 0), -338688000) << "-33.8688° — південна широта";
  EXPECT_EQ(readI32Le(Bytes{0xFF, 0xFF, 0xFF, 0xFF}, 0), -1);
  EXPECT_EQ(readI32Le(Bytes{0x00, 0x00, 0x00, 0x80}, 0), std::numeric_limits<std::int32_t>::min());
}

TEST(UbxRead, LastBytesOfBufferAreAllowed) {
  // Рівно до кінця буфера — ще можна
  EXPECT_EQ(readU16Le(Bytes{0x00, 0x01, 0x02}, 1), 0x0201);
  EXPECT_EQ(readU32Le(Bytes{0x00, 0x01, 0x02, 0x03, 0x04}, 1), 0x04030201u);
}

TEST(UbxRead, ThrowsWhenNotEnoughBytes) {
  // Дані з мережі чи UART не можна читати «на віру»: замість читання чужої пам'яті — виняток
  EXPECT_THROW(readU16Le(Bytes{}, 0), std::out_of_range);
  EXPECT_THROW(readU16Le(Bytes{0x01, 0x02}, 1), std::out_of_range) << "з offset 1 лишився один байт";
  EXPECT_THROW(readU32Le(Bytes{0x01, 0x02, 0x03}, 0), std::out_of_range);
  EXPECT_THROW(readU32Le(Bytes{0x01, 0x02, 0x03, 0x04}, 1), std::out_of_range);
  EXPECT_THROW(readI32Le(Bytes{0x01, 0x02, 0x03, 0x04}, 5), std::out_of_range) << "offset за межами буфера";
}

TEST(UbxRead, HugeOffsetDoesNotWrapAround) {
  // offset + 2 при offset = SIZE_MAX переповнюється і стає 1: перевірка «offset + 2 > size» тут мовчить
  constexpr std::size_t kHuge = std::numeric_limits<std::size_t>::max();
  const Bytes bytes{0x01, 0x02, 0x03, 0x04};
  EXPECT_THROW(readU16Le(bytes, kHuge), std::out_of_range);
  EXPECT_THROW(readU32Le(bytes, kHuge - 1), std::out_of_range);
}

// ---------- контрольна сума Флетчера ----------

TEST(UbxChecksum, EmptyIsZero) {
  EXPECT_EQ(fletcher8(Bytes{}), (Checksum{0x00, 0x00}));
}

TEST(UbxChecksum, OrderMatters) {
  // На відміну від XOR: A — сума байтів, B — сума всіх проміжних A, тобто залежить від позиції
  EXPECT_EQ(fletcher8(Bytes{0x01, 0x02}), (Checksum{0x03, 0x04}));
  EXPECT_EQ(fletcher8(Bytes{0x02, 0x01}), (Checksum{0x03, 0x05}));
}

TEST(UbxChecksum, WrapsAroundAt256) {
  // A і B — по одному байту: переповнення — це частина алгоритму (рахуємо за модулем 256)
  EXPECT_EQ(fletcher8(Bytes(1000, 0xFF)), (Checksum{0x18, 0xEC}));
}

TEST(UbxChecksum, RealHeader) {
  // Клас, id і довжина з запиту MON-VER (версія прошивки модуля)
  EXPECT_EQ(fletcher8(Bytes{0x0A, 0x04, 0x00, 0x00}), (Checksum{0x0E, 0x34}));
}

// ---------- кадр ----------

TEST(UbxEncode, PollWithoutPayload) {
  // Справжні запити з документації u-blox: порожні дані = «надішли мені це повідомлення»
  EXPECT_EQ(encodeFrame(0x0A, 0x04, Bytes{}), (Bytes{0xB5, 0x62, 0x0A, 0x04, 0x00, 0x00, 0x0E, 0x34})) << "MON-VER";
  EXPECT_EQ(encodeFrame(0x01, 0x07, Bytes{}), (Bytes{0xB5, 0x62, 0x01, 0x07, 0x00, 0x00, 0x08, 0x19})) << "NAV-PVT";
}

TEST(UbxEncode, PayloadIsCopiedAndChecksummed) {
  // CFG-PRT для порту 1: один байт даних
  EXPECT_EQ(encodeFrame(0x06, 0x00, Bytes{0x01}), (Bytes{0xB5, 0x62, 0x06, 0x00, 0x01, 0x00, 0x01, 0x08, 0x22}));
  // CFG-RATE: 6 байтів даних, B переповнюється кілька разів. Кадр лише будуємо, нікуди не надсилаємо
  EXPECT_EQ(encodeFrame(0x06, 0x08, Bytes{0xC8, 0x00, 0x01, 0x00, 0x01, 0x00}),
            (Bytes{0xB5, 0x62, 0x06, 0x08, 0x06, 0x00, 0xC8, 0x00, 0x01, 0x00, 0x01, 0x00, 0xDE, 0x6A}));
}

TEST(UbxEncode, LengthIsLittleEndian) {
  Bytes payload(262);
  for (std::size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<std::uint8_t>(i * 7);
  }
  const Bytes frame = encodeFrame(0x01, 0x07, payload);
  ASSERT_EQ(frame.size(), 6u + 262u + 2u);
  EXPECT_EQ(frame[4], 0x06) << "262 = 0x0106: молодший байт довжини першим";
  EXPECT_EQ(frame[5], 0x01);
  EXPECT_EQ(Bytes(frame.begin() + 6, frame.end() - 2), payload);
  EXPECT_EQ(frame[frame.size() - 2], 0xF8);
  EXPECT_EQ(frame[frame.size() - 1], 0xF5) << "контрольна сума — від класу до кінця даних, без 0xB5 0x62";
}

TEST(UbxEncode, MaxLength) {
  // Довжина — 2 байти: 65535 ще влазить, 65536 — вже ні
  EXPECT_EQ(encodeFrame(0x01, 0x07, Bytes(65535)).size(), 6u + 65535u + 2u);
  EXPECT_THROW(encodeFrame(0x01, 0x07, Bytes(65536)), std::length_error);
}
