#include "pi_tracker/msp.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace msp = pi_tracker::msp;

namespace {

// Справжні відповіді SpeedyBee F405 V4 (Betaflight 4.5.1), записані 2026-11-09
// через USB на Mac
const std::vector<std::uint8_t> kApiVersion{0x24, 0x4d, 0x3e, 0x03, 0x01,
                                            0x00, 0x01, 0x2e, 0x2d};
const std::vector<std::uint8_t> kFcVariant{0x24, 0x4d, 0x3e, 0x04, 0x02,
                                           0x42, 0x54, 0x46, 0x4c, 0x1a};
const std::vector<std::uint8_t> kFcVersion{0x24, 0x4d, 0x3e, 0x03, 0x03,
                                           0x04, 0x05, 0x01, 0x00};
const std::vector<std::uint8_t> kAttitude{0x24, 0x4d, 0x3e, 0x06, 0x6c, 0x02,
                                          0x00, 0x07, 0x00, 0x88, 0x00, 0xe7};
const std::vector<std::uint8_t> kRawImu{
    0x24, 0x4d, 0x3e, 0x12, 0x66, 0xe6, 0xff, 0x09, 0x00, 0x0d, 0x08, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60};
// Відповідь без даних (розмір 0) — так Betaflight відповідає на команду, якої
// не підтримує
const std::vector<std::uint8_t> kEmpty{0x24, 0x4d, 0x3e, 0x00, 0x25, 0x25};
// Кадр помилки: '!' замість '>' (складений вручну за специфікацією)
const std::vector<std::uint8_t> kError{0x24, 0x4d, 0x21, 0x00, 0x6c, 0x6c};

// Подає байти по одному й збирає всі кадри, що повернув парсер
std::vector<msp::Frame> feedAll(msp::Parser &parser,
                                const std::vector<std::uint8_t> &bytes) {
  std::vector<msp::Frame> frames;
  for (const std::uint8_t byte : bytes) {
    if (auto frame = parser.feed(byte)) {
      frames.push_back(std::move(*frame));
    }
  }
  return frames;
}

std::vector<std::uint8_t> concat(std::vector<std::uint8_t> a,
                                 const std::vector<std::uint8_t> &b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

} // namespace

// TEST(Група, Назва) ≈ @Test fun назва() у класі Група. Макрос сам створює клас
// і реєструє тест
TEST(MspEncode, AttitudeRequest) {
  const std::vector<std::uint8_t> expected{0x24, 0x4d, 0x3c, 0x00, 0x6c, 0x6c};
  EXPECT_EQ(msp::encodeRequest(msp::kAttitude),
            expected); // при помилці надрукує обидва вектори
}

TEST(MspParser, ApiVersion) {
  msp::Parser parser;
  const auto frames = feedAll(parser, kApiVersion);
  // ASSERT_ зупиняє тест одразу: без кадру рядок frames[0] нижче читав би за
  // межами вектора. EXPECT_ лише записує помилку і йде далі — так видно всі
  // розбіжності за один запуск
  ASSERT_EQ(frames.size(), 1U);
  EXPECT_EQ(frames[0].command, msp::kApiVersion);
  EXPECT_FALSE(frames[0].isError);
  EXPECT_EQ(frames[0].payload.size(), 3U);

  const auto api = msp::decodeApiVersion(frames[0]);
  ASSERT_TRUE(api.has_value());
  EXPECT_EQ(api->protocol, 0);
  EXPECT_EQ(api->major, 1);
  EXPECT_EQ(api->minor, 46);
}

TEST(MspParser, VariantAndVersion) {
  msp::Parser parser;
  const auto frames = feedAll(parser, concat(kFcVariant, kFcVersion));
  ASSERT_EQ(frames.size(), 2U);
  EXPECT_EQ(msp::decodeFcVariant(frames[0]),
            "BTFL"); // optional<string> порівнюється з рядком напряму

  const auto version = msp::decodeFcVersion(frames[1]);
  ASSERT_TRUE(version.has_value());
  EXPECT_EQ(version->major, 4);
  EXPECT_EQ(version->minor, 5);
  EXPECT_EQ(version->patch, 1);
}

TEST(MspDecode, Attitude) {
  msp::Parser parser;
  const auto frames = feedAll(parser, kAttitude);
  ASSERT_EQ(frames.size(), 1U);

  const auto attitude = msp::decodeAttitude(frames[0]);
  ASSERT_TRUE(attitude.has_value());
  // double — з допуском: 0.2 у двійковому вигляді неточне (як 0.1 + 0.2 != 0.3
  // у Kotlin)
  EXPECT_NEAR(attitude->rollDeg, 0.2, 1e-9);
  EXPECT_NEAR(attitude->pitchDeg, 0.7, 1e-9);
  EXPECT_NEAR(attitude->yawDeg, 136.0, 1e-9);
  // Після << — додатковий текст, який надрукується лише при помилці
  EXPECT_FALSE(msp::decodeRawImu(frames[0]).has_value())
      << "RAW_IMU decoder must reject an ATTITUDE frame";
}

TEST(MspDecode, RawImu) {
  msp::Parser parser;
  const auto frames = feedAll(parser, kRawImu);
  ASSERT_EQ(frames.size(), 1U);

  const auto imu = msp::decodeRawImu(frames[0]);
  ASSERT_TRUE(imu.has_value());
  // Плата лежить рівно: прискорення ≈ 1 g по осі z (2048 одиниць на g)
  EXPECT_EQ(imu->acc, (std::array<std::int16_t, 3>{-26, 9, 2061}));
  EXPECT_EQ(imu->gyro, (std::array<std::int16_t, 3>{0, 1, 0}));
}

TEST(MspParser, EmptyPayload) {
  msp::Parser parser;
  const auto frames = feedAll(parser, kEmpty);
  ASSERT_EQ(frames.size(), 1U);
  EXPECT_EQ(frames[0].command, 37);
  EXPECT_TRUE(frames[0].payload.empty());
}

TEST(MspParser, ErrorFrame) {
  msp::Parser parser;
  const auto frames = feedAll(parser, kError);
  ASSERT_EQ(frames.size(), 1U);
  EXPECT_TRUE(frames[0].isError);
  EXPECT_EQ(frames[0].command, msp::kAttitude);
  EXPECT_FALSE(msp::decodeAttitude(frames[0]).has_value());
}

TEST(MspParser, SkipsGarbageBeforeFrame) {
  msp::Parser parser;
  const auto frames = feedAll(
      parser, concat({0x00, 0xff, '$', 0x13, '$', 'M', 'x', 0x42}, kAttitude));
  ASSERT_EQ(frames.size(), 1);
  EXPECT_EQ(frames[0].command, msp::kAttitude);
}

TEST(MspParser, BadChecksumDropsFrameAndCounts) {
  msp::Parser parser;
  auto broken = kAttitude;
  broken[6] ^= 0x01;
  const auto frames = feedAll(parser, concat(broken, kApiVersion));
  ASSERT_EQ(frames.size(), 1);
  ASSERT_EQ(frames[0].command, msp::kApiVersion);
  EXPECT_EQ(parser.checksumErrors(), 1);
}

TEST(MspParser, FrameSplitAcrossReads) {
  msp::Parser parser;
  const std::vector<std::uint8_t> first(kRawImu.begin(), kRawImu.begin() + 10);
  const std::vector<std::uint8_t> second(kRawImu.begin() + 10, kRawImu.end());
  const auto a = feedAll(parser, first);
  const auto b = feedAll(parser, second);
  EXPECT_TRUE(a.empty());
  ASSERT_EQ(b.size(), 1);
  EXPECT_EQ(b[0].command, msp::kRawImu);
}

// Проблема з питання Дня 23: байт розміру 06 зіпсовано на 200 (0xC8). Парсер
// чекає 200 байтів даних і ковтає наступні відповіді разом із їхніми "$M>" — у
// стані Payload він початку кадру не шукає
TEST(MspParser, CorruptedSizeSwallowsNextFrame) {
  msp::Parser parser;
  auto corrupted = kAttitude;
  corrupted[3] = 200;
  EXPECT_TRUE(feedAll(parser, corrupted).empty());
  EXPECT_TRUE(feedAll(parser, kRawImu).empty())
      << "without reset() the next reply is lost";
}

TEST(MspParser, ResetDropsPartialFrame) {
  msp::Parser parser;
  auto broken = kAttitude;
  broken[6] ^= 0x01;
  const auto a0 = feedAll(parser, broken);
  ASSERT_EQ(parser.checksumErrors(), 1);

  auto corrupted = kAttitude;
  corrupted[3] = 200;
  const auto a = feedAll(parser, corrupted);

  parser.reset();

  const auto frames = feedAll(parser, kRawImu);
   ASSERT_EQ(frames.size(), 1);
   EXPECT_EQ(parser.checksumErrors(), 1);
}
