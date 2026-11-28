#pragma once
#include <cstdint>

// Bit 31 selects the shared English blob; the other bits are UTF-8 byte offsets.
// These tables live in flash, not in persisted settings or save files.
inline const char* resolveI18nOffset(const char* local, const char* english, uint32_t offset) {
  return (offset & 0x80000000u) ? english + (offset & 0x7fffffffu) : local + offset;
}
