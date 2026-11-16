#include "pi_tracker/length_framing.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

using pi_tracker::encodeFrame;
using pi_tracker::FrameDecoder;
using Bytes = std::vector<std::uint8_t>;

namespace {

Bytes concat(Bytes a, const Bytes &b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

// Шматок [from, from + count) — як один recv(), що повернув лише частину потоку
Bytes slice(const Bytes &bytes, std::size_t from, std::size_t count) {
  return Bytes(bytes.begin() + static_cast<std::ptrdiff_t>(from),
               bytes.begin() + static_cast<std::ptrdiff_t>(from + count));
}

} // namespace

// ---------- encodeFrame ----------

TEST(EncodeFrame, AddsFourByteBigEndianLength) {
  EXPECT_EQ(encodeFrame(Bytes{0xAA, 0xBB}), (Bytes{0x00, 0x00, 0x00, 0x02, 0xAA, 0xBB}));
}

TEST(EncodeFrame, LengthUsesAllFourBytes) {
  // 258 = 0x00000102; 70000 = 0x00011170 — старший байт першим (big-endian, «мережевий порядок»)
  const Bytes frame258 = encodeFrame(Bytes(258, 0x55));
  ASSERT_EQ(frame258.size(), 4u + 258u);
  EXPECT_EQ(slice(frame258, 0, 4), (Bytes{0x00, 0x00, 0x01, 0x02}));

  const Bytes frame70000 = encodeFrame(Bytes(70000, 0x55));
  ASSERT_EQ(frame70000.size(), 4u + 70000u);
  EXPECT_EQ(slice(frame70000, 0, 4), (Bytes{0x00, 0x01, 0x11, 0x70}));
}

TEST(EncodeFrame, EmptyPayloadIsJustHeader) {
  EXPECT_EQ(encodeFrame(Bytes{}), (Bytes{0x00, 0x00, 0x00, 0x00}));
}

// ---------- FrameDecoder ----------

TEST(FrameDecoder, WholeFrameInOneChunk) {
  FrameDecoder decoder{1024};
  const auto frames = decoder.feed(Bytes{0x00, 0x00, 0x00, 0x03, 'A', 'B', 'C'});
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0], (Bytes{'A', 'B', 'C'}));
  EXPECT_EQ(decoder.buffered(), 0u);
}

TEST(FrameDecoder, TwoFramesInOneChunk) {
  // send("AAA") + send("BBB") → один recv() повернув обидва кадри разом
  FrameDecoder decoder{1024};
  const auto frames = decoder.feed(concat(encodeFrame(Bytes{'A', 'A', 'A'}), encodeFrame(Bytes{'B', 'B', 'B'})));
  ASSERT_EQ(frames.size(), 2u) << "feed має повернути ВСІ кадри, що вже повністю прийшли";
  EXPECT_EQ(frames[0], (Bytes{'A', 'A', 'A'}));
  EXPECT_EQ(frames[1], (Bytes{'B', 'B', 'B'}));
}

TEST(FrameDecoder, ByteByByte) {
  // Найгірший випадок: кожен recv() повертає 1 байт
  FrameDecoder decoder{1024};
  const Bytes stream = encodeFrame(Bytes{1, 2, 3, 4, 5});
  ASSERT_EQ(stream.size(), 4u + 5u) << "спершу має працювати encodeFrame";
  for (std::size_t i = 0; i + 1 < stream.size(); ++i) {
    EXPECT_TRUE(decoder.feed(Bytes{stream[i]}).empty()) << "кадр ще не повний після байта " << i;
    EXPECT_EQ(decoder.buffered(), i + 1) << "неповний кадр треба зберегти до наступного feed";
  }
  const auto frames = decoder.feed(Bytes{stream.back()});
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0], (Bytes{1, 2, 3, 4, 5}));
  EXPECT_EQ(decoder.buffered(), 0u);
}

TEST(FrameDecoder, HeaderSplitBetweenChunks) {
  FrameDecoder decoder{1024};
  const Bytes stream = encodeFrame(Bytes(300, 0x77)); // довжина 0x0000012C — важливі обидва молодші байти
  ASSERT_EQ(stream.size(), 4u + 300u) << "спершу має працювати encodeFrame";
  EXPECT_TRUE(decoder.feed(slice(stream, 0, 2)).empty());
  const auto frames = decoder.feed(slice(stream, 2, stream.size() - 2));
  ASSERT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0], Bytes(300, 0x77));
}

