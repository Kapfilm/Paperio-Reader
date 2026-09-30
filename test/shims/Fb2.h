#pragma once

#include <Print.h>

#include <cstdint>
#include <deque>
#include <string>

namespace reader {
class ReaderCancellationToken;
}

// Host EPUB-pipeline tests do not mount an FB2 source package. Keep the
// production integration linkable while making every FB2-only hook a no-op;
// the real parser/ZIP/encoding paths are covered by NativeFb2ZipTest.
class Fb2 {
 public:
  static constexpr char SOURCE_MARKER_FILE[] = "/.fb2_source";

  static bool renderChapterOnDemand(const std::string&, int, Print&,
                                    const reader::ReaderCancellationToken* = nullptr) {
    return false;
  }
  static bool decodeImageOnDemand(const std::string&, const reader::ReaderCancellationToken* = nullptr) {
    return false;
  }
  static uint32_t getApproxChapterSize(const std::string&, int) { return 0; }
  static bool loadApproxChapterSizes(const std::string&, std::deque<uint32_t>& out) {
    out.clear();
    return false;
  }
  static bool getLogicalChapterBounds(const std::string&, int chapter, int& start, int& end) {
    start = chapter;
    end = chapter;
    return chapter >= 0;
  }
};
