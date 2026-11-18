#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace pi_tracker {

namespace ubx {

inline constexpr std::uint8_t kSync1 = 0xB5;
inline constexpr std::uint8_t kSync2 = 0x62;

std::uint16_t readU16Le(std::span<const std::uint8_t> bytes,
                        std::size_t offset);
std::uint32_t readU32Le(std::span<const std::uint8_t> bytes,
                        std::size_t offset);
std::int32_t readI32Le(std::span<const std::uint8_t> bytes, std::size_t offset);

struct Checksum {
  std::uint8_t a{};
  std::uint8_t b{};
  bool operator==(const Checksum &) const = default;
};
Checksum fletcher8(std::span<const std::uint8_t> bytes);

std::vector<std::uint8_t> encodeFrame(std::uint8_t messageClass,
                                      std::uint8_t messageId,
                                      std::span<const std::uint8_t> payload);

struct Frame {
  std::uint8_t messageClass{};
  std::uint8_t messageId{};
  std::vector<std::uint8_t> payload;
  bool operator==(const Frame &) const = default;
};

class Parser {
public:
  explicit Parser(std::size_t maxPayload);
  std::optional<Frame>
  feed(std::uint8_t byte); // кадр — лише коли зібраний і сума збіглася
  std::size_t checksumErrors() const { return m_checksumErrors; }
  std::size_t lengthErrors() const { return m_lengthErrors; }
  void reset(); // забути недозібраний кадр, лічильники лишити
private:
  enum class State {
    Sync1,      // чекаємо kSync1
    Sync2,      // чекаємо kSync2
    Class,      // чекаємо class
    Id,         // чекаємо id
    LenLo,      // розмір lo
    LenHi,      // розмір hi
    Payload,    // дані: рівно m_size байтів
    Checksum_A, // контрольна сума
    Checksum_B, // контрольна сума
  };
  State m_state{State::Sync1};
  Frame m_frame;          // кадр, що збирається зараз
  std::uint16_t m_size{}; // скільки байтів даних оголошено в кадрі
  Checksum m_checksum{};
  std::size_t m_checksumErrors{};
  std::size_t m_maxPayload{};
  std::size_t m_lengthErrors{};
  std::uint8_t m_checksumA{};
  std::uint8_t m_sizeLo{};
};
}; // namespace ubx

}; // namespace pi_tracker