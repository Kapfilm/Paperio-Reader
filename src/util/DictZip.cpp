#include "DictZip.h"

#include <InflateReader.h>
#include <Memory.h>

#include <algorithm>
#include <cstddef>

namespace DictZip {
namespace {

constexpr size_t INPUT_BYTES = 2048;
constexpr size_t OUTPUT_BYTES = 512;
constexpr size_t TABLE_SCAN_BYTES = 128;

bool readLe16(HalFile& file, uint16_t* out) {
  uint8_t raw[2];
  if (file.read(raw, sizeof(raw)) != static_cast<int>(sizeof(raw))) return false;
  *out = static_cast<uint16_t>(raw[0] | (static_cast<uint16_t>(raw[1]) << 8));
  return true;
}

bool sumChunkSizes(HalFile& file, const uint32_t tableOffset, uint32_t count, uint32_t* total) {
  if (!total || !file.seekSet(tableOffset)) return false;
  uint8_t buffer[TABLE_SCAN_BYTES];
  *total = 0;
  while (count > 0) {
    const uint32_t batchCount = std::min<uint32_t>(count, TABLE_SCAN_BYTES / sizeof(uint16_t));
    const size_t batchBytes = batchCount * sizeof(uint16_t);
    if (file.read(buffer, batchBytes) != static_cast<int>(batchBytes)) return false;
    for (uint32_t i = 0; i < batchCount; ++i) {
      const uint16_t compressedSize =
          static_cast<uint16_t>(buffer[i * 2] | (static_cast<uint16_t>(buffer[i * 2 + 1]) << 8));
      if (compressedSize == 0) return false;
      *total += compressedSize;
    }
    count -= batchCount;
  }
  return true;
}

struct ChunkSource {
  InflateReader reader;  // Must remain first: the uzlib callback receives its address.
  HalFile* file = nullptr;
  uint32_t remaining = 0;
  bool readFailed = false;
  uint8_t buffer[INPUT_BYTES] = {};
};

static_assert(offsetof(ChunkSource, reader) == 0);

int chunkRead(uzlib_uncomp* uncomp) {
  auto* source = reinterpret_cast<ChunkSource*>(uncomp);
  if (source->remaining == 0) return -1;
  const uint32_t wanted = std::min<uint32_t>(source->remaining, INPUT_BYTES);
  const int read = source->file->read(source->buffer, static_cast<int>(wanted));
  if (read <= 0) {
    source->readFailed = true;
    return -1;
  }
  source->remaining -= static_cast<uint32_t>(read);
  uncomp->source = source->buffer + 1;
  uncomp->source_limit = source->buffer + read;
  return source->buffer[0];
}

bool extractChunk(HalFile& file, const uint32_t compressedOffset, const uint32_t compressedSize, uint32_t discard,
                  uint32_t extract, HalFile& output, ExtractError* error) {
  const auto fail = [error](const ExtractError value) {
    if (error) *error = value;
    return false;
  };
  if (extract == 0) return true;

  const size_t ringBytes = InflateReader::ringSizeFor(0);
  auto ring = makeUniqueNoThrow<uint8_t[]>(ringBytes);
  if (!ring) return fail(ExtractError::LowMemory);
  auto source = makeUniqueNoThrow<ChunkSource>();
  if (!source) return fail(ExtractError::LowMemory);
  auto buffer = makeUniqueNoThrow<uint8_t[]>(OUTPUT_BYTES);
  if (!buffer) return fail(ExtractError::LowMemory);

  if (!file.seekSet(compressedOffset)) return fail(ExtractError::ReadError);
  source->file = &file;
  source->remaining = compressedSize;
  if (!source->reader.initWithExternalRing(ring.get(), ringBytes)) return fail(ExtractError::LowMemory);
  source->reader.setReadCallback(&chunkRead);

  const auto decodeFailed = [&] {
    return fail(source->readFailed ? ExtractError::ReadError : ExtractError::Decompress);
  };
  while (discard > 0) {
    const size_t batch = std::min<size_t>(discard, OUTPUT_BYTES);
    if (!source->reader.read(buffer.get(), batch)) return decodeFailed();
    discard -= static_cast<uint32_t>(batch);
  }
  while (extract > 0) {
    const size_t batch = std::min<size_t>(extract, OUTPUT_BYTES);
    if (!source->reader.read(buffer.get(), batch)) return decodeFailed();
    if (output.write(buffer.get(), batch) != batch) return fail(ExtractError::ReadError);
    extract -= static_cast<uint32_t>(batch);
  }
  return true;
}

}  // namespace

bool parse(HalFile& file, Info* info, ExtractError* error) {
  const auto fail = [error](const ExtractError value) {
    if (error) *error = value;
    return false;
  };
  if (error) *error = ExtractError::None;
  if (!info) return fail(ExtractError::Decompress);
  *info = {};

  uint8_t header[10];
  if (file.read(header, sizeof(header)) != static_cast<int>(sizeof(header))) return fail(ExtractError::ReadError);
  if (header[0] != 0x1f || header[1] != 0x8b || header[2] != 8 || (header[3] & 0x04) == 0)
    return fail(ExtractError::Decompress);

  const uint8_t flags = header[3];
  uint16_t extraLength = 0;
  if (!readLe16(file, &extraLength)) return fail(ExtractError::ReadError);
  uint32_t extraRead = 0;
  bool foundRa = false;
  while (extraRead + 4 <= extraLength) {
    uint8_t subHeader[4];
    if (file.read(subHeader, sizeof(subHeader)) != static_cast<int>(sizeof(subHeader)))
      return fail(ExtractError::ReadError);
    extraRead += 4;
    const uint16_t subLength = static_cast<uint16_t>(subHeader[2] | (static_cast<uint16_t>(subHeader[3]) << 8));
    if (extraRead + subLength > extraLength) return fail(ExtractError::Decompress);
    if (subHeader[0] != 'R' || subHeader[1] != 'A') {
      if (!file.seekSet(file.position() + subLength)) return fail(ExtractError::ReadError);
      extraRead += subLength;
      continue;
    }
    if (foundRa || subLength < 6) return fail(ExtractError::Decompress);
    uint16_t version = 0;
    uint16_t chunkLength = 0;
    uint16_t chunkCount = 0;
    if (!readLe16(file, &version) || !readLe16(file, &chunkLength) || !readLe16(file, &chunkCount))
      return fail(ExtractError::ReadError);
    extraRead += 6;
    const uint32_t tableBytes = static_cast<uint32_t>(chunkCount) * sizeof(uint16_t);
    if (version != 1 || chunkLength == 0 || chunkCount == 0 || static_cast<uint32_t>(subLength) != 6U + tableBytes)
      return fail(ExtractError::Decompress);
    info->chunkTableOffset = static_cast<uint32_t>(file.position());
    info->chunkLength = chunkLength;
    info->chunkCount = chunkCount;
    if (!file.seekSet(file.position() + tableBytes)) return fail(ExtractError::ReadError);
    extraRead += tableBytes;
    foundRa = true;
  }
  if (extraRead != extraLength || !foundRa) return fail(ExtractError::Decompress);

  const auto skipCString = [&]() {
    int byte = 0;
    do {
      byte = file.read();
      if (byte < 0) return false;
    } while (byte != 0);
    return true;
  };
  if ((flags & 0x08) && !skipCString()) return fail(ExtractError::ReadError);
  if ((flags & 0x10) && !skipCString()) return fail(ExtractError::ReadError);
  if (flags & 0x02) {
    uint8_t crc[2];
    if (file.read(crc, sizeof(crc)) != static_cast<int>(sizeof(crc))) return fail(ExtractError::ReadError);
  }

  info->dataOffset = static_cast<uint32_t>(file.position());
  const uint32_t fileSize = static_cast<uint32_t>(file.fileSize());
  if (fileSize < 4 || !file.seekSet(fileSize - 4)) return fail(ExtractError::ReadError);
  uint8_t sizeBytes[4];
  if (file.read(sizeBytes, sizeof(sizeBytes)) != static_cast<int>(sizeof(sizeBytes)))
    return fail(ExtractError::ReadError);
  info->totalSize = static_cast<uint32_t>(sizeBytes[0]) | (static_cast<uint32_t>(sizeBytes[1]) << 8) |
                    (static_cast<uint32_t>(sizeBytes[2]) << 16) | (static_cast<uint32_t>(sizeBytes[3]) << 24);
  if (info->totalSize == 0) return fail(ExtractError::Decompress);
  info->valid = true;
  return true;
}

bool extractEntry(const char* path, const uint32_t offset, const uint32_t size, HalFile& output, ExtractError* error) {
  const auto fail = [error](const ExtractError value) {
    if (error) *error = value;
    return false;
  };
  if (error) *error = ExtractError::None;
  if (size == 0) return true;
  HalFile file;
  if (!Storage.openFileForRead("DICTZIP", path, file)) return fail(ExtractError::ReadError);
  Info info;
  if (!parse(file, &info, error)) {
    file.close();
    return false;
  }
  if (offset > info.totalSize || size > info.totalSize - offset) {
    file.close();
    return fail(ExtractError::ReadError);
  }

  const uint32_t firstChunk = offset / info.chunkLength;
  const uint32_t lastChunk = (offset + size - 1) / info.chunkLength;
  if (lastChunk >= info.chunkCount) {
    file.close();
    return fail(ExtractError::ReadError);
  }

  // Scan through a fixed 128-byte stack buffer. Keeping the whole table would
  // cost 55 KB for the Russian Wiktionary (13,802 chunks), competing with the
  // 32 KB inflate ring on an ESP32-C3 with no PSRAM.
  uint32_t precedingCompressedBytes = 0;
  if (!sumChunkSizes(file, info.chunkTableOffset, firstChunk, &precedingCompressedBytes)) {
    file.close();
    return fail(ExtractError::ReadError);
  }
  uint32_t compressedOffset = info.dataOffset + precedingCompressedBytes;

  uint32_t remaining = size;
  for (uint32_t chunk = firstChunk; chunk <= lastChunk; ++chunk) {
    uint16_t compressedSize = 0;
    if (!file.seekSet(info.chunkTableOffset + chunk * sizeof(uint16_t)) || !readLe16(file, &compressedSize) ||
        compressedSize == 0) {
      file.close();
      return fail(ExtractError::ReadError);
    }
    uint32_t outputSize = info.chunkLength;
    if (chunk + 1 == info.chunkCount) outputSize = info.totalSize - chunk * info.chunkLength;
    if (outputSize == 0 || outputSize > info.chunkLength) outputSize = info.chunkLength;
    const uint32_t localOffset = chunk == firstChunk ? offset % info.chunkLength : 0;
    if (localOffset >= outputSize) {
      file.close();
      return fail(ExtractError::Decompress);
    }
    const uint32_t take = std::min(remaining, outputSize - localOffset);
    if (!extractChunk(file, compressedOffset, compressedSize, localOffset, take, output, error)) {
      file.close();
      return false;
    }
    compressedOffset += compressedSize;
    remaining -= take;
  }
  file.close();
  return remaining == 0 || fail(ExtractError::Decompress);
}

}  // namespace DictZip
