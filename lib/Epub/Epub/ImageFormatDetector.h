#pragma once

#include <cstdint>
#include <cstring>

#include "converters/ImageToFramebufferDecoder.h"

// Shared utility for detecting image formats and parsing dimensions.
// Used by cover detection, image manifest resolution, and image converters.
class ImageFormatDetector {
 public:
  enum class Format : uint8_t { Unknown, Jpeg, Png, Gif };

  // Detect image format from buffer header by sniffing magic bytes.
  // Returns Format::Unknown if the buffer is too small or format is not recognized.
  static Format detect(const uint8_t* buf, size_t len) {
    if (!buf || len == 0) return Format::Unknown;

    // JPEG: starts with 0xFFD8FF
    if (len >= 3 && buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF) {
      return Format::Jpeg;
    }

    // PNG: starts with 0x89504E47 (89 'P' 'N' 'G')
    if (len >= 8 && buf[0] == 0x89 && buf[1] == 0x50 && buf[2] == 0x4E && buf[3] == 0x47) {
      return Format::Png;
    }

    // GIF: starts with "GIF" (47 49 46)
    if (len >= 3 && buf[0] == 'G' && buf[1] == 'I' && buf[2] == 'F') {
      return Format::Gif;
    }

    return Format::Unknown;
  }

  // Helper to check if detected format is actually valid (covers at least the minimum header size)
  static bool isValidFormat(Format fmt, size_t bufLen) {
    switch (fmt) {
      case Format::Jpeg:
        return bufLen >= 3;
      case Format::Png:
        return bufLen >= 8;
      case Format::Gif:
        return bufLen >= 10;  // Need at least logical-screen header
      case Format::Unknown:
        return false;
    }
    return false;
  }
};
