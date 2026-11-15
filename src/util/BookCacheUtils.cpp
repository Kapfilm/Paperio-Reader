#include "BookCacheUtils.h"

#include <Epub.h>
#include <Fb2.h>
#include <Fb2StateMigration.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Txt.h>
#include <Xtc.h>

#include "../GlobalBookmarkIndex.h"
#include "../RecentBooksStore.h"
#include "BookArchiveUtils.h"

namespace {
bool copyStateFile(const std::string& oldPath, const std::string& newPath) {
  FsFile source;
  if (!Storage.openFileForRead("BOOK", oldPath, source)) return false;
  const uint64_t size = source.fileSize64();
  source.close();

  if (!Storage.copyFile("BOOK", oldPath, newPath.c_str())) return false;
  FsFile copy;
  return Storage.openFileForRead("BOOK", newPath, copy) && copy.fileSize64() == size && copy.close();
}
}  // namespace

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

bool clearGeneratedBookCacheForPath(const std::string& sourcePath) {
  const bool fb2 = isFb2BookPath(sourcePath);
  const auto type = !fb2 && isBookZipPath(sourcePath) ? detectBookArchiveType(sourcePath) : BookArchiveType::None;
  if (fb2 || type == BookArchiveType::Fb2) return Fb2(sourcePath, "/.crosspoint").clearGeneratedCache();
  if (FsHelpers::hasEpubExtension(sourcePath) || type == BookArchiveType::Epub) {
    return Epub(sourcePath, "/.crosspoint").clearCache(false, true);
  }
  if (FsHelpers::hasXtcExtension(sourcePath)) return Xtc(sourcePath, "/.crosspoint").clearCache();
  return false;
}

bool transferBookStateAfterMove(const std::string& oldPath, const std::string& newPath) {
  const auto type = isBookZipPath(newPath) && !isFb2BookPath(newPath)
                        ? detectBookArchiveType(newPath)
                        : BookArchiveType::None;
  const bool fb2 = isFb2BookPath(newPath) || type == BookArchiveType::Fb2;
  const bool epub = FsHelpers::hasEpubExtension(newPath) || type == BookArchiveType::Epub;
  if (!fb2 && !epub) return true;
  // A changed extension can make the file a different reader format. Do not
  // put state from a recognised different format into the new book's cache.
  if ((fb2 && FsHelpers::hasEpubExtension(oldPath)) || (epub && isFb2BookPath(oldPath))) return true;

  const std::string oldCache = fb2 ? Fb2(oldPath, "/.crosspoint").getCachePath()
                                   : Epub(oldPath, "/.crosspoint").getCachePath();
  const std::string newCache = fb2 ? Fb2(newPath, "/.crosspoint").getCachePath()
                                   : Epub(newPath, "/.crosspoint").getCachePath();
  if (oldCache != newCache && Storage.exists(oldCache.c_str())) {
    if (Storage.exists(newCache.c_str())) return false;
    const std::string staged = newCache + ".move-tmp";
    if (Storage.exists(staged.c_str()) && !Storage.removeDir(staged.c_str())) return false;
    if (!Storage.mkdir(staged.c_str(), true)) return false;
    const auto copyIfPresent = [&](const char* name) {
      const std::string from = oldCache + "/" + name;
      return !Storage.exists(from.c_str()) || copyStateFile(from, staged + "/" + name);
    };
    // A pending v29 migration still refers to its archived cache. Keep that
    // whole recovery chain available at the original path rather than
    // associating only part of it with the moved book.
    if (fb2 && Fb2StateMigration::needsReaderMigration(oldCache)) {
      Storage.removeDir(staged.c_str());
      return false;
    }
    constexpr const char* stateFiles[] = {
        "progress.bin", "progress.bin.bak", "bookmarks.bin", "reader_settings.bin", "stats.bin",
    };
    bool success = true;
    for (const auto* name : stateFiles) {
      if (!copyIfPresent(name)) {
        success = false;
        break;
      }
    }
    // Keep the source layout evidence. For an unmigrated v29 package the old
    // section table is also needed to translate its saved chapter positions.
    if (success && fb2) {
      success = copyIfPresent(Fb2StateMigration::NATIVE_LAYOUT_FILE) &&
                copyIfPresent(Fb2StateMigration::PACKAGE_STATE_FILE) && copyIfPresent(".fb2_sections.bin");
    }
    if (!success || !Storage.rename(staged.c_str(), newCache.c_str())) {
      Storage.removeDir(staged.c_str());
      return false;
    }
  }
  RECENT_BOOKS.renameBook(oldPath, newPath);
  GLOBAL_BOOKMARKS.renameBook(oldPath, newPath, newCache);
  return true;
}
