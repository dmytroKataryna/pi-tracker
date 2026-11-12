#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <utility>

namespace pi_tracker {

// Обмежена черга, яка нічого не викидає: коли вона повна, виробник ЧЕКАЄ.
// Java: ArrayBlockingQueue — push = put, tryPush = offer, pop = take.
// Нових потоків черга не створює: чекає (спить) той потік, який викликав
// push/pop.
template <typename T> class BlockingQueue {

public:
  explicit BlockingQueue(std::size_t capacity) : m_capacity{capacity} {
    if (capacity == 0) {
      throw std::invalid_argument("BlockingQueue: capacity must be > 0");
    }
  }

  bool push(T value) {
    std::unique_lock lock{m_mutex};

    m_notFull.wait(lock,
                   [this] { return m_queue.size() < m_capacity || m_closed; });

    if (m_closed) {
      return false;
    }

    m_queue.push(std::move(value));
    lock.unlock();

    m_notEmpty.notify_one();
    return true;
  }

  bool tryPush(T value) {
    std::unique_lock lock{m_mutex};
    if (m_closed || m_queue.size() >= m_capacity) {
      return false;
    }

    m_queue.push(std::move(value));

    lock.unlock();
    m_notEmpty.notify_one();
    return true;
  }

  std::optional<T> pop() {
    std::unique_lock lock{m_mutex};
    m_notEmpty.wait(lock, [this] { return !m_queue.empty() || m_closed; });

    if (m_closed && m_queue.empty()) {
      return std::nullopt;
    }

    T e = std::move(m_queue.front());
    m_queue.pop();

    lock.unlock();
    m_notFull.notify_one();

    return e;
  }

  void close() {
    {
      std::lock_guard lock{m_mutex};
      m_closed = true;
    }

    m_notEmpty.notify_all();
    m_notFull.notify_all();
  }

  std::size_t size() const {
    std::lock_guard lock{m_mutex};
    return m_queue.size();
  }

private:
  const std::size_t m_capacity;
  std::queue<T> m_queue;

  mutable std::mutex m_mutex;
  std::condition_variable m_notEmpty;
  std::condition_variable m_notFull;

  bool m_closed{false};
};
} // namespace pi_tracker
