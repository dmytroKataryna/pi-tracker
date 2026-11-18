#pragma once

#include <cstdint>
#include <span>
#include <cstddef>
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
}; // namespace ubx

}; // namespace pi_tracker