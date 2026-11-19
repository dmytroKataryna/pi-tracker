#include "pi_tracker/latency_stats.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace pi_tracker {

LatencyStats::LatencyStats(std::size_t window) : m_window{window} {
  if (window == 0) {
    throw std::invalid_argument("LatencyStats: window must be > 0");
  }
  m_samples.reserve(window);
}

void LatencyStats::add(std::chrono::microseconds sample) {
  if (sample < std::chrono::microseconds::zero()) {
    throw std::invalid_argument("LatencyStats: negative latency");
  }
  if (m_samples.size() < m_window) {
    m_samples.push_back(sample);
  } else {
    m_samples[m_next] = sample;
  }
  m_next = (m_next + 1) % m_window;
}

std::chrono::microseconds LatencyStats::percentile(double p) const {
  if (!(p >= 0.0 && p <= 100.0)) {
    throw std::invalid_argument("LatencyStats: percentile must be in [0, 100]");
  }
  if (m_samples.empty()) {
    throw std::out_of_range("LatencyStats: no samples");
  }

  const std::size_t n = m_samples.size();
  std::size_t rank = static_cast<std::size_t>(std::ceil(p * static_cast<double>(n) / 100.0));
  if (rank == 0) {
    rank = 1;
  }

  std::vector<std::chrono::microseconds> copy = m_samples;
  const auto nth = copy.begin() + static_cast<std::ptrdiff_t>(rank - 1);
  std::nth_element(copy.begin(), nth, copy.end());
  return *nth;
}

std::chrono::microseconds LatencyStats::max() const {
  if (m_samples.empty()) {
    throw std::out_of_range("LatencyStats: no samples");
  }
  return *std::max_element(m_samples.begin(), m_samples.end());
}

} // namespace pi_tracker
