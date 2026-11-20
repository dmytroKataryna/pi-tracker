#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>

struct Position {
  std::int32_t latE7;
  std::int32_t lonE7;
  std::int32_t altMm;
  std::uint32_t fixNumber;
  std::uint64_t timeUs;
};

class LatestPosition {
public:
  void set(const Position &position) {
    std::lock_guard lock{m_mutex};
    m_latE7 = position.latE7;
    m_lonE7 = position.lonE7;
    m_altMm = position.altMm;
    m_fixNumber = position.fixNumber;
    m_timeUs = position.timeUs;
  }

  Position get() const {
    std::lock_guard lock{m_mutex};
    return Position{m_latE7, m_lonE7, m_altMm, m_fixNumber, m_timeUs};
  }

private:
  mutable std::mutex m_mutex;

  std::int32_t m_latE7{};
  std::int32_t m_lonE7{};
  std::int32_t m_altMm{};
  std::uint32_t m_fixNumber{};
  std::uint64_t m_timeUs{};
};

constexpr std::int32_t kLatE7 = 504'500'000; // 50.45° пн. ш.
constexpr std::int32_t kLonE7 = 305'200'000; // 30.52° сх. д.
constexpr int kReads = 2'000'000;

int main() {
  LatestPosition latest;
  std::atomic<bool> stop{false};

  std::thread gps([&] {
    for (std::uint32_t fix = 1; !stop; ++fix) {
      const auto n = static_cast<std::int32_t>(fix);
      latest.set(
          Position{kLatE7 + n, kLonE7 + n, 120'000, fix, fix * 100'000ull});
    }
  });

  int mixed = 0;
  for (int i = 0; i < kReads; ++i) {
    const Position p = latest.get();
    if (p.fixNumber == 0) {
      continue;
    }
    const auto fix = static_cast<std::int32_t>(p.fixNumber);
    if (p.latE7 - kLatE7 != fix || p.lonE7 - kLonE7 != fix) {
      ++mixed;
      if (mixed <= 3) {
        std::printf(
            "змішана позиція: фікс %d, широта з фікса %d, довгота з фікса %d\n",
            fix, p.latE7 - kLatE7, p.lonE7 - kLonE7);
      }
    }
  }
  stop = true;
  gps.join();

  std::printf("прочитано %d позицій, змішаних: %d\n", kReads, mixed);
}
