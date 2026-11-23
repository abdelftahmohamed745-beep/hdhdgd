// Nimbus event dispatcher: synchronous publish & deferred thread-safe queue.
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nimbus::events {

using SubscriptionId = uint64_t;

class Dispatcher {
 public:
  template <typename Event>
  SubscriptionId subscribe(std::function<void(const Event&)> handler) {
    if (!handler) return 0;
    std::lock_guard lock(mutex_);
    const SubscriptionId id = nextId_++;
    auto& list = subscribers_[std::type_index(typeid(Event))];
    list.push_back({id, [fn = std::move(handler)](const void* payload) {
                      fn(*static_cast<const Event*>(payload));
                    }});
    return id;
  }

  template <typename Event>
  void unsubscribe(SubscriptionId id) {
    if (id == 0) return;
    std::lock_guard lock(mutex_);
    auto it = subscribers_.find(std::type_index(typeid(Event)));
    if (it == subscribers_.end()) return;
    auto& list = it->second;
    list.erase(std::remove_if(list.begin(), list.end(),
                              [id](const Entry& entry) { return entry.id == id; }),
               list.end());
  }

  void unsubscribeAll(SubscriptionId id) {
    if (id == 0) return;
    std::lock_guard lock(mutex_);
    for (auto& [type, list] : subscribers_) {
      (void)type;
      list.erase(std::remove_if(list.begin(), list.end(),
                                [id](const Entry& entry) { return entry.id == id; }),
                 list.end());
    }
  }

  template <typename Event>
  void publish(const Event& event) {
    std::vector<std::function<void(const void*)>> snapshot;
    {
      std::lock_guard lock(mutex_);
      const auto it = subscribers_.find(std::type_index(typeid(Event)));
      if (it == subscribers_.end()) return;
      snapshot.reserve(it->second.size());
      for (const Entry& entry : it->second) snapshot.push_back(entry.callback);
    }
    for (const auto& cb : snapshot) cb(&event);
  }

  template <typename Event>
  void queue(Event event) {
    auto holder = std::make_shared<Event>(std::move(event));
    std::lock_guard lock(mutex_);
    queued_.push_back([this, holder = std::move(holder)]() { publish(*holder); });
  }

  uint32_t dispatchQueued() {
    std::vector<std::function<void()>> batch;
    {
      std::lock_guard lock(mutex_);
      batch.swap(queued_);
    }
    for (auto& fn : batch) fn();
    return static_cast<uint32_t>(batch.size());
  }

  uint32_t pendingCount() const {
    std::lock_guard lock(mutex_);
    return static_cast<uint32_t>(queued_.size());
  }

  void clear() {
    std::lock_guard lock(mutex_);
    subscribers_.clear();
    queued_.clear();
  }

 private:
  struct Entry {
    SubscriptionId id = 0;
    std::function<void(const void*)> callback;
  };

  mutable std::mutex mutex_;
  std::unordered_map<std::type_index, std::vector<Entry>> subscribers_;
  std::vector<std::function<void()>> queued_;
  SubscriptionId nextId_ = 1;
};

}  // namespace nimbus::events
