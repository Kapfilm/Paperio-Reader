#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

// Debug-friendly word-width cache with statistics tracking.
//
// Caching strategy:
// - Lazy: empty at start, populated on first encounter of each word
// - Font-aware: cache key includes fontId and style to prevent collisions
//   (same word "the" in body vs heading → different cache entries)
// - Size-aware: caller filters to only cache at scale=1.0f (standard size)
//   (sized words like 120% inline spans bypass cache)
// - Clearable: can be emptied before memory-pressure operations
// - Statistics: hit/miss tracking for performance analysis
class WordWidthCache {
 public:
  struct Stats {
    uint32_t lookups = 0;
    uint32_t hits = 0;
    uint32_t misses = 0;
    uint32_t cacheSize = 0;
    uint32_t maxCacheSize = 0;

    float hitRate() const { return lookups > 0 ? (100.0f * hits / lookups) : 0.0f; }

    bool isEmpty() const { return lookups == 0; }
  };

  WordWidthCache() = default;
  ~WordWidthCache() = default;

  // Non-copyable, non-movable
  WordWidthCache(const WordWidthCache&) = delete;
  WordWidthCache& operator=(const WordWidthCache&) = delete;

  // Look up word width in cache. On miss, returns -1 and caller must measure.
  // Caller is responsible for calling insert() with the measured width.
  // Cache accounts for font ID and style, so same word in different fonts won't collide.
  int16_t lookup(const std::string& word, int fontId, uint32_t style) {
    stats_.lookups++;

    if (cache_.empty()) {
      stats_.misses++;
      return -1;  // Not cached
    }

    uint64_t key = makeKey(word, fontId, style);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
      stats_.hits++;
      return it->second;
    }

    stats_.misses++;
    return -1;
  }

  // Insert measured word width into cache (if there's space).
  // Safe to call regardless of cache state; will grow up to MAX_ENTRIES.
  void insert(const std::string& word, int fontId, uint32_t style, int16_t width) {
    if (cache_.size() >= MAX_ENTRIES) {
      return;  // Cache full, don't evict; just stop growing
    }

    uint64_t key = makeKey(word, fontId, style);
    cache_[key] = width;
    stats_.cacheSize = cache_.size();
  }

  // Clear cache to free memory before heavy operations (layout, image decode, etc.)
  // Stats are preserved so caller can see pre-clear activity.
  void clear() {
    cache_.clear();
    stats_.cacheSize = 0;
  }

  // Get current statistics for logging.
  Stats getStats() const {
    Stats s = stats_;
    s.maxCacheSize = MAX_ENTRIES;
    return s;
  }

  // Reset statistics (called at start of new pagination).
  void resetStats() {
    stats_ = {};
    stats_.maxCacheSize = MAX_ENTRIES;
  }

  // Returns true if cache has ever been populated.
  bool isUsed() const { return stats_.lookups > 0; }

 private:
  // Create composite cache key from word, font ID, and font style.
  // FNV-1a hash for word, XOR'd with fontId and style to account for font-dependent metrics.
  static uint64_t makeKey(const std::string& word, int fontId, uint32_t style) {
    uint64_t hash = 14695981039346656037ull;  // FNV offset
    for (char c : word) {
      hash ^= static_cast<uint8_t>(c);
      hash *= 1099511628211ull;
    }
    // Mix in font ID and style to prevent collisions across different fonts
    hash ^= static_cast<uint64_t>(fontId) * 2654435761ull;  // Prime multiplier
    hash ^= static_cast<uint64_t>(style) * 2246822519ull;   // Different prime
    return hash;
  }

  static constexpr uint32_t MAX_ENTRIES = 512;  // ~8 KB overhead (16 bytes per entry hash table)

  std::unordered_map<uint64_t, int16_t> cache_;  // {composite key (word+fontId+style) → width}
  Stats stats_;
};
