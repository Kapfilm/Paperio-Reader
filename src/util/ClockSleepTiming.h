#pragma once
#include <cstdint>

namespace clock_sleep {
constexpr uint64_t waitToMinuteUs(int64_t seconds, int32_t micros, uint64_t maxWait = 2000000ULL) {
  const int64_t secondInMinute = ((seconds % 60) + 60) % 60;
  const uint64_t wait = static_cast<uint64_t>(60 - secondInMinute) * 1000000ULL - micros;
  return wait < maxWait ? wait : maxWait;
}
}  // namespace clock_sleep
