#include "pi_tracker/ubx.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>
#include <limits>

namespace pi_tracker {

namespace ubx {

std::uint16_t readU16Le(std::span<const std::uint8_t> bytes,
                        std::size_t offset) {

  if (bytes.size() < 2 || offset > bytes.size() - 2) {
    throw std::out_of_range("not enought bytes");
  }

  std::uint8_t first = bytes[offset];
  std::uint8_t second = bytes[offset + 1];

  return static_cast<std::uint16_t>(first) |
         static_cast<std::uint16_t>(second << 8);
}

std::uint32_t readU32Le(std::span<const std::uint8_t> bytes,
                        std::size_t offset) {
  if (bytes.size() < 4 || offset > bytes.size() - 4) {
    throw std::out_of_range("not enought bytes");
  }

  std::uint8_t first = bytes[offset];
  std::uint8_t second = bytes[offset + 1];
  std::uint8_t third = bytes[offset + 2];
  std::uint8_t forth = bytes[offset + 3];

  return static_cast<std::uint32_t>(first) |
         (static_cast<std::uint32_t>(second) << 8) |
         (static_cast<std::uint32_t>(third) << 16) |
         (static_cast<std::uint32_t>(forth) << 24);
}
std::int32_t readI32Le(std::span<const std::uint8_t> bytes,
                       std::size_t offset) {

  return static_cast<std::int32_t>(readU32Le(bytes, offset));
}

Checksum fletcher8(std::span<const std::uint8_t> bytes) {
  std::uint8_t a = 0;
  std::uint8_t b = 0;

  for (std::size_t i = 0; i < bytes.size(); i++) {
    std::uint8_t byte = bytes[i];
    a += byte;
    b += a;
  }

  return Checksum(a, b);
}

std::vector<std::uint8_t> encodeFrame(std::uint8_t messageClass,
                                      std::uint8_t messageId,
                                      std::span<const std::uint8_t> payload) {
  if (payload.size() > std::numeric_limits<std::uint16_t>::max()) {
    throw std::length_error("payload too big");
  }

  std::vector<std::uint8_t> result{};
  // B5 62 | class id | lenLo lenHi | payload | CK_A CK_B
  result.reserve(6 + payload.size() + 2);

  result.push_back(kSync1);
  result.push_back(kSync2);
  result.push_back(messageClass);
  result.push_back(messageId);

  std::uint8_t lenLo = static_cast<std::uint8_t>(payload.size() & 0xFF);
  std::uint8_t lenHi = static_cast<std::uint8_t>((payload.size() >> 8) & 0xFF);
  result.push_back(lenLo);
  result.push_back(lenHi);

  result.insert(result.end(), payload.begin(), payload.end());

  std::span<const std::uint8_t> sub_span =  std::span{result}.subspan(2);
  Checksum checkSum = fletcher8(sub_span);

  result.push_back(checkSum.a);
  result.push_back(checkSum.b);

  return result;
}

} // namespace ubx

} // namespace pi_tracker