#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace pi_tracker {

inline constexpr std::size_t kFrameHeaderSize = 4;

std::vector<std::uint8_t> encodeFrame(std::span<const std::uint8_t> payload);

class FrameDecoder {
public:
  explicit FrameDecoder(std::size_t maxPayload) : m_maxPayload(maxPayload) {}


  std::vector<std::vector<std::uint8_t>>
  feed(std::span<const std::uint8_t> chunk);

  std::size_t buffered() const;

private:
  std::size_t m_maxPayload{};
  std::vector<std::uint8_t> m_buffer{};
};

} // namespace pi_tracker