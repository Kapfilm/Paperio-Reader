#pragma once

#include <HalStorage.h>

#include <cstdint>

namespace ProgressiveJpegDc {

enum class Result : uint8_t {
  Ok,
  Unsupported,
  InvalidData,
  IoError,
  OutOfMemory,
  Aborted,
  Stopped,
};

using RowCallback = bool (*)(void* user, uint16_t y, const uint8_t* grayscale, uint16_t width);
using AbortCallback = bool (*)(void* user);

struct DecodeOptions {
  uint16_t outputWidth = 0;
  uint16_t outputHeight = 0;
  AbortCallback shouldAbort = nullptr;
  void* abortUser = nullptr;
};

struct ImageInfo {
  uint16_t width = 0;
  uint16_t height = 0;
};

// Reads marker segments through SOF and rewinds the file before returning.
// Returns Unsupported for baseline and other JPEG coding modes.
Result probe(FsFile& file, ImageInfo& info);

// Decodes SOF2 JPEG luminance into resized grayscale rows. Small covers up to
// 512x768 (4096 padded blocks) use all DC/AC scans, with one coefficient row
// plus a nonzero-bit mask per block; scans are reread for each block row.
// Larger covers or initial allocation failure use the bounded DC-only preview.
// The input is rewound; no full-image coefficient/pixel buffer is allocated.
Result decode(FsFile& file, const DecodeOptions& options, RowCallback rowCallback, void* rowUser);

const char* resultName(Result result);

}  // namespace ProgressiveJpegDc
