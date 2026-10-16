#pragma once

#include <HalStorage.h>

class Print;

class BmpToBmpConverter {
 public:
  // Scale/crop any BMP supported by Bitmap into a compact 1-bit thumbnail.
  static bool bmpFileTo1BitBmpStreamWithSize(FsFile& bmpFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight,
                                             bool crop = true);
};
