#include "pi_tracker/blocking_queue.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using pi_tracker::BlockingQueue;

namespace {

// Скільки чекаємо, щоб переконатися, що потік ДОСІ заблокований
constexpr auto kStillBlocked = 50ms;
// Скільки максимум чекаємо, доки заблокований потік прокинеться (із запасом на повільні CI і санітайзери)
constexpr auto kWakeUp = 2s;

bool isBlocked(const std::future<void> &done) {
  return done.wait_for(kStillBlocked) == std::future_status::timeout;
}

bool finished(const std::future<void> &done) {
  return done.wait_for(kWakeUp) == std::future_status::ready;
}

// Наприкінці тесту (навіть якщо перевірка впала) закриває чергу, щоб завислий потік прокинувся
// і тест не висів вічно. Оголошувати ПІСЛЯ std::future: знищиться першим (зворотний порядок)
struct CloseAtEnd {
  BlockingQueue<int> &queue;
  ~CloseAtEnd() { queue.close(); }
};

} // namespace

TEST(BlockingQueue, ZeroCapacityThrows) {
  EXPECT_THROW(BlockingQueue<int>{0}, std::invalid_argument);
}

TEST(BlockingQueue, FifoOrder) {
  BlockingQueue<int> queue{3};
  EXPECT_TRUE(queue.push(1));
  EXPECT_TRUE(queue.push(2));
  EXPECT_TRUE(queue.push(3));
  EXPECT_EQ(queue.size(), 3u);
  EXPECT_EQ(queue.pop(), 1);
  EXPECT_EQ(queue.pop(), 2);
  EXPECT_EQ(queue.pop(), 3);
  EXPECT_EQ(queue.size(), 0u);
}

TEST(BlockingQueue, TryPushFailsWhenFull) {
  BlockingQueue<int> queue{1};
  EXPECT_TRUE(queue.tryPush(1));
  EXPECT_FALSE(queue.tryPush(2)) << "tryPush не має чекати і не має класти в повну чергу";
  EXPECT_EQ(queue.size(), 1u);
  EXPECT_EQ(queue.pop(), 1);
}

TEST(BlockingQueue, PopWaitsForPush) {
  BlockingQueue<int> queue{1};
  std::optional<int> got;
  std::future<void> consumer = std::async(std::launch::async, [&] { got = queue.pop(); });
  CloseAtEnd guard{queue};

  EXPECT_TRUE(isBlocked(consumer)) << "pop() на порожній черзі має чекати";
  EXPECT_TRUE(queue.push(42));
  ASSERT_TRUE(finished(consumer)) << "push() не розбудив споживача";
  EXPECT_EQ(got, 42);
}

TEST(BlockingQueue, PushWaitsWhenFull) {
  BlockingQueue<int> queue{2};
  EXPECT_TRUE(queue.push(1));
  EXPECT_TRUE(queue.push(2));
  std::future<void> producer = std::async(std::launch::async, [&] { EXPECT_TRUE(queue.push(3)); });
  CloseAtEnd guard{queue};

  EXPECT_TRUE(isBlocked(producer)) << "push() у повну чергу має чекати";
  EXPECT_EQ(queue.size(), 2u) << "у повну чергу поклали зайвий елемент";
  EXPECT_EQ(queue.pop(), 1);
  ASSERT_TRUE(finished(producer)) << "pop() не розбудив виробника";
  EXPECT_EQ(queue.pop(), 2);
  EXPECT_EQ(queue.pop(), 3);
}

TEST(BlockingQueue, CloseWakesAllWaitingConsumers) {
  BlockingQueue<int> queue{1};
  std::future<void> first = std::async(std::launch::async, [&] { EXPECT_EQ(queue.pop(), std::nullopt); });
  std::future<void> second = std::async(std::launch::async, [&] { EXPECT_EQ(queue.pop(), std::nullopt); });
  EXPECT_TRUE(isBlocked(first));
  EXPECT_TRUE(isBlocked(second));

  queue.close();
  EXPECT_TRUE(finished(first)) << "close() має розбудити ВСІХ, хто чекає в pop()";
  EXPECT_TRUE(finished(second)) << "close() має розбудити ВСІХ, хто чекає в pop()";
}

TEST(BlockingQueue, CloseWakesWaitingProducer) {
  BlockingQueue<int> queue{1};
  EXPECT_TRUE(queue.push(1));
  std::future<void> producer = std::async(std::launch::async, [&] {
    EXPECT_FALSE(queue.push(2)) << "push() після close() має повернути false";
  });
  EXPECT_TRUE(isBlocked(producer));

  queue.close();
  EXPECT_TRUE(finished(producer)) << "close() має розбудити і тих, хто чекає в push()";
}

TEST(BlockingQueue, PopDrainsAfterClose) {
  BlockingQueue<int> queue{3};
  EXPECT_TRUE(queue.push(1));
  EXPECT_TRUE(queue.push(2));
  queue.close();
  EXPECT_FALSE(queue.push(3)) << "після close() нові елементи не приймаються";
  EXPECT_FALSE(queue.tryPush(3)) << "після close() нові елементи не приймаються";
  EXPECT_EQ(queue.pop(), 1) << "те, що вже в черзі, має дійти до споживача";
  EXPECT_EQ(queue.pop(), 2);
  EXPECT_EQ(queue.pop(), std::nullopt) << "закрита й порожня черга повертає nullopt";
}

// 4 виробники × 1000 чисел, 4 споживачі, місткість 8: кожне число має дійти рівно один раз
TEST(BlockingQueue, ManyProducersAndConsumers) {
  constexpr int kProducers = 4;
  constexpr int kConsumers = 4;
  constexpr int kPerProducer = 1000;
  BlockingQueue<int> queue{8};

  std::atomic<long long> sum{0};
  std::atomic<int> count{0};
  std::vector<std::thread> consumers;
  for (int c = 0; c < kConsumers; ++c) {
    consumers.emplace_back([&] {
      while (const auto value = queue.pop()) {
        sum += *value;
        ++count;
      }
    });
  }
  std::vector<std::thread> producers;
  for (int p = 0; p < kProducers; ++p) {
    producers.emplace_back([&, p] {
      for (int i = 1; i <= kPerProducer; ++i) {
        queue.push(p * kPerProducer + i);
      }
    });
  }
  for (auto &producer : producers) {
    producer.join();
  }
  queue.close(); // усе покладено: споживачі доберуть залишок і вийдуть із циклу
  for (auto &consumer : consumers) {
    consumer.join();
  }

  constexpr int kTotal = kProducers * kPerProducer;
  EXPECT_EQ(count.load(), kTotal);
  EXPECT_EQ(sum.load(), static_cast<long long>(kTotal) * (kTotal + 1) / 2);
}
