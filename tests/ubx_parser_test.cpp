#include "pi_tracker/ubx.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <random>
#include <span>
#include <utility>
#include <vector>

using pi_tracker::ubx::encodeFrame;
using pi_tracker::ubx::Frame;
using pi_tracker::ubx::Parser;
using Bytes = std::vector<std::uint8_t>;

namespace {

// Справжня відповідь модуля: ACK-ACK (клас 0x05, id 0x01) — «команду CFG-PRT (06 00) прийнято»
const Bytes kAckFrame{0xB5, 0x62, 0x05, 0x01, 0x02, 0x00, 0x06, 0x00, 0x0E, 0x37};
const Frame kAck{0x05, 0x01, {0x06, 0x00}};

// Подає байти по одному, як вони приходили б з UART, і збирає всі готові кадри
std::vector<Frame> feedAll(Parser &parser, std::span<const std::uint8_t> bytes) {
  std::vector<Frame> frames;
  for (const std::uint8_t byte : bytes) {
    if (std::optional<Frame> frame = parser.feed(byte)) {
      frames.push_back(std::move(*frame));
    }
  }
  return frames;
}

Bytes concat(std::initializer_list<Bytes> parts) {
  Bytes all;
  for (const Bytes &part : parts) {
    all.insert(all.end(), part.begin(), part.end());
  }
  return all;
}

} // namespace

TEST(UbxParser, RealAckFrame) {
  Parser parser{1024};
  EXPECT_EQ(feedAll(parser, kAckFrame), std::vector<Frame>{kAck});
  EXPECT_EQ(parser.checksumErrors(), 0u);
  EXPECT_EQ(parser.lengthErrors(), 0u);
}

TEST(UbxParser, FrameWithoutPayload) {
  // Запит MON-VER: довжина 0, одразу після довжини йде контрольна сума
  Parser parser{1024};
  const Bytes monVer{0xB5, 0x62, 0x0A, 0x04, 0x00, 0x00, 0x0E, 0x34};
  EXPECT_EQ(feedAll(parser, monVer), (std::vector<Frame>{{0x0A, 0x04, {}}}));
}

TEST(UbxParser, FrameIsReturnedOnLastByteOnly) {
  Parser parser{1024};
  for (std::size_t i = 0; i + 1 < kAckFrame.size(); ++i) {
    EXPECT_FALSE(parser.feed(kAckFrame[i]).has_value()) << "кадр ще не повний, байт " << i;
  }
  const std::optional<Frame> frame = parser.feed(kAckFrame.back());
  ASSERT_TRUE(frame.has_value()) << "останній байт — CK_B: саме тут кадр готовий";
  EXPECT_EQ(*frame, kAck);
}

TEST(UbxParser, SkipsGarbageBeforeFrame) {
  // Підключилися посеред потоку: спершу хвіст чужого кадру. 0x62 без 0xB5 і 0xB5 без 0x62 — не початок
  Parser parser{1024};
  EXPECT_EQ(feedAll(parser, concat({{0x00, 0xFF, 0x62, 0x13, 0xB5, 0x00}, kAckFrame})), std::vector<Frame>{kAck});
}

TEST(UbxParser, RepeatedFirstSyncByte) {
  // B5 B5 62: перший 0xB5 виявився сміттям, але другий — справжній початок кадру
  Parser parser{1024};
  EXPECT_EQ(feedAll(parser, concat({{0xB5}, kAckFrame})), std::vector<Frame>{kAck})
      << "у стані «чекаю 0x62» байт 0xB5 не повертає на самий початок";
}

TEST(UbxParser, SyncBytesInsidePayloadAreJustData) {
  // Усередині кадру B5 62 — звичайні дані: довжина вже відома, тож шукати новий початок не треба
  Parser parser{1024};
  const Bytes payload{0xB5, 0x62, 0xB5, 0x62, 0x00};
  EXPECT_EQ(feedAll(parser, encodeFrame(0x01, 0x07, payload)), (std::vector<Frame>{{0x01, 0x07, payload}}));
}

TEST(UbxParser, ConsecutiveFramesDoNotMix) {
  Parser parser{1024};
  const std::vector<Frame> expected{{0x01, 0x07, {1, 2, 3, 4}}, {0x0A, 0x04, {}}, {0x05, 0x01, {9}}};
  Bytes stream;
  for (const Frame &frame : expected) {
    const Bytes encoded = encodeFrame(frame.messageClass, frame.messageId, frame.payload);
    stream.insert(stream.end(), encoded.begin(), encoded.end());
  }
  EXPECT_EQ(feedAll(parser, stream), expected) << "дані попереднього кадру не мають потрапити в наступний";
}

TEST(UbxParser, LengthIsLittleEndian) {
  // 262 = 0x0106. Якщо прочитати довжину як big-endian, вийде 0x0601 = 1537 > 300
  Parser parser{300};
  const Bytes payload(262, 0x11);
  EXPECT_EQ(feedAll(parser, encodeFrame(0x01, 0x07, payload)), (std::vector<Frame>{{0x01, 0x07, payload}}));
  EXPECT_EQ(parser.lengthErrors(), 0u);
}

