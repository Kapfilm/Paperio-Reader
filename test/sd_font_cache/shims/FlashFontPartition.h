#pragma once
#include <cstddef>
#include <cstdint>
namespace FlashFontPartition {
constexpr uint8_t MAX_ENTRIES = 16;
constexpr size_t HEADER_BYTES = 776;
size_t capacityBytes();
bool hasEntry(const char*, uint8_t);
bool isMapped();
void unmap();
bool beginWrite(const char*);
bool appendFile(const char*, const char*, uint8_t);
bool finaliseWrite();
bool mmap(const char*, uint8_t, const uint8_t**, size_t*);
}  // namespace FlashFontPartition
