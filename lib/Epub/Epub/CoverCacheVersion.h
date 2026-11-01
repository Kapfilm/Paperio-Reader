#pragma once

#include <HalStorage.h>

#include <algorithm>
#include <string>
#include <vector>

namespace CoverCacheVersion {
inline constexpr const char* MARKER = "/.cover-render-v2";

// Only disposable rendered covers are invalidated. Original image bytes,
// reading positions, annotations, statistics and book indexes remain intact.
inline void ensureCurrent(const std::string& cachePath) {
  const std::string markerPath = cachePath + MARKER;
  if (Storage.exists(markerPath.c_str())) return;
  FsFile directory = Storage.open(cachePath.c_str());
  if (!directory || !directory.isDirectory()) return;
  std::vector<std::string> stale;
  char name[128];
  for (FsFile entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
    const bool regularFile = !entry.isDirectory();
    entry.getName(name, sizeof(name));
    entry.close();
    const std::string filename(name);
    const bool bmp = filename.size() >= 4 && filename.compare(filename.size() - 4, 4, ".bmp") == 0;
    if (regularFile &&
        (filename == "cover.bmp" || filename == "cover_crop.bmp" || (bmp && filename.rfind("thumb_", 0) == 0)))
      stale.push_back(cachePath + "/" + filename);
  }
  directory.close();
  if (!std::all_of(stale.begin(), stale.end(), [](const std::string& path) { return Storage.remove(path.c_str()); }))
    return;
  FsFile marker;
  if (Storage.openFileForWrite("COVER", markerPath, marker)) marker.close();
}
}  // namespace CoverCacheVersion
