#pragma once

#include <HalStorage.h>

#include <cstdint>

namespace DictZip {

struct Info {
  uint32_t chunkTableOffset = 0;
  uint32_t dataOffset = 0;
  uint32_t totalSize = 0;
  uint16_t chunkLength = 0;
  uint16_t chunkCount = 0;
  bool valid = false;
};

enum class ExtractError : uint8_t { None, LowMemory, ReadError, Decompress };

bool parse(HalFile& file, Info* info, ExtractError* outError = nullptr);
bool extractEntry(const char* path, uint32_t offset, uint32_t size, HalFile& outFile, ExtractError* outError = nullptr);

}  // namespace DictZip
