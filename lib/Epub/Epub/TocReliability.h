#pragma once

#include <cstdint>

// Tracks whether a book's Table of Contents (TOC) is reliable for navigation.
// Unknown state avoids O(tocCount) scans on first load; once known, the value is persisted
// in the metadata cache and reused across sessions.
enum class TocReliability : int8_t {
  Unknown = -1,    // Not yet determined
  Unreliable = 0,  // TOC exists but references don't reliably map to spine items
  Reliable = 1,    // TOC is present and properly linked to spine
};
