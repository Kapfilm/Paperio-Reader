#pragma once

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "Bookmark.h"

// Stores starred/bookmarked pages for a single book.
// Persisted as a binary file on SD card within the book's cache directory.
class BookmarkStore {
 public:
  using Bookmark = ::Bookmark;

  // Load bookmarks from the cache directory (e.g. .crosspoint/epub_<hash>/).
  void load(const std::string& cachePath) {
    basePath = cachePath;
    bookmarks.clear();
    dirty = false;

    FsFile f;
    if (!Storage.openFileForRead("BKM", getFilePath(), f)) {
      return;
    }

    uint8_t version;
    if (f.read(reinterpret_cast<uint8_t*>(&version), sizeof(version)) != sizeof(version) || version < 1 ||
        version > FILE_VERSION) {
      f.close();
      return;
    }

    uint16_t count = 0;
    if (f.read(reinterpret_cast<uint8_t*>(&count), sizeof(count)) != sizeof(count) || count > MAX_BOOKMARKS) {
      LOG_ERR("BKM", "Invalid bookmark count: %u", static_cast<unsigned>(count));
      f.close();
      return;
    }

    bookmarks.reserve(count);
    for (uint16_t i = 0; i < count; i++) {
      Bookmark bm;
      if (f.read(reinterpret_cast<uint8_t*>(&bm.spineIndex), sizeof(bm.spineIndex)) != sizeof(bm.spineIndex) ||
          f.read(reinterpret_cast<uint8_t*>(&bm.pageNumber), sizeof(bm.pageNumber)) != sizeof(bm.pageNumber)) {
        LOG_ERR("BKM", "Truncated bookmarks file at entry %d", i);
        bookmarks.clear();
        f.close();
        return;
      }
      if (version >= 2) {
        uint16_t nameLen = 0;
        if (f.read(reinterpret_cast<uint8_t*>(&nameLen), sizeof(nameLen)) != sizeof(nameLen) ||
            nameLen > MAX_NAME_LENGTH) {
          LOG_ERR("BKM", "Invalid bookmark name length at entry %d", i);
          bookmarks.clear();
          f.close();
          return;
        }
        if (nameLen > 0) {
          bm.name.resize(nameLen);
          if (f.read(reinterpret_cast<uint8_t*>(&bm.name[0]), nameLen) != nameLen) {
            LOG_ERR("BKM", "Truncated bookmark name at entry %d", i);
            bookmarks.clear();
            f.close();
            return;
          }
        }
      }
      if (version >= 3) {
        uint16_t anchorLen = 0;
        if (f.read(reinterpret_cast<uint8_t*>(&anchorLen), sizeof(anchorLen)) != sizeof(anchorLen) ||
            anchorLen > MAX_PREVIEW_ANCHOR_LENGTH) {
          bookmarks.clear();
          f.close();
          return;
        }
        bm.previewAnchor.resize(anchorLen);
        if (anchorLen && f.read(reinterpret_cast<uint8_t*>(&bm.previewAnchor[0]), anchorLen) != anchorLen) {
          bookmarks.clear();
          f.close();
          return;
        }
        uint8_t fullNote = 0;
        if (f.read(&fullNote, 1) != 1 || fullNote > 1) {
          bookmarks.clear();
          f.close();
          return;
        }
        bm.fullNote = fullNote != 0;
      }
      if (version >= 4) {
        uint8_t length = 0;
        if (f.read(&length, 1) != 1 || length > MAX_TEXT_ANCHOR_LENGTH) {
          bookmarks.clear();
          f.close();
          return;
        }
        bm.textAnchor.resize(length);
        if (length && f.read(reinterpret_cast<uint8_t*>(bm.textAnchor.data()), length) != length) {
          bookmarks.clear();
          f.close();
          return;
        }
      }
      bookmarks.push_back(std::move(bm));
    }

    f.close();
    LOG_DBG("BKM", "Loaded %d bookmarks", count);
  }

