#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>

namespace utils {

// 生产-消费：pull 阻塞在 cond_empty_ 上，push 阻塞在 cond_full_ 上
template <typename T>
class SafeQueue {
 public:
  explicit SafeQueue(size_t max_size = 100) : max_size_(max_size) {}
  ~SafeQueue() { clear(); }

  SafeQueue(const SafeQueue&) = delete;
  SafeQueue& operator=(const SafeQueue&) = delete;

  void push(T&& value) {
    std::unique_lock<std::mutex> lock(mutex_);
    // 背压防御 Backpressure：磁盘读取与解压速度不一致，数据包增长速度很快
    cond_full_.wait(lock,
                    [this] { return queue_.size() < max_size_ || !running_; });
    if (!running_) return;
    queue_.push(std::move(value));  // 右值引用：强制外部通过 std::move 传参
    cond_empty_.notify_one();       //
  }

  // 获取 & 删除竞态：在安全地返回弹出值之前不能删除（也能返回智能指针来解决）
  // 1. 阻塞弹出
  bool pop(T& value) {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_empty_.wait(lock, [this] { return !queue_.empty() || !running_; });
    if (!running_ && queue_.empty()) return false;  // 优雅停机
    value = std::move(queue_.front());
    queue_.pop();
    cond_full_.notify_one();
    return true;
  }

  // 2. 非阻塞弹出
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
    cond_empty_.notify_all();  // 唤醒阻塞的读线程
    cond_full_.notify_all();   // 唤醒阻塞的写线程
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