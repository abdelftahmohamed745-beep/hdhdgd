#include "nimbus/log.hpp"

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace nimbus::log {
namespace {
std::mutex g_mutex;
Level g_level = Level::Info;
Sink g_sink;
void* g_sinkData = nullptr;
RingBuffer g_ring;
}

void RingBuffer::push(Level level, std::string message) {
  std::lock_guard lock(mutex_);
  ++total_;
  if (entries_.size() == capacity_) entries_.erase(entries_.begin());
  entries_.emplace_back(level, std::move(message));
}

std::vector<std::pair<Level, std::string>> RingBuffer::read(uint32_t offset, uint32_t limit) const {
  std::lock_guard lock(mutex_);
  const uint64_t firstSequence = total_ - entries_.size();
  const uint64_t requested = std::max<uint64_t>(offset, firstSequence);
  const size_t begin = static_cast<size_t>(std::min<uint64_t>(requested - firstSequence, entries_.size()));
  const size_t count = std::min<size_t>(limit, entries_.size() - begin);
  return {entries_.begin() + static_cast<std::ptrdiff_t>(begin),
          entries_.begin() + static_cast<std::ptrdiff_t>(begin + count)};
}

uint32_t RingBuffer::total() const {
  std::lock_guard lock(mutex_);
  return static_cast<uint32_t>(std::min<uint64_t>(total_, UINT32_MAX));
}

void RingBuffer::clear() {
  std::lock_guard lock(mutex_);
  entries_.clear();
  total_ = 0;
}

void setLevel(Level level) {
  std::lock_guard lock(g_mutex);
  g_level = level;
}
Level getLevel() {
  std::lock_guard lock(g_mutex);
  return g_level;
}
void setSink(Sink sink, void* userData) {
  std::lock_guard lock(g_mutex);
  g_sink = std::move(sink);
  g_sinkData = userData;
}
RingBuffer* ring() { return &g_ring; }

void write(Level level, const char* format, ...) {
  Sink sink;
  void* userData = nullptr;
  {
    std::lock_guard lock(g_mutex);
    if (static_cast<uint8_t>(level) < static_cast<uint8_t>(g_level)) return;
    sink = g_sink;
    userData = g_sinkData;
  }
  char stackBuffer[1024];
  va_list args;
  va_start(args, format);
  const int length = std::vsnprintf(stackBuffer, sizeof(stackBuffer), format ? format : "", args);
  va_end(args);
  std::string message;
  if (length < 0) message = "log formatting failed";
  else if (static_cast<size_t>(length) < sizeof(stackBuffer)) message.assign(stackBuffer, static_cast<size_t>(length));
  else {
    std::vector<char> buffer(static_cast<size_t>(length) + 1);
    va_start(args, format);
    std::vsnprintf(buffer.data(), buffer.size(), format ? format : "", args);
    va_end(args);
    message.assign(buffer.data(), static_cast<size_t>(length));
  }
  g_ring.push(level, message);
  if (sink) sink(level, message.c_str(), userData);
  else std::fprintf(stderr, "[nimbus:%u] %s\n", static_cast<unsigned>(level), message.c_str());
}

}  // namespace nimbus::log