  // Save bookmarks to SD card (only if changed).
  bool save() {
    if (!dirty) return true;
    if (basePath.empty()) return false;

    if (bookmarks.size() > MAX_BOOKMARKS) {
      LOG_ERR("BKM", "Too many bookmarks to save: %u", static_cast<unsigned>(bookmarks.size()));
      return false;
    }

    FsFile f;
    if (!Storage.openFileForWrite("BKM", getFilePath(), f)) {
      LOG_ERR("BKM", "Failed to save bookmarks");
      return false;
    }

    auto writePodChecked = [&f](const auto& value) {
      return f.write(reinterpret_cast<const uint8_t*>(&value), sizeof(value)) == sizeof(value);
    };

    const uint16_t count = static_cast<uint16_t>(bookmarks.size());
    bool ok = writePodChecked(FILE_VERSION) && writePodChecked(count);

    for (const auto& bm : bookmarks) {
      const uint16_t nameLen = static_cast<uint16_t>(std::min<size_t>(bm.name.size(), MAX_NAME_LENGTH));
      ok = ok && writePodChecked(bm.spineIndex) && writePodChecked(bm.pageNumber) && writePodChecked(nameLen);
      if (ok && nameLen > 0) {
        ok = f.write(reinterpret_cast<const uint8_t*>(bm.name.data()), nameLen) == nameLen;
      }
      const uint16_t anchorLen = static_cast<uint16_t>(bm.previewAnchor.size());
      ok = ok && writePodChecked(anchorLen);
      if (ok && anchorLen > 0) {
        ok = f.write(reinterpret_cast<const uint8_t*>(bm.previewAnchor.data()), anchorLen) == anchorLen;
      }
      const uint8_t fullNote = bm.fullNote ? 1 : 0;
      const auto textLength = static_cast<uint8_t>(bm.textAnchor.size());
      ok = ok && writePodChecked(fullNote) && writePodChecked(textLength);
      if (ok && textLength) {
        ok = f.write(reinterpret_cast<const uint8_t*>(bm.textAnchor.data()), textLength) == textLength;
      }
    }

    if (ok) {
      if (!f.close()) {
        LOG_ERR("BKM", "Failed to close bookmarks file");
        return false;
      }
    } else {
      f.close();
      LOG_ERR("BKM", "Failed while writing bookmarks");
      return false;
    }
    dirty = false;
    LOG_DBG("BKM", "Saved %d bookmarks", count);
    return true;
  }

  // Toggle bookmark for the given page. Returns true if now starred, false if removed.
  bool toggle(uint16_t spineIndex, uint16_t pageNumber, const std::string& previewAnchor = {}, bool fullNote = false,
              std::string textAnchor = {}) {
    if (previewAnchor.size() > MAX_PREVIEW_ANCHOR_LENGTH) return false;
    auto it = find(spineIndex, pageNumber, previewAnchor, fullNote);
    if (it != bookmarks.end()) {
      bookmarks.erase(it);
      dirty = true;
      return false;
    }
    if (bookmarks.size() >= MAX_BOOKMARKS) return false;
    bookmarks.push_back({spineIndex, pageNumber, {}, previewAnchor, fullNote, std::move(textAnchor)});
    dirty = true;
    return true;
  }

  // Check if a page is starred.
  [[nodiscard]] bool has(uint16_t spineIndex, uint16_t pageNumber, const std::string& previewAnchor = {},
                         bool fullNote = false) const {
    return std::any_of(bookmarks.begin(), bookmarks.end(),
                       [spineIndex, pageNumber, &previewAnchor, fullNote](const Bookmark& bm) {
                         return bm.spineIndex == spineIndex && bm.pageNumber == pageNumber &&
                                bm.previewAnchor == previewAnchor && bm.fullNote == fullNote;
                       });
  }

  [[nodiscard]] const std::vector<Bookmark>& getAll() const { return bookmarks; }
  [[nodiscard]] bool isEmpty() const { return bookmarks.empty(); }
  void markDirty() { dirty = true; }
  bool setTextAnchor(size_t index, std::string textAnchor) {
    if (textAnchor.empty() || textAnchor.size() > MAX_TEXT_ANCHOR_LENGTH) return false;
    bookmarks[index].textAnchor = std::move(textAnchor);
    dirty = true;
    return true;
  }
  void remapPage(size_t index, uint16_t page) { bookmarks[index].pageNumber = page; }

  // Set or clear the name for the bookmark at index. Empty name reverts to default label.
  void rename(size_t index, std::string name) {
    if (index >= bookmarks.size()) return;
    if (name.size() > MAX_NAME_LENGTH) name.resize(MAX_NAME_LENGTH);
    bookmarks[index].name = std::move(name);
    dirty = true;
  }

  void removeAt(size_t index) {
    if (index >= bookmarks.size()) return;
    bookmarks.erase(bookmarks.begin() + index);
    dirty = true;
  }

  static constexpr uint8_t MAX_TEXT_ANCHOR_LENGTH = 64;
  static constexpr uint16_t MAX_NAME_LENGTH = 128;
  static constexpr uint16_t MAX_PREVIEW_ANCHOR_LENGTH = 191;

 private:
  static constexpr uint8_t FILE_VERSION = 4;
  static constexpr uint16_t MAX_BOOKMARKS = 1000;

  std::vector<Bookmark> bookmarks;
  std::string basePath;
  bool dirty = false;

  [[nodiscard]] std::string getFilePath() const { return basePath + "/bookmarks.bin"; }

  std::vector<Bookmark>::iterator find(uint16_t spineIndex, uint16_t pageNumber, const std::string& previewAnchor,
                                       bool fullNote) {
    return std::find_if(bookmarks.begin(), bookmarks.end(),
                        [spineIndex, pageNumber, &previewAnchor, fullNote](const Bookmark& bm) {
                          return bm.spineIndex == spineIndex && bm.pageNumber == pageNumber &&
                                 bm.previewAnchor == previewAnchor && bm.fullNote == fullNote;
                        });
  }
};
