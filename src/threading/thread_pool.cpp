// thread_pool.cpp - Worker pool implementation.
//
// ZPAQ-NG - Copyright (c) 2026 Erick de S.C. Araujo. Unlicense.

#include "threading/thread_pool.hpp"

#include <cassert>

namespace zpaq_ng::threading {

ThreadPool::ThreadPool(unsigned threads) {
  const unsigned hw = std::thread::hardware_concurrency();
  const unsigned n = threads == 0 ? (hw > 0 ? hw : 1) : threads;
  workers_.reserve(n);
  for (unsigned i = 0; i < n; ++i) {
    workers_.emplace_back([this] { worker_loop(); });
  }
}

ThreadPool::~ThreadPool() {
  {
    std::lock_guard<std::mutex> lock(mtx_);
    stop_ = true;
  }
  cv_.notify_all();
  for (auto& t : workers_) t.join();
}

void ThreadPool::submit(std::function<void()> task) {
  {
    std::lock_guard<std::mutex> lock(mtx_);
    tasks_.push(std::move(task));
    ++pending_;
  }
  cv_.notify_one();
}

void ThreadPool::wait() {
  std::unique_lock<std::mutex> lock(mtx_);
  done_cv_.wait(lock, [this] { return pending_ == 0; });
}

void ThreadPool::worker_loop() {
  while (true) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock(mtx_);
      cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
      if (stop_ && tasks_.empty()) return;
      task = std::move(tasks_.front());
      tasks_.pop();
    }
    task();
    {
      std::lock_guard<std::mutex> lock(mtx_);
      --pending_;
    }
    done_cv_.notify_all();
  }
}

} // namespace zpaq_ng::threading