TEST(UbxParser, BadChecksumDropsFrameAndCounts) {
  Parser parser{1024};
  Bytes wrongB = kAckFrame;
  wrongB.back() = 0x38;
  Bytes wrongA = kAckFrame;
  wrongA[wrongA.size() - 2] = 0x0F;
  Bytes swapped = kAckFrame;
  std::swap(swapped[swapped.size() - 2], swapped[swapped.size() - 1]);

  EXPECT_TRUE(feedAll(parser, wrongB).empty()) << "CK_B не збігся";
  EXPECT_TRUE(feedAll(parser, wrongA).empty()) << "CK_A не збігся";
  EXPECT_TRUE(feedAll(parser, swapped).empty()) << "CK_A і CK_B переставлені місцями";
  EXPECT_EQ(parser.checksumErrors(), 3u);
  EXPECT_EQ(feedAll(parser, kAckFrame), std::vector<Frame>{kAck}) << "після зіпсованого кадру наступний читається";
}

TEST(UbxParser, CorruptedPayloadByteIsDetected) {
  Parser parser{1024};
  Bytes corrupted = encodeFrame(0x01, 0x07, Bytes{10, 20, 30, 40});
  corrupted[8] ^= 0x04; // один біт у даних перевернувся (шум на лінії)
  EXPECT_TRUE(feedAll(parser, concat({corrupted, kAckFrame})) == std::vector<Frame>{kAck});
  EXPECT_EQ(parser.checksumErrors(), 1u);
}

TEST(UbxParser, LengthAboveMaxIsRejectedWithoutWaiting) {
  // Зіпсований байт довжини: FF FF = 65535. Без перевірки парсер чекав би 65535 байтів
  // і «з'їв» би як дані всі наступні справжні кадри
  Parser parser{100};
  const Bytes badHeader{0xB5, 0x62, 0x01, 0x07, 0xFF, 0xFF};
  EXPECT_EQ(feedAll(parser, concat({badHeader, kAckFrame})), std::vector<Frame>{kAck});
  EXPECT_EQ(parser.lengthErrors(), 1u);
  EXPECT_EQ(parser.checksumErrors(), 0u);
}

TEST(UbxParser, LengthEqualToMaxIsAccepted) {
  Parser parser{100};
  const Bytes atMax(100, 0x00);
  const Bytes overMax(101, 0x00);
  EXPECT_EQ(feedAll(parser, encodeFrame(0x01, 0x07, atMax)), (std::vector<Frame>{{0x01, 0x07, atMax}}))
      << "довжина рівно maxPayload — ще дозволена";
  EXPECT_TRUE(feedAll(parser, encodeFrame(0x01, 0x07, overMax)).empty());
  EXPECT_EQ(parser.lengthErrors(), 1u) << "101 > 100";
  EXPECT_EQ(feedAll(parser, kAckFrame), std::vector<Frame>{kAck});
}

TEST(UbxParser, ResetDropsPartialFrameButKeepsCounters) {
  Parser parser{1024};
  Bytes wrongB = kAckFrame;
  wrongB.back() = 0x38;
  feedAll(parser, wrongB);
  ASSERT_EQ(parser.checksumErrors(), 1u);

  // Половина кадру, а далі відповідь не прийшла вчасно (обрив, тайм-аут) — програма викликає reset()
  feedAll(parser, Bytes(kAckFrame.begin(), kAckFrame.begin() + 5));
  parser.reset();
  EXPECT_EQ(feedAll(parser, kAckFrame), std::vector<Frame>{kAck}) << "без reset() парсер вважав би B5 62 частиною старого кадру";
  EXPECT_EQ(parser.checksumErrors(), 1u) << "reset() забуває кадр, але не статистику";
}

TEST(UbxParser, RandomFramesWithNoiseBetween) {
  // 300 випадкових кадрів, між ними — шум (будь-які байти, крім 0xB5)
  std::mt19937 random{42};
  std::uniform_int_distribution<int> byteValue{0, 255};
  std::uniform_int_distribution<std::size_t> payloadSize{0, 200};
  std::uniform_int_distribution<std::size_t> noiseSize{0, 10};
  auto randomByte = [&] { return static_cast<std::uint8_t>(byteValue(random)); };

  std::vector<Frame> sent;
  Bytes stream;
  for (int i = 0; i < 300; ++i) {
    Frame frame{randomByte(), randomByte(), Bytes(payloadSize(random))};
    for (auto &b : frame.payload) {
      b = randomByte();
    }
    for (std::size_t n = noiseSize(random); n > 0; --n) {
      std::uint8_t noise = randomByte();
      stream.push_back(noise == 0xB5 ? 0x00 : noise);
    }
    const Bytes encoded = encodeFrame(frame.messageClass, frame.messageId, frame.payload);
    stream.insert(stream.end(), encoded.begin(), encoded.end());
    sent.push_back(std::move(frame));
  }

  Parser parser{200};
  const std::vector<Frame> received = feedAll(parser, stream);
  EXPECT_EQ(received.size(), sent.size());
  EXPECT_TRUE(received == sent);
  EXPECT_EQ(parser.checksumErrors(), 0u);
  EXPECT_EQ(parser.lengthErrors(), 0u);
}
