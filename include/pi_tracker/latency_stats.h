#pragma once

#include <chrono>
#include <cstddef>
#include <vector>

namespace pi_tracker {


class LatencyStats {
public:
  explicit LatencyStats(std::size_t window);

  void add(std::chrono::microseconds sample);

  std::size_t count() const { return m_samples.size(); }

  std::chrono::microseconds percentile(double p) const;

  std::chrono::microseconds max() const;

private:
  std::size_t m_window;
  std::vector<std::chrono::microseconds> m_samples;
  std::size_t m_next{};
};

} // namespace pi_tracker
