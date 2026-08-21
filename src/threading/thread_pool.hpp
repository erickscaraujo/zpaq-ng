// thread_pool.hpp - Worker pool and ordered output for ZPAQ-NG.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.
//
// Independent compression blocks are processed by a fixed worker pool and
// their results are written in submission order by OrderedWriter, so output is
// deterministic even when threads > 1.

#ifndef ZPAQ_NG_THREADING_THREAD_POOL_HPP
#define ZPAQ_NG_THREADING_THREAD_POOL_HPP

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <vector>

namespace zpaq_ng::threading {

// A fixed-size pool of workers consuming a queue of tasks. Tasks may be
// submitted from any thread; submitted functions must be thread-safe.
class ThreadPool {
public:
  explicit ThreadPool(unsigned threads = 0);
  ~ThreadPool();
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  // Enqueue a task. Results are gathered by the caller via futures or shared
  // state; this pool does not return values.
  void submit(std::function<void()> task);

  // Wait for all submitted tasks to finish.
  void wait();

  // Number of worker threads.
  unsigned size() const noexcept { return workers_.size(); }

private:
  void worker_loop();

  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> tasks_;
  std::mutex mtx_;
  std::condition_variable cv_;
  std::condition_variable done_cv_;
  unsigned pending_ = 0;
  bool stop_ = false;
};

// Collects (index, payload) pairs from parallel producers and hands them out in
// strict index order, so parallel block compression can be written
// deterministically.
template <typename T>
class OrderedCollector {
public:
  void push(std::size_t index, T&& value) {
    std::lock_guard<std::mutex> lock(mtx_);
    pending_.push_back({index, std::move(value)});
  }

  // Wait until item `index` is available and return it (moving out).
  // The caller must consume items in strictly increasing index order.
  T take(std::size_t index) {
    std::unique_lock<std::mutex> lock(mtx_);
    while (true) {
      for (std::size_t i = 0; i < pending_.size(); ++i) {
        if (pending_[i].first == index) {
          T result = std::move(pending_[i].second);
          pending_.erase(pending_.begin() + static_cast<long>(i));
          return result;
        }
      }
      cv_.wait(lock);
    }
  }

private:
  struct Item {
    std::size_t first;
    T second;
  };
  std::vector<Item> pending_;
  std::mutex mtx_;
  std::condition_variable cv_;
};

} // namespace zpaq_ng::threading

#endif // ZPAQ_NG_THREADING_THREAD_POOL_HPP