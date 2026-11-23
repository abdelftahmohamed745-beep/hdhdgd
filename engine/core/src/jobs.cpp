#include "nimbus/jobs.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace nimbus::jobs {

struct QueuedJob {
  std::function<void()> fn;
  std::shared_ptr<JobState> state;
  std::shared_ptr<JobState> dependency;
};

struct JobSystem::Impl {
  mutable std::mutex mutex;
  std::condition_variable ready;
  std::condition_variable idle;
  std::deque<QueuedJob> queue;
  std::vector<std::thread> workers;
  uint32_t active = 0;
  bool stopping = false;
};

JobSystem::JobSystem(uint32_t workerCount) : impl_(std::make_unique<Impl>()) {
  if (workerCount == 0) {
    const unsigned hardware = std::thread::hardware_concurrency();
    // Keep capacity for the game, render and platform threads on mobile CPUs.
    workerCount = hardware > 2 ? hardware - 2 : 1;
  }
  workerCount = std::clamp(workerCount, 1u, 4u);
  impl_->workers.reserve(workerCount);
  try {
    for (uint32_t i = 0; i < workerCount; ++i) {
      impl_->workers.emplace_back([state = impl_.get()] {
        for (;;) {
          QueuedJob job;
          {
            std::unique_lock lock(state->mutex);
            auto nextReadyIndex = [state]() -> int {
              for (size_t idx = 0; idx < state->queue.size(); ++idx) {
                const auto& dep = state->queue[idx].dependency;
                if (!dep || dep->done.load(std::memory_order_acquire))
                  return static_cast<int>(idx);
              }
              return -1;
            };
            state->ready.wait(lock, [&] {
              if (state->stopping && state->queue.empty()) return true;
              return nextReadyIndex() >= 0;
            });
            const int readyIdx = nextReadyIndex();
            if (state->stopping && state->queue.empty()) return;
            if (readyIdx < 0) continue;
            job = std::move(state->queue[static_cast<size_t>(readyIdx)]);
            state->queue.erase(state->queue.begin() + readyIdx);
            ++state->active;
          }
          if (!job.state || !job.state->cancelled.load(std::memory_order_acquire)) {
            if (!job.dependency || !job.dependency->cancelled.load(std::memory_order_acquire)) {
              try {
                if (job.fn) job.fn();
              } catch (...) {
                /* submitted packaged_tasks capture exceptions */
              }
            } else if (job.state) {
              job.state->cancelled.store(true, std::memory_order_release);
            }
          }
          if (job.state) job.state->done.store(true, std::memory_order_release);
          {
            std::lock_guard lock(state->mutex);
            --state->active;
            if (state->queue.empty() && state->active == 0) state->idle.notify_all();
          }
          state->ready.notify_all();
        }
      });
    }
  } catch (...) {
    {
      std::lock_guard lock(impl_->mutex);
      impl_->stopping = true;
    }
    impl_->ready.notify_all();
    for (std::thread& worker : impl_->workers)
      if (worker.joinable()) worker.join();
    impl_->workers.clear();
    throw;
  }
}

JobSystem::~JobSystem() { shutdown(); }

uint32_t JobSystem::workerCount() const {
  if (!impl_) return 0;
  return static_cast<uint32_t>(impl_->workers.size());
}

uint32_t JobSystem::pendingCount() const {
  if (!impl_) return 0;
  std::lock_guard lock(impl_->mutex);
  return static_cast<uint32_t>(impl_->queue.size() + impl_->active);
}

bool JobSystem::enqueue(std::function<void()> job) {
  if (!job || !impl_) return false;
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->stopping) return false;
    impl_->queue.push_back({std::move(job), nullptr, nullptr});
  }
  impl_->ready.notify_one();
  return true;
}

JobHandle JobSystem::submitTask(std::function<void()> job) {
  auto state = std::make_shared<JobState>();
  if (!job || !impl_) {
    state->done.store(true, std::memory_order_release);
    return JobHandle(state);
  }
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->stopping) {
      state->done.store(true, std::memory_order_release);
      return JobHandle(state);
    }
    impl_->queue.push_back({std::move(job), state, nullptr});
  }
  impl_->ready.notify_one();
  return JobHandle(state);
}

JobHandle JobSystem::submitAfter(const JobHandle& dependency, std::function<void()> job) {
  auto state = std::make_shared<JobState>();
  if (!job || !impl_) {
    state->done.store(true, std::memory_order_release);
    return JobHandle(state);
  }
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->stopping) {
      state->done.store(true, std::memory_order_release);
      return JobHandle(state);
    }
    impl_->queue.push_back({std::move(job), state, dependency.state()});
  }
  impl_->ready.notify_all();
  return JobHandle(state);
}

void JobSystem::parallelFor(uint32_t count, uint32_t chunkSize,
                            const std::function<void(uint32_t, uint32_t)>& fn) {
  if (count == 0 || !fn) return;
  const uint32_t step = std::max(1u, chunkSize);
  if (!impl_ || workerCount() <= 1 || count <= step) {
    fn(0, count);
    return;
  }
  std::vector<JobHandle> handles;
  handles.reserve((count + step - 1) / step);
  for (uint32_t start = 0; start < count; start += step) {
    const uint32_t end = std::min(count, start + step);
    handles.push_back(submitTask([start, end, &fn]() { fn(start, end); }));
  }
  for (const JobHandle& handle : handles) waitFor(handle);
}

void JobSystem::waitFor(const JobHandle& handle) {
  if (!impl_ || !handle.valid()) return;
  std::unique_lock lock(impl_->mutex);
  impl_->idle.wait(lock, [&] {
    return handle.done() || (impl_->queue.empty() && impl_->active == 0);
  });
}

void JobSystem::waitIdle() {
  if (!impl_) return;
  std::unique_lock lock(impl_->mutex);
  impl_->idle.wait(lock, [this] { return impl_->queue.empty() && impl_->active == 0; });
}

void JobSystem::shutdown() {
  if (!impl_) return;
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->stopping) return;
    impl_->stopping = true;
    for (auto& q : impl_->queue) {
      if (q.state) {
        q.state->cancelled.store(true, std::memory_order_release);
        q.state->done.store(true, std::memory_order_release);
      }
    }
    impl_->queue.clear();
  }
  impl_->ready.notify_all();
  impl_->idle.notify_all();
  for (std::thread& worker : impl_->workers)
    if (worker.joinable()) worker.join();
  impl_->workers.clear();
}

}  // namespace nimbus::jobs
