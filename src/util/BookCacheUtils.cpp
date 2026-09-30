#include "BookCacheUtils.h"

#include <Epub.h>
#include <Fb2.h>
#include <FsHelpers.h>
#include <Txt.h>
#include <Xtc.h>

#include "BookArchiveUtils.h"

std::string getBookCachePath(const std::string& sourcePath) {
  if (isFb2BookPath(sourcePath)) return Fb2(sourcePath, "/.crosspoint").getCachePath();
  if (isBookZipPath(sourcePath)) {
    const BookArchiveType type = detectBookArchiveType(sourcePath);
    if (type == BookArchiveType::Fb2) return Fb2(sourcePath, "/.crosspoint").getCachePath();
    if (type == BookArchiveType::Epub) return Epub(sourcePath, "/.crosspoint").getCachePath();
    return {};
  }
  if (FsHelpers::hasEpubExtension(sourcePath)) return Epub(sourcePath, "/.crosspoint").getCachePath();
  if (FsHelpers::hasXtcExtension(sourcePath)) return Xtc(sourcePath, "/.crosspoint").getCachePath();
  if (FsHelpers::hasTxtExtension(sourcePath) || FsHelpers::hasMarkdownExtension(sourcePath)) {
    return Txt(sourcePath, "/.crosspoint").getCachePath();
  }
  return {};
}

bool clearBookCacheForPath(const std::string& sourcePath) {
  if (isFb2BookPath(sourcePath)) return Fb2(sourcePath, "/.crosspoint").clearCache();
  if (isBookZipPath(sourcePath)) {
    const BookArchiveType type = detectBookArchiveType(sourcePath);
    if (type == BookArchiveType::Fb2) return Fb2(sourcePath, "/.crosspoint").clearCache();
    if (type == BookArchiveType::Epub) return Epub(sourcePath, "/.crosspoint").clearCache();
    return false;
  }
  if (FsHelpers::hasEpubExtension(sourcePath)) return Epub(sourcePath, "/.crosspoint").clearCache();
  if (FsHelpers::hasXtcExtension(sourcePath)) return Xtc(sourcePath, "/.crosspoint").clearCache();
  if (FsHelpers::hasTxtExtension(sourcePath) || FsHelpers::hasMarkdownExtension(sourcePath)) {
    const std::string cachePath = Txt(sourcePath, "/.crosspoint").getCachePath();
    return !Storage.exists(cachePath.c_str()) || Storage.removeDir(cachePath.c_str());
  }
  return false;
}
