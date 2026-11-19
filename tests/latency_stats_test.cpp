#include "pi_tracker/latency_stats.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

using pi_tracker::LatencyStats;
using namespace std::chrono_literals;
using std::chrono::microseconds;

// Перцентиль методом найближчого рангу (nearest-rank):
//   впорядкувати вибірку за зростанням; rank = ceil(p / 100 * N), але не менше 1; відповідь — елемент №rank (з 1).
//   p0 — мінімум, p50 — медіана, p100 — максимум.

TEST(LatencyStats, ZeroWindowIsRejected) {
  EXPECT_THROW(LatencyStats{0}, std::invalid_argument);
}

TEST(LatencyStats, EmptyHasNoPercentiles) {
  const LatencyStats stats{10};
  EXPECT_EQ(stats.count(), 0u);
  EXPECT_THROW(stats.percentile(50), std::out_of_range) << "з порожньої вибірки нічого не порахуєш";
  EXPECT_THROW(stats.max(), std::out_of_range);
}

TEST(LatencyStats, SingleSample) {
  LatencyStats stats{10};
  stats.add(7ms);
  EXPECT_EQ(stats.count(), 1u);
  EXPECT_EQ(stats.percentile(0), 7ms);
  EXPECT_EQ(stats.percentile(50), 7ms);
  EXPECT_EQ(stats.percentile(100), 7ms);
  EXPECT_EQ(stats.max(), 7ms);
}

TEST(LatencyStats, NearestRankOnFourSamples) {
  LatencyStats stats{10};
  for (const microseconds sample : {40ms, 10ms, 30ms, 20ms}) { // навмисно не по порядку
    stats.add(sample);
  }
  EXPECT_EQ(stats.percentile(0), 10ms) << "p0 — мінімум";
  EXPECT_EQ(stats.percentile(25), 10ms) << "ceil(0.25 * 4) = 1";
  EXPECT_EQ(stats.percentile(26), 20ms) << "ceil(0.26 * 4) = ceil(1.04) = 2: округлення ВГОРУ";
  EXPECT_EQ(stats.percentile(50), 20ms);
  EXPECT_EQ(stats.percentile(75), 30ms);
  EXPECT_EQ(stats.percentile(76), 40ms) << "ceil(3.04) = 4";
  EXPECT_EQ(stats.percentile(100), 40ms);
  EXPECT_EQ(stats.max(), 40ms);
}

TEST(LatencyStats, OneToHundredMilliseconds) {
  LatencyStats stats{100};
  for (int ms = 1; ms <= 100; ++ms) {
    stats.add(std::chrono::milliseconds{ms});
  }
  EXPECT_EQ(stats.percentile(1), 1ms);
  EXPECT_EQ(stats.percentile(50), 50ms);
  EXPECT_EQ(stats.percentile(90), 90ms);
  EXPECT_EQ(stats.percentile(99), 99ms);
  EXPECT_EQ(stats.percentile(100), 100ms);
}

TEST(LatencyStats, AverageHidesTheTail) {
  // 980 кадрів по 30 мс і 20 по 280 мс: середнє рівно 35 мс, «виглядає нормально».
  // Але кожен 50-й кадр запізнюється на чверть секунди — це й показує p99
  LatencyStats stats{1000};
  for (int i = 0; i < 1000; ++i) {
    stats.add(i % 50 == 49 ? 280ms : 30ms);
  }
  EXPECT_EQ(stats.percentile(50), 30ms);
  EXPECT_EQ(stats.percentile(98), 30ms) << "980-й елемент ще 30 мс";
  EXPECT_EQ(stats.percentile(99), 280ms) << "990-й — уже хвіст";
  EXPECT_EQ(stats.max(), 280ms);
}

TEST(LatencyStats, OrderOfSamplesDoesNotMatter) {
  std::vector<microseconds> samples;
  for (int i = 0; i < 500; ++i) {
    samples.push_back(microseconds{i * 37 % 1000});
  }
  std::vector<microseconds> shuffled = samples;
  std::shuffle(shuffled.begin(), shuffled.end(), std::mt19937{5});

  LatencyStats inOrder{500};
  LatencyStats random{500};
  for (std::size_t i = 0; i < samples.size(); ++i) {
    inOrder.add(samples[i]);
    random.add(shuffled[i]);
  }
  for (const double p : {0.0, 10.0, 50.0, 90.0, 99.0, 99.9, 100.0}) {
    EXPECT_EQ(inOrder.percentile(p), random.percentile(p)) << "p" << p;
  }
}

TEST(LatencyStats, PercentileDoesNotChangeTheData) {
  LatencyStats stats{10};
  for (const microseconds sample : {5ms, 1ms, 4ms, 2ms, 3ms}) {
    stats.add(sample);
  }
  EXPECT_EQ(stats.percentile(50), 3ms);
  EXPECT_EQ(stats.percentile(50), 3ms) << "повторний виклик — та сама відповідь";
  EXPECT_EQ(stats.count(), 5u);
  stats.add(100ms);
  stats.add(200ms);
  EXPECT_EQ(stats.max(), 200ms);
  EXPECT_EQ(stats.percentile(0), 1ms);
}

TEST(LatencyStats, BadPercentileIsRejected) {
  LatencyStats stats{10};
  stats.add(1ms);
  EXPECT_THROW(stats.percentile(-1), std::invalid_argument);
  EXPECT_THROW(stats.percentile(100.5), std::invalid_argument);
  // NaN не менший за 0 і не більший за 100: перевірка «p < 0 || p > 100» його пропустить
  EXPECT_THROW(stats.percentile(std::numeric_limits<double>::quiet_NaN()), std::invalid_argument)
      << "NaN — теж неправильний перцентиль";
}

TEST(LatencyStats, NegativeLatencyIsRejected) {
  // Від'ємна затримка = виміряли не тим годинником (system_clock стрибнув назад після NTP)
  LatencyStats stats{10};
  EXPECT_THROW(stats.add(-1us), std::invalid_argument);
  EXPECT_EQ(stats.count(), 0u) << "зіпсований вимір не потрапляє у вибірку";
  stats.add(0us);
  EXPECT_EQ(stats.count(), 1u) << "нуль — допустимо";
}

TEST(LatencyStats, OnlyLastWindowSamplesCount) {
  // Пам'ять обмежена: зберігаємо лише останні window вимірів, найстаріший витісняється
  LatencyStats stats{3};
  stats.add(100ms); // колись був пік
  stats.add(1ms);
  stats.add(2ms);
  stats.add(3ms); // витісняє 100 мс
  EXPECT_EQ(stats.count(), 3u);
  EXPECT_EQ(stats.max(), 3ms) << "100 мс вже вийшло з вікна";
  stats.add(50ms); // витісняє 1 мс
  EXPECT_EQ(stats.percentile(0), 2ms);
  EXPECT_EQ(stats.max(), 50ms);
}

TEST(LatencyStats, WindowKeepsWorkingAfterManyWraps) {
  LatencyStats stats{5};
  for (int us = 1; us <= 1000; ++us) {
    stats.add(microseconds{us});
  }
  EXPECT_EQ(stats.count(), 5u);
  EXPECT_EQ(stats.percentile(0), 996us);
  EXPECT_EQ(stats.percentile(50), 998us);
  EXPECT_EQ(stats.max(), 1000us);
}
