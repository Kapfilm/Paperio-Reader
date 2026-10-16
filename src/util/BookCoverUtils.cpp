#include "BookCoverUtils.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdint>
#include <vector>

#include "BookCacheUtils.h"

namespace {
constexpr const char* CUSTOM_SUFFIXES[] = {
    // SD cards use a case-insensitive FAT/exFAT filesystem. Listing uppercase
    // aliases here made install() treat the just-written ".cover.jpg" as a
    // different ".cover.JPG" path and delete the same file during cleanup.
    ".cover.jpg",
    ".cover.jpeg",
    ".cover.png",
    ".cover.bmp",
};

bool removeIfPresent(const std::string& path) { return !Storage.exists(path.c_str()) || Storage.remove(path.c_str()); }

bool fileSize(const std::string& path, uint64_t& size) {
  HalFile file;
  if (!Storage.openFileForRead("COVER", path, file)) return false;
  size = file.fileSize64();
  file.close();
  return true;
}

std::string revisionPath(const std::string& bookPath) { return BookCoverUtils::basePath(bookPath) + ".cover.rev"; }

bool writeRevision(const std::string& bookPath, const uint32_t revision) {
  const std::string path = revisionPath(bookPath);
  const std::string partial = path + ".part";
  removeIfPresent(partial);
  HalFile file;
  if (!Storage.openFileForWrite("COVER", partial, file)) return false;
  const bool ok = file.write(&revision, sizeof(revision)) == sizeof(revision);
  file.close();
  if (!ok) {
    removeIfPresent(partial);
    return false;
  }
  removeIfPresent(path);
  if (!Storage.rename(partial.c_str(), path.c_str())) {
    removeIfPresent(partial);
    return false;
  }
  return true;
}
}  // namespace

namespace BookCoverUtils {

std::string basePath(const std::string& bookPath) {
  const size_t separator = bookPath.find_last_of("/\\");
  size_t dot = bookPath.rfind('.');
  if (dot == std::string::npos || (separator != std::string::npos && dot < separator)) return bookPath;
  if (FsHelpers::checkFileExtension(bookPath, ".fb2.zip") && dot >= 4) dot -= 4;
  return bookPath.substr(0, dot);
}

std::string findCustomCover(const std::string& bookPath) {
  const std::string base = basePath(bookPath);
  for (const char* suffix : CUSTOM_SUFFIXES) {
    const std::string candidate = base + suffix;
    if (Storage.exists(candidate.c_str())) return candidate;
  }
  return {};
}

bool hasCustomCover(const std::string& bookPath) { return !findCustomCover(bookPath).empty(); }

uint32_t customCoverRevision(const std::string& bookPath) {
  if (!hasCustomCover(bookPath)) return 0;
  HalFile file;
  const std::string path = revisionPath(bookPath);
  if (!Storage.openFileForRead("COVER", path, file)) return 1;
  uint32_t revision = 0;
  const bool valid = file.read(&revision, sizeof(revision)) == sizeof(revision) && revision != 0;
  file.close();
  return valid ? revision : 1;
}

void invalidateDerivedFiles(const std::string& bookPath) {
  const std::string cachePath = getBookCachePath(bookPath);
  if (cachePath.empty() || !Storage.exists(cachePath.c_str())) return;

  std::vector<std::string> removals;
  HalFile directory = Storage.open(cachePath.c_str());
  if (directory && directory.isDirectory()) {
    char name[160];
    for (HalFile entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
      entry.getName(name, sizeof(name));
      const bool isFile = !entry.isDirectory();
      entry.close();
      if (!isFile) continue;
      const std::string filename(name);
      const bool thumbnail = filename.rfind("thumb_", 0) == 0 && FsHelpers::hasBmpExtension(filename);
      const bool fullCover = filename == "cover.bmp" || filename == "cover_crop.bmp" || filename == "cover.img";
      if (thumbnail || fullCover) removals.push_back(cachePath + "/" + filename);
    }
    directory.close();
  } else if (directory) {
    directory.close();
  }

  for (const auto& path : removals) {
    if (!Storage.remove(path.c_str())) LOG_ERR("COVER", "Failed to remove derived cover: %s", path.c_str());
  }
}

bool install(const std::string& bookPath, const std::string& imagePath) {
  const uint32_t oldRevision = customCoverRevision(bookPath);
  const char* extension = nullptr;
  if (FsHelpers::hasJpgExtension(imagePath)) {
    extension = ".jpg";
  } else if (FsHelpers::hasPngExtension(imagePath)) {
    extension = ".png";
  } else if (FsHelpers::hasBmpExtension(imagePath)) {
    extension = ".bmp";
  } else {
    LOG_ERR("COVER", "Unsupported custom cover format: %s", imagePath.c_str());
    return false;
  }

  const std::string destination = basePath(bookPath) + ".cover" + extension;
  if (imagePath == destination) {
    invalidateDerivedFiles(bookPath);
    return true;
  }

  const std::string partial = destination + ".part";
  const std::string backup = destination + ".bak";
  removeIfPresent(partial);
  removeIfPresent(backup);

  if (!Storage.copyFile("COVER", imagePath, partial.c_str())) {
    removeIfPresent(partial);
    return false;
  }

  uint64_t sourceSize = 0;
  uint64_t stagedSize = 0;
  if (!fileSize(imagePath, sourceSize) || !fileSize(partial, stagedSize) || sourceSize == 0 ||
      sourceSize != stagedSize) {
    LOG_ERR("COVER", "Custom cover copy verification failed: %s", imagePath.c_str());
    removeIfPresent(partial);
    return false;
  }

  const bool hadDestination = Storage.exists(destination.c_str());
  if (hadDestination && !Storage.rename(destination.c_str(), backup.c_str())) {
    removeIfPresent(partial);
    return false;
  }
  if (!Storage.rename(partial.c_str(), destination.c_str())) {
    if (hadDestination) Storage.rename(backup.c_str(), destination.c_str());
    removeIfPresent(partial);
    return false;
  }
  removeIfPresent(backup);

  const std::string base = basePath(bookPath);
  for (const char* suffix : CUSTOM_SUFFIXES) {
    const std::string oldPath = base + suffix;
    if (oldPath != destination) removeIfPresent(oldPath);
  }

  const uint32_t newRevision = oldRevision == UINT32_MAX ? 1 : oldRevision + 1;
  if (!writeRevision(bookPath, newRevision)) {
    LOG_ERR("COVER", "Failed to write custom cover revision for %s", bookPath.c_str());
  }

  invalidateDerivedFiles(bookPath);
  LOG_INF("COVER", "Installed custom cover %s for %s", destination.c_str(), bookPath.c_str());
  return true;
}

bool reset(const std::string& bookPath) {
  bool ok = true;
  const std::string base = basePath(bookPath);
  for (const char* suffix : CUSTOM_SUFFIXES) {
    const std::string path = base + suffix;
    if (!removeIfPresent(path)) ok = false;
    removeIfPresent(path + ".part");
    removeIfPresent(path + ".bak");
  }
  removeIfPresent(revisionPath(bookPath));
  removeIfPresent(revisionPath(bookPath) + ".part");
  invalidateDerivedFiles(bookPath);
  LOG_INF("COVER", "Restored embedded cover for %s", bookPath.c_str());
  return ok;
}

}  // namespace BookCoverUtils
