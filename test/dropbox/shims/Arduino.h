#pragma once
#include <cstdint>
inline uint32_t dropboxTestMillis = 0;
inline uint32_t millis() { return dropboxTestMillis; }
inline void delay(uint32_t) {}
