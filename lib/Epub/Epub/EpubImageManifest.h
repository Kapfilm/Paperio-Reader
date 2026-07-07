#pragma once

#include <ZipFile.h>

#include <memory>
#include <string>
#include <vector>

// Per-image entry: just the normalised key and its pixel dimensions. Dimensions are the only
// thing any consumer reads (pagination needs them; the parser reads width/height and nothing
// else). Render-time extraction re-resolves the ZIP entry by path via ImageBlock::ensureExtracted,
// so no ZIP-stat fields are cached here.
struct ImageManifestEntry {
  std::string epubEntryPath;  // normalised key, e.g. "OEBPS/images/foo.jpg"
  int16_t width = 0;
  int16_t height = 0;
};

// Incremental, persisted image-dimension cache. The book is NOT scanned up front: the
// manifest starts from whatever images.bin holds (possibly empty) and grows one image at a
// time via ensureResolved() as section indexing first encounters each image. Each image's
// header is therefore read at most once in the book's lifetime; nothing is held resident for
// images you never reach. (Eagerly building the whole thing cost tens of seconds and shredded
// the heap on image-heavy books — see git history.)
class EpubImageManifest {
 public:
  // v4: entries are now just {key, width, height}. Dropped the unused per-entry extractedPath and
  // the unused ZIP-stat fields (method/compressed/uncompressed/localHeaderOffset) — all were
  // written and persisted but never read. Bumping invalidates older caches, which then refill
  // incrementally. (v2 added normalised keys; v3 dropped extractedPath.)
  static constexpr uint8_t VERSION = 4;

  // Load images.bin from cachePath into memory. A missing (or stale-version) file is NOT an
  // error: it yields an empty but loaded manifest that ensureResolved() fills incrementally.
  // Always remembers cachePath for later persistIfDirty().
  bool load(const std::string& cachePath);

  bool isLoaded() const { return loaded_; }

  // Look up an image's dimensions, resolving + caching them on a miss. On a miss reads just
  // the image header (by central-directory offset) from epubPath, appends the entry in sorted
  // order, and marks the manifest dirty for the next persistIfDirty(). epubEntryPath must be
  // the normalised path (matches find()'s key). Returns nullptr if the header can't be read.
  const ImageManifestEntry* ensureResolved(const std::string& epubPath, const std::string& epubEntryPath);

  // Returns nullptr when the entry is not (yet) in the manifest.
  const ImageManifestEntry* find(const std::string& epubEntryPath) const;

  // Rewrite images.bin if entries were added since the last persist. Cheap no-op when clean.
  // Also releases the reused resolve handle (see closeResolveHandle): callers invoke this at
  // each build's end, which is also when the run of ensureResolved() resolves is finished.
  void persistIfDirty();

 private:
  // Close the SD descriptor that ensureResolved() keeps open across a build's images. Run from
  // persistIfDirty() regardless of dirty state, so a build that resolved nothing new (or only
  // failed lookups) still releases the handle.
  void closeResolveHandle();

  std::vector<ImageManifestEntry> entries_;
  std::string cachePath_;
  bool loaded_ = false;
  bool dirty_ = false;

  // One ZipFile reused across ensureResolved() misses (see the .cpp). ZipFile caches the
  // EOCD details and a sequential central-directory cursor in its members; reusing the
  // instance keeps both alive between images instead of re-scanning the whole central
  // directory per image. resolveEpubPath_ backs the reference ZipFile holds, so it must
  // outlive resolveZip_ — they are always (re)assigned together.
  std::string resolveEpubPath_;
  std::unique_ptr<ZipFile> resolveZip_;
};
