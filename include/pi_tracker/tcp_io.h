#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace pi_tracker {
void sendAll(int fd, std::span<const std::uint8_t> bytes);

std::size_t receiveSome(int fd, std::span<std::uint8_t> buffer);
} // namespace pi_tracker