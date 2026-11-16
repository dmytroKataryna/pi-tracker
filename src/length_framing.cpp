#include "pi_tracker/length_framing.h"
#include <algorithm>
#include <format>
#include <limits>
#include <stdexcept>
#include <vector>

namespace pi_tracker {

std::vector<std::uint8_t> encodeFrame(std::span<const std::uint8_t> payload) {
  if (payload.size() > std::numeric_limits<std::uint32_t>::max()) {
    throw std::length_error("payload.size is too large");
  }

  std::uint32_t size = static_cast<std::uint32_t>(payload.size());
  std::vector<std::uint8_t> result(kFrameHeaderSize + size);

  std::uint8_t first = static_cast<std::uint8_t>(size >> 24);
  std::uint8_t second = static_cast<std::uint8_t>(size >> 16);
  std::uint8_t third = static_cast<std::uint8_t>(size >> 8);
  std::uint8_t forth = static_cast<std::uint8_t>(size >> 0);

  result.at(0) = first;
  result.at(1) = second;
  result.at(2) = third;
  result.at(3) = forth;

  std::copy(payload.begin(), payload.end(), result.begin() + kFrameHeaderSize);

  return result;
}

std::vector<std::vector<std::uint8_t>>
FrameDecoder::feed(std::span<const std::uint8_t> chunk) {
  m_buffer.insert(m_buffer.end(), chunk.begin(), chunk.end());

  std::vector<std::vector<std::uint8_t>> result{};
  std::size_t offsetIndex = 0;
  while (m_buffer.size() - offsetIndex >= kFrameHeaderSize) {

    std::uint8_t first = m_buffer.at(offsetIndex);
    std::uint8_t second = m_buffer.at(offsetIndex + 1);
    std::uint8_t third = m_buffer.at(offsetIndex + 2);
    std::uint8_t forth = m_buffer.at(offsetIndex + 3);

    std::uint32_t size = static_cast<std::uint32_t>(first << 24) +
                         static_cast<std::uint32_t>(second << 16) +
                         static_cast<std::uint32_t>(third << 8) +
                         static_cast<std::uint32_t>(forth << 0);
    if (size > m_maxPayload) {
      throw std::length_error(
          std::format("size bigger m_maxPayload: {}", size));
    }

    if (m_buffer.size() - offsetIndex < size + kFrameHeaderSize) {
      break;
    }

    std::vector<std::uint8_t> msg{};
    msg.reserve(size);

    std::ptrdiff_t offset =
        static_cast<std::ptrdiff_t>(kFrameHeaderSize + offsetIndex);

    msg.insert(msg.begin(), m_buffer.begin() + offset,
               m_buffer.begin() + offset + size);

    result.push_back(std::move(msg));
    offsetIndex = kFrameHeaderSize + size + offsetIndex;
  }

  m_buffer.erase(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(offsetIndex));
  return result;
}

std::size_t FrameDecoder::buffered() const { return m_buffer.size(); }

} // namespace pi_tracker