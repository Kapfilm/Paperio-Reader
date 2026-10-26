#pragma once

#include <cstdint>

namespace calibration_test {
inline uint32_t nowMs = 0;
}

inline uint32_t millis() { return calibration_test::nowMs; }
