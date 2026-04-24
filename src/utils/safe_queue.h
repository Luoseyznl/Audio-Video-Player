#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>

namespace utils {

/**
 * @brief 线程安全队列
 * 特点：支持移动语义，支持最大容量限制（背压机制），支持阻塞等待。
 */
template <typename T>
class SafeQueue {
 public:
  explicit SafeQueue(size_t max_size = 100) : max_size_(max_size) {}
  ~SafeQueue() { clear(); }

  SafeQueue(const SafeQueue&) = delete;
  SafeQueue& operator=(const SafeQueue&) = delete;

  void push(T&& value) {
    std::unique_lock<std::mutex> lock(mutex_);
    // 当队列满时，阻塞等待直到有空位（防止内存爆掉）
    cond_full_.wait(lock,
                    [this] { return queue_.size() < max_size_ || !running_; });

    if (!running_) return;

    queue_.push(std::move(value));
    cond_empty_.notify_one();
  }

  bool pop(T& value) {
    std::unique_lock<std::mutex> lock(mutex_);
    // 当队列为空时，阻塞等待直到有新货
    cond_empty_.wait(lock, [this] { return !queue_.empty() || !running_; });

    if (!running_ && queue_.empty()) return false;

    value = std::move(queue_.front());
    queue_.pop();
    cond_full_.notify_one();
    return true;
  }

  bool try_pop(T& value) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (queue_.empty()) return false;

    value = std::move(queue_.front());
    queue_.pop();
    cond_full_.notify_one();
    return true;
  }

  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!queue_.empty()) {
      queue_.pop();
    }
    cond_full_.notify_all();
  }

  void stop() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      running_ = false;
    }
    cond_empty_.notify_all();
    cond_full_.notify_all();
  }

  size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

  bool empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
  }

 private:
  mutable std::mutex mutex_;
  std::queue<T> queue_;
  std::condition_variable cond_empty_;
  std::condition_variable cond_full_;
  size_t max_size_;
  bool running_ = true;
};

}  // namespace utils