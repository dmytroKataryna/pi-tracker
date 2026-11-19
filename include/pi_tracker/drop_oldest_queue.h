#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <utility>

namespace pi_tracker {

template <typename T> class DropOldestQueue {
public:
  explicit DropOldestQueue(std::size_t capacity) : m_capacity(capacity) {
    if (capacity == 0) {
      throw std::invalid_argument("capacity is 0");
    }
  }

  bool push(T value) {
    std::lock_guard lock{m_mutex};
    if (m_closed) {
      return false;
    }

    if (m_queue.size() >= m_capacity) {
      m_dropped += 1;
      m_queue.pop();
    }

    m_queue.push(std::move(value));

    m_c_v.notify_all();

    return true;
  }

  std::optional<T> pop() {
    std::unique_lock lock{m_mutex};
    if (m_closed && m_queue.empty()) {
      return std::nullopt;
    }

    m_c_v.wait(lock, [this] { return !m_queue.empty() || m_closed; });

    if (m_queue.empty()) {
      return std::nullopt;
    }

    T temp = std::move(m_queue.front());
    m_queue.pop();

    return temp;
  }

  void close() {
    {
      std::lock_guard lock{m_mutex};
      m_closed = true;
    }
    m_c_v.notify_all();
  }

  std::size_t size() const {
    std::lock_guard lock{m_mutex};
    return m_queue.size();
  }

  std::size_t dropped() const {
    std::lock_guard lock{m_mutex};
    return m_dropped;
  }

private:
  const std::size_t m_capacity;
  std::size_t m_dropped{};
  std::queue<T> m_queue;

  mutable std::mutex m_mutex;
  std::condition_variable m_c_v;

  bool m_closed{false};
};

} // namespace pi_tracker