TEST(FrameDecoder, FrameAndAHalf) {
  FrameDecoder decoder{1024};
  const Bytes first = encodeFrame(Bytes{'o', 'n', 'e'});
  const Bytes second = encodeFrame(Bytes{'t', 'w', 'o', '!'});
  const Bytes stream = concat(first, second);
  ASSERT_EQ(stream.size(), (4u + 3u) + (4u + 4u)) << "спершу має працювати encodeFrame";
  const std::size_t cut = first.size() + 5; // заголовок другого кадру + 1 байт даних

  const auto a = decoder.feed(slice(stream, 0, cut));
  ASSERT_EQ(a.size(), 1u);
  EXPECT_EQ(a[0], (Bytes{'o', 'n', 'e'}));
  EXPECT_EQ(decoder.buffered(), 5u) << "початок другого кадру має лишитися в буфері";

  const auto b = decoder.feed(slice(stream, cut, stream.size() - cut));
  ASSERT_EQ(b.size(), 1u);
  EXPECT_EQ(b[0], (Bytes{'t', 'w', 'o', '!'}));
  EXPECT_EQ(decoder.buffered(), 0u);
}

TEST(FrameDecoder, EmptyFrame) {
  FrameDecoder decoder{1024};
  const auto frames = decoder.feed(concat(encodeFrame(Bytes{9}), encodeFrame(Bytes{})));
  ASSERT_EQ(frames.size(), 2u) << "кадр довжини 0 — теж кадр, навіть останній у шматку";
  EXPECT_EQ(frames[0], (Bytes{9}));
  EXPECT_TRUE(frames[1].empty());
  EXPECT_EQ(decoder.buffered(), 0u);
}

TEST(FrameDecoder, LengthEqualToMaxIsAccepted) {
  FrameDecoder decoder{4};
  const auto frames = decoder.feed(encodeFrame(Bytes{1, 2, 3, 4}));
  ASSERT_EQ(frames.size(), 1u) << "maxPayload — найбільша ДОЗВОЛЕНА довжина";
}

TEST(FrameDecoder, RejectsTooLongFrameRightAfterHeader) {
  // Сміття або атака: заголовок обіцяє 1 MiB. Помилка — одразу після 4 байтів заголовка,
  // не чекаючи (і не накопичуючи в пам'яті) самих даних
  FrameDecoder decoder{1024};
  EXPECT_THROW(decoder.feed(Bytes{0x00, 0x10, 0x00, 0x00}), std::length_error);
}

TEST(FrameDecoder, RandomChunksRoundTrip) {
  // 200 кадрів різної довжини, потік поділено на випадкові шматки 1..64 байти
  std::mt19937 random{42}; // фіксоване зерно — тест щоразу однаковий
  std::uniform_int_distribution<std::size_t> payloadSize{0, 300};
  std::uniform_int_distribution<int> byteValue{0, 255};
  std::uniform_int_distribution<std::size_t> chunkSize{1, 64};

  std::vector<Bytes> sent;
  Bytes stream;
  for (int i = 0; i < 200; ++i) {
    Bytes payload(payloadSize(random));
    for (auto &b : payload) {
      b = static_cast<std::uint8_t>(byteValue(random));
    }
    stream = concat(stream, encodeFrame(payload));
    sent.push_back(payload);
  }

  FrameDecoder decoder{300};
  std::vector<Bytes> received;
  for (std::size_t pos = 0; pos < stream.size();) {
    const std::size_t count = std::min(chunkSize(random), stream.size() - pos);
    for (auto &frame : decoder.feed(slice(stream, pos, count))) {
      received.push_back(std::move(frame));
    }
    pos += count;
  }
  EXPECT_EQ(received.size(), sent.size());
  EXPECT_TRUE(received == sent) << "кадри мають прийти ті самі й у тому самому порядку";
  EXPECT_EQ(decoder.buffered(), 0u);
}
