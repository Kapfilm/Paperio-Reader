#pragma once
namespace HalClock {
inline bool isApproximate() { return false; }
inline bool syncNtp() { return true; }
}  // namespace HalClock
