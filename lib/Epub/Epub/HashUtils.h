#pragma once

#include <cstdint>
#include <string>

// Shared hash utilities used across EPUB metadata and ZIP handling.
class HashUtils {
 public:
  // FNV-1a 64-bit hash function from string.
  // Used by BookMetadataCache for spine href indexing and ZipFile for batch size lookup.
  static uint64_t fnvHash64(const std::string& s) { return fnvHash64(s.c_str(), s.length()); }

  // FNV-1a 64-bit hash function from raw buffer.
  // Useful for computing hashes without allocating std::string.
  static uint64_t fnvHash64(const char* s, size_t len) {
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < len; i++) {
      hash ^= static_cast<uint8_t>(s[i]);
      hash *= 1099511628211ull;
    }
    return hash;
  }

  // FNV-1a 64-bit hash function from buffer.
  static uint64_t fnvHash64(const uint8_t* buf, size_t len) {
    return fnvHash64(reinterpret_cast<const char*>(buf), len);
  }
};
