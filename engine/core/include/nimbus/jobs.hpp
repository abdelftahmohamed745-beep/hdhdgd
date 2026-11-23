// Bounded thread pool for asynchronous engine work with task handles & dependencies.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace nimbus::jobs {

struct JobState {
  std::atomic<bool> done{false};
  std::atomic<bool> cancelled{false};
};

class JobHandle {
 public:
  JobHandle() = default;
  explicit JobHandle(std::shared_ptr<JobState> state) : state_(std::move(state)) {}

  bool valid() const { return state_ != nullptr; }
  bool done() const { return !state_ || state_->done.load(std::memory_order_acquire); }
  void cancel() {
    if (state_) state_->cancelled.store(true, std::memory_order_release);
  }
  bool cancelled() const { return state_ && state_->cancelled.load(std::memory_order_acquire); }
  const std::shared_ptr<JobState>& state() const { return state_; }

 private:
  std::shared_ptr<JobState> state_;
};

class JobSystem {
 public:
  explicit JobSystem(uint32_t workerCount = 0);
  ~JobSystem();
  JobSystem(const JobSystem&) = delete;
  JobSystem& operator=(const JobSystem&) = delete;

  uint32_t workerCount() const;
  uint32_t pendingCount() const;
  void waitIdle();
  void waitFor(const JobHandle& handle);
  void shutdown();

  template <typename Fn, typename... Args>
  auto submit(Fn&& function, Args&&... args)
      -> std::future<std::invoke_result_t<Fn, Args...>> {
    using Result = std::invoke_result_t<Fn, Args...>;
    auto task = std::make_shared<std::packaged_task<Result()>>(
        std::bind(std::forward<Fn>(function), std::forward<Args>(args)...));
    std::future<Result> result = task->get_future();
    enqueue([task] { (*task)(); });
    return result;
  }

  JobHandle submitTask(std::function<void()> job);
  JobHandle submitAfter(const JobHandle& dependency, std::function<void()> job);
  void parallelFor(uint32_t count, uint32_t chunkSize, const std::function<void(uint32_t, uint32_t)>& fn);
  bool enqueue(std::function<void()> job);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace nimbus::jobs
