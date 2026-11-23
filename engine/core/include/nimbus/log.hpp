// Thread-safe engine diagnostics with a bounded in-memory history.
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace nimbus::log {

enum class Level : uint8_t { Debug, Info, Warning, Error, Fatal };
using Sink = std::function<void(Level, const char*, void*)>;

class RingBuffer {
 public:
  explicit RingBuffer(uint32_t capacity = 512) : capacity_(capacity ? capacity : 1) {}
  void push(Level level, std::string message);
  std::vector<std::pair<Level, std::string>> read(uint32_t offset, uint32_t limit) const;
  uint32_t total() const;
  void clear();

 private:
  mutable std::mutex mutex_;
  uint32_t capacity_;
  uint64_t total_ = 0;
  std::vector<std::pair<Level, std::string>> entries_;
};

void setLevel(Level level);
Level getLevel();
void setSink(Sink sink, void* userData = nullptr);
RingBuffer* ring();
void write(Level level, const char* format, ...);

}  // namespace nimbus::log
