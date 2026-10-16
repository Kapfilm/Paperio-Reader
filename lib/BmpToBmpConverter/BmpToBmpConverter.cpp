#include "BmpToBmpConverter.h"

#include <Bitmap.h>
#include <BitmapHelpers.h>
#include <CooperativeAbort.h>
#include <Logging.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

bool BmpToBmpConverter::bmpFileTo1BitBmpStreamWithSize(FsFile& bmpFile, Print& bmpOut, const int targetMaxWidth,
                                                       const int targetMaxHeight, const bool crop) {
  if (targetMaxWidth <= 0 || targetMaxHeight <= 0) return false;

  Bitmap source(bmpFile, true);
  if (source.parseHeaders() != BmpReaderError::Ok || !source.isComplete()) return false;

  const int sourceWidth = source.getWidth();
  const int sourceHeight = source.getHeight();
  int cropX = 0;
  int cropY = 0;
  int cropWidth = sourceWidth;
  int cropHeight = sourceHeight;

  if (crop) {
    if (static_cast<int64_t>(sourceWidth) * targetMaxHeight > static_cast<int64_t>(targetMaxWidth) * sourceHeight) {
      cropWidth = std::max(1, static_cast<int>(static_cast<int64_t>(sourceHeight) * targetMaxWidth / targetMaxHeight));
      cropX = (sourceWidth - cropWidth) / 2;
    } else {
      cropHeight = std::max(1, static_cast<int>(static_cast<int64_t>(sourceWidth) * targetMaxHeight / targetMaxWidth));
      cropY = (sourceHeight - cropHeight) / 2;
    }
  }

  int outputWidth = cropWidth;
  int outputHeight = cropHeight;
  if (outputWidth > targetMaxWidth || outputHeight > targetMaxHeight) {
    if (static_cast<int64_t>(outputWidth) * targetMaxHeight > static_cast<int64_t>(targetMaxWidth) * outputHeight) {
      outputHeight = std::max(1, static_cast<int>(static_cast<int64_t>(outputHeight) * targetMaxWidth / outputWidth));
      outputWidth = targetMaxWidth;
    } else {
      outputWidth = std::max(1, static_cast<int>(static_cast<int64_t>(outputWidth) * targetMaxHeight / outputHeight));
      outputHeight = targetMaxHeight;
    }
  }

  BmpHeader header{};
  createBmpHeader(&header, outputWidth, outputHeight,
                  source.isTopDown() ? BmpRowOrder::TopDown : BmpRowOrder::BottomUp);
  if (bmpOut.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) != sizeof(header)) return false;

  const int sourcePackedBytes = (sourceWidth + 3) / 4;
  const int outputRowBytes = ((outputWidth + 31) / 32) * 4;
  std::unique_ptr<uint8_t[]> decoded(new (std::nothrow) uint8_t[sourcePackedBytes]);
  std::unique_ptr<uint8_t[]> raw(new (std::nothrow) uint8_t[source.getRowBytes()]);
  std::unique_ptr<uint32_t[]> dark(new (std::nothrow) uint32_t[outputWidth]());
  std::unique_ptr<uint32_t[]> total(new (std::nothrow) uint32_t[outputWidth]());
  std::unique_ptr<uint8_t[]> output(new (std::nothrow) uint8_t[outputRowBytes]);
  if (!decoded || !raw || !dark || !total || !output) return false;

  auto writeRow = [&]() {
    memset(output.get(), 0xFF, outputRowBytes);
    for (int x = 0; x < outputWidth; ++x) {
      if (total[x] && static_cast<uint32_t>(dark[x]) * 2 >= total[x]) {
        output[x >> 3] &= static_cast<uint8_t>(~(0x80 >> (x & 7)));
      }
    }
    return bmpOut.write(output.get(), outputRowBytes) == static_cast<size_t>(outputRowBytes);
  };

  int activeOutputY = -1;
  int writtenRows = 0;
  for (int fileY = 0; fileY < sourceHeight; ++fileY) {
    if (CooperativeAbort::shouldAbortLongTask()) {
      CooperativeAbort::markAborted();
      return false;
    }
    if (source.readNextRow(decoded.get(), raw.get()) != BmpReaderError::Ok) return false;

    const int visualY = source.isTopDown() ? fileY : sourceHeight - 1 - fileY;
    if (visualY < cropY || visualY >= cropY + cropHeight) continue;
    const int outputY = static_cast<int>(static_cast<int64_t>(visualY - cropY) * outputHeight / cropHeight);
    if (activeOutputY != outputY) {
      if (activeOutputY >= 0) {
        if (!writeRow()) return false;
        ++writtenRows;
      }
      memset(dark.get(), 0, outputWidth * sizeof(uint32_t));
      memset(total.get(), 0, outputWidth * sizeof(uint32_t));
      activeOutputY = outputY;
    }

    for (int x = cropX; x < cropX + cropWidth; ++x) {
      const int outputX = static_cast<int>(static_cast<int64_t>(x - cropX) * outputWidth / cropWidth);
      const uint8_t gray = (decoded[x >> 2] >> (6 - ((x & 3) * 2))) & 0x03;
      ++total[outputX];
      if (gray < 2) ++dark[outputX];
    }
  }

  if (activeOutputY >= 0) {
    if (!writeRow()) return false;
    ++writtenRows;
  }
  if (writtenRows != outputHeight) {
    LOG_ERR("BMP", "Scaled BMP wrote %d of %d rows", writtenRows, outputHeight);
    return false;
  }
  return true;
}
