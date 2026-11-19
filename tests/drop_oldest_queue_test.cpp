#include "pi_tracker/drop_oldest_queue.h"

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using pi_tracker::DropOldestQueue;
using namespace std::chrono_literals;

// Черга для «живих» даних (кадри камери, телеметрія): виробник НІКОЛИ не чекає.
// Якщо черга повна, найстаріший елемент викидається — споживачу важливіша свіжість, ніж повнота

namespace {

// Сторож: якщо тест не завершився за 10 с (push чи pop заблокувався назавжди), процес завершується
// з повідомленням. Інакше зависла черга тримала б CI до загального тайм-ауту
class DropOldestQueueTest : public ::testing::Test {
protected:
  void SetUp() override {
    const std::string name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
    m_watchdog = std::thread([this, name] {
      std::unique_lock lock{m_mutex};
      if (!m_finished.wait_for(lock, 10s, [this] { return m_done; })) {
        std::fprintf(stderr, "\nТЕСТ ЗАВИС: %s — push або pop заблокувався назавжди\n", name.c_str());
        std::fflush(stderr);
        std::_Exit(1);
      }
    });
  }

  void TearDown() override {
    {
      std::lock_guard lock{m_mutex};
      m_done = true;
    }
    m_finished.notify_one();
    m_watchdog.join();
  }

private:
  std::thread m_watchdog;
  std::mutex m_mutex;
  std::condition_variable m_finished;
  bool m_done{};
};

} // namespace

TEST_F(DropOldestQueueTest, ZeroCapacityIsRejected) {
  EXPECT_THROW(DropOldestQueue<int>{0}, std::invalid_argument);
}

TEST_F(DropOldestQueueTest, FifoWhileNotFull) {
  DropOldestQueue<int> queue{3};
  EXPECT_TRUE(queue.push(1));
  EXPECT_TRUE(queue.push(2));
  EXPECT_TRUE(queue.push(3));
  EXPECT_EQ(queue.size(), 3u);
  EXPECT_EQ(queue.pop(), 1);
  EXPECT_EQ(queue.pop(), 2);
  EXPECT_EQ(queue.pop(), 3);
  EXPECT_EQ(queue.dropped(), 0u);
}

TEST_F(DropOldestQueueTest, DropsOldestWhenFull) {
  DropOldestQueue<int> queue{3};
  for (int frame = 1; frame <= 5; ++frame) {
    EXPECT_TRUE(queue.push(frame)) << "push на повну чергу не чекає і не відмовляє, а витісняє";
  }
  EXPECT_EQ(queue.size(), 3u) << "розмір не росте понад capacity";
  EXPECT_EQ(queue.dropped(), 2u) << "кадри 1 і 2 викинуто";
  EXPECT_EQ(queue.pop(), 3) << "викидається НАЙСТАРІШИЙ, а не новий";
  EXPECT_EQ(queue.pop(), 4);
  EXPECT_EQ(queue.pop(), 5);
}

TEST_F(DropOldestQueueTest, PushNeverBlocks) {
  // Споживача немає зовсім. BlockingQueue тут зупинила б виробника на другому push
  DropOldestQueue<int> queue{1};
  for (int i = 0; i < 1000; ++i) {
    queue.push(i);
  }
  EXPECT_EQ(queue.dropped(), 999u);
  EXPECT_EQ(queue.pop(), 999) << "лишився найсвіжіший";
}

TEST_F(DropOldestQueueTest, PopWaitsForData) {
  DropOldestQueue<int> queue{4};
  auto consumer = std::async(std::launch::async, [&] { return queue.pop(); });
  EXPECT_EQ(consumer.wait_for(50ms), std::future_status::timeout) << "у порожній черзі pop має чекати";
  queue.push(42);
  ASSERT_EQ(consumer.wait_for(2s), std::future_status::ready) << "push не розбудив споживача (забули notify?)";
  EXPECT_EQ(consumer.get(), 42);
}

TEST_F(DropOldestQueueTest, CloseWakesAllWaitingConsumers) {
  DropOldestQueue<int> queue{4};
  std::vector<std::future<std::optional<int>>> consumers;
  for (int i = 0; i < 3; ++i) {
    consumers.push_back(std::async(std::launch::async, [&] { return queue.pop(); }));
  }
  std::this_thread::sleep_for(50ms); // даємо всім трьом заснути в pop
  queue.close();
  for (auto &consumer : consumers) {
    ASSERT_EQ(consumer.wait_for(2s), std::future_status::ready) << "close() має розбудити ВСІХ, хто чекає в pop";
    EXPECT_EQ(consumer.get(), std::nullopt);
  }
}

TEST_F(DropOldestQueueTest, CloseLetsConsumerDrainTheRest) {
  DropOldestQueue<int> queue{4};
  queue.push(1);
  queue.push(2);
  queue.close();
  EXPECT_EQ(queue.pop(), 1) << "після close те, що вже в черзі, ще можна забрати";
  EXPECT_EQ(queue.pop(), 2);
  EXPECT_EQ(queue.pop(), std::nullopt) << "закрита і порожня — більше нічого не буде";
}

TEST_F(DropOldestQueueTest, PushAfterCloseIsRejected) {
  DropOldestQueue<int> queue{1};
  queue.push(1);
  queue.close();
  EXPECT_FALSE(queue.push(2));
  EXPECT_EQ(queue.dropped(), 0u) << "відхилений push нічого не викидає";
  EXPECT_EQ(queue.pop(), 1);
}

TEST_F(DropOldestQueueTest, WorksWithMoveOnlyType) {
  // Кадр камери зазвичай не можна копіювати (буфер, файловий дескриптор) — лише переміщувати
  DropOldestQueue<std::unique_ptr<int>> queue{2};
  queue.push(std::make_unique<int>(1));
  queue.push(std::make_unique<int>(2));
  queue.push(std::make_unique<int>(3)); // витісняє 1: unique_ptr сам звільнить пам'ять
  const std::optional<std::unique_ptr<int>> first = queue.pop();
  ASSERT_TRUE(first.has_value() && *first);
  EXPECT_EQ(**first, 2);
  EXPECT_EQ(queue.dropped(), 1u);
}

TEST_F(DropOldestQueueTest, FastProducerSlowConsumer) {
  // Виробник шле номери 0..99999 так швидко, як може; споживач у цей час забирає, скільки встигне.
  // Що б не викинулося, порядок зберігається, нічого не дублюється, а останній кадр завжди доходить
  constexpr int kCount = 100'000;
  DropOldestQueue<int> queue{8};
  auto producer = std::async(std::launch::async, [&] {
    for (int i = 0; i < kCount; ++i) {
      queue.push(i);
    }
    queue.close();
  });

  std::vector<int> received;
  while (const std::optional<int> value = queue.pop()) {
    received.push_back(*value);
  }
  producer.get();

  ASSERT_FALSE(received.empty());
  for (std::size_t i = 1; i < received.size(); ++i) {
    ASSERT_LT(received[i - 1], received[i]) << "порядок зламався на позиції " << i;
  }
  EXPECT_EQ(received.back(), kCount - 1) << "найсвіжіший кадр не можна втратити";
  EXPECT_EQ(received.size() + queue.dropped(), static_cast<std::size_t>(kCount))
      << "кожен кадр або отримано, або пораховано як викинутий";
}
