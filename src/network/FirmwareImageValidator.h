#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace firmware_flash::detail {

// Platform-independent core used by the flash-partition adapter. Reads are
// bounded by partitionSize, and SHA failures must never produce a valid image.
// Result is the firmware_flash::Result enum; injecting it keeps this header
// usable by host tests without pulling in ESP-IDF partition definitions.
template <typename Result, typename Read, typename Sha>
Result validatePartitionImage(size_t partitionSize, Read&& read, Sha& sha, uint8_t* scratch, size_t scratchSize,
                              size_t* imageSize) {
  if (imageSize) *imageSize = 0;
  constexpr size_t headerSize = 24, segmentHeaderSize = 8, digestSize = 32;
  if (partitionSize < headerSize) return Result::TOO_SMALL;
  if (!scratch || !scratchSize) return Result::OOM;
  uint8_t header[headerSize];
  if (!read(0, header, sizeof(header))) return Result::READ_FAIL;
  if (header[0] != 0xE9) return Result::BAD_MAGIC;
  // ESP32-C3 uses little-endian chip ID 5. Revision minima are deliberately
  // handled by the stock bootloader (Paperio supports patched X4 images).
  if (header[12] != 5 || header[13] != 0) return Result::BAD_CHIP;
  if (!header[1] || header[1] > 16) return Result::BAD_SEGMENTS;
  if (header[23] > 1) return Result::BAD_SIZE;
  if (!sha.update(header, sizeof(header))) return Result::BAD_SHA;
  uint8_t checksum = 0xEF;
  size_t position = headerSize;
  for (uint8_t segment = 0; segment < header[1]; ++segment) {
    if (segmentHeaderSize > partitionSize - position) return Result::BAD_SEGMENTS;
    uint8_t segmentHeader[segmentHeaderSize];
    if (!read(position, segmentHeader, sizeof(segmentHeader))) return Result::READ_FAIL;
    if (!sha.update(segmentHeader, sizeof(segmentHeader))) return Result::BAD_SHA;
    position += segmentHeaderSize;
    const uint32_t length = uint32_t(segmentHeader[4]) | (uint32_t(segmentHeader[5]) << 8) |
                            (uint32_t(segmentHeader[6]) << 16) | (uint32_t(segmentHeader[7]) << 24);
    if (length > partitionSize - position) return Result::BAD_SEGMENTS;
    size_t remaining = length;
    while (remaining) {
      const size_t count = std::min(remaining, scratchSize);
      if (!read(position, scratch, count)) return Result::READ_FAIL;
      if (!sha.update(scratch, count)) return Result::BAD_SHA;
      for (size_t i = 0; i < count; ++i) checksum ^= scratch[i];
      position += count;
      remaining -= count;
    }
  }
  // The checksum is the final byte in the next 16-byte block, even when the
  // segment data already ends on a block boundary. Subtraction avoids wrap.
  const size_t paddingSize = 16 - (position % 16);
  const size_t trailerSize = header[23] ? digestSize : 0;
  if (paddingSize > partitionSize - position || trailerSize > partitionSize - position - paddingSize)
    return Result::BAD_SIZE;
  uint8_t padding[16];
  if (!read(position, padding, paddingSize)) return Result::READ_FAIL;
  if (!sha.update(padding, paddingSize)) return Result::BAD_SHA;
  if (padding[paddingSize - 1] != checksum) return Result::BAD_CHECKSUM;
  position += paddingSize;
  if (trailerSize) {
    uint8_t expected[digestSize], actual[digestSize];
    if (!read(position, expected, sizeof(expected))) return Result::READ_FAIL;
    if (!sha.finish(actual) || std::memcmp(expected, actual, digestSize)) return Result::BAD_SHA;
    position += trailerSize;
  }
  if (imageSize) *imageSize = position;
  return Result::OK;
}

}  // namespace firmware_flash::detail
