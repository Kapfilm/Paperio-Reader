#pragma once

#include <climits>
#include <cstdint>

namespace BookProgressPages {
// FB2 packages share the EPUB progress format. Page pairs are an optional tail;
// legacy records remain valid for resuming but do not provide this display data.
enum class Format { Epub, Xtc, Text };

inline bool decode(Format format, const uint8_t* data, int size, int& currentPage, int& totalPages) {
  currentPage = 0;
  totalPages = 0;
  if (!data || size < (format == Format::Epub ? 11 : 9)) return false;
  const auto read16 = [data](int offset) {
    return static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8);
  };
  const auto read32 = [data](int offset) {
    return static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) | (static_cast<uint32_t>(data[offset + 3]) << 24);
  };
  uint32_t page = 0;
  uint32_t total = 0;
  if (format == Format::Epub) {
    page = read16(7);
    total = read16(9);
  } else if (format == Format::Xtc) {
    page = read32(0);
    total = read32(5);
    if (page >= static_cast<uint32_t>(INT_MAX)) return false;
    ++page;
  } else {
    page = read16(0) + 1;
    total = read16(7);
  }
  if (total == 0 || total > static_cast<uint32_t>(INT_MAX) || page == 0 || page > total) return false;
  currentPage = static_cast<int>(page);
  totalPages = static_cast<int>(total);
  return true;
}
}  // namespace BookProgressPages
