#include "BookArchiveUtils.h"

#include <FsHelpers.h>
#include <ZipFile.h>

namespace {
bool hasExtension(std::string_view path, const char* extension) {
  return FsHelpers::checkFileExtension(path, extension);
}
}  // namespace

bool isFb2BookPath(const std::string_view path) {
  return hasExtension(path, ".fb2") || hasExtension(path, ".fb2.zip");
}

bool isBookZipPath(const std::string_view path) { return hasExtension(path, ".zip"); }

bool isFb2OrZipBookPath(const std::string_view path) { return isFb2BookPath(path) || isBookZipPath(path); }

BookArchiveType detectBookArchiveType(const std::string& path) {
  ZipFile zip(path);

  size_t containerSize = 0;
  if (zip.getInflatedFileSize("META-INF/container.xml", &containerSize) && containerSize > 0) {
    return BookArchiveType::Epub;
  }

  bool containsFb2 = false;
  if (!zip.streamCentralDirectoryNames([&](const std::string_view entryPath) {
        if (hasExtension(entryPath, ".fb2")) containsFb2 = true;
      })) {
    return BookArchiveType::None;
  }
  return containsFb2 ? BookArchiveType::Fb2 : BookArchiveType::None;
}
