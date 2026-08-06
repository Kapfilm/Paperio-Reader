#include "ClippingStore.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
constexpr uint8_t FILE_VERSION = 3;
constexpr size_t LEGACY_CHAPTER_TITLE_MAX = 48;
constexpr size_t INITIAL_RESERVE = 4;
constexpr char CLIPPINGS_DIR[] = "/.crosspoint/clippings";

std::string pathForBook(const std::string& filePath) {
  uint32_t hash = 2166136261UL;
  for (const unsigned char byte : filePath) {
    hash ^= byte;
    hash *= 16777619UL;
  }
  return std::string(CLIPPINGS_DIR) + "/epub_" + std::to_string(hash) + ".bin";
}

template <typename T>
bool readPodChecked(FsFile& file, T& value) {
  return file.read(reinterpret_cast<uint8_t*>(&value), sizeof(value)) == sizeof(value);
}

template <typename T>
bool writePodChecked(FsFile& file, const T& value) {
  return file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(value)) == sizeof(value);
}

bool readStringChecked(FsFile& file, std::string& value) {
  uint32_t length = 0;
  if (!readPodChecked(file, length) || length > serialization::MAX_STRING_LENGTH) return false;
  value.resize(length);
  return length == 0 || file.read(reinterpret_cast<uint8_t*>(value.data()), length) == static_cast<int>(length);
}

bool writeStringChecked(FsFile& file, const std::string& value) {
  const uint32_t length = static_cast<uint32_t>(value.size());
  return writePodChecked(file, length) && (length == 0 || file.write(reinterpret_cast<const uint8_t*>(value.data()),
                                                                     length) == static_cast<size_t>(length));
}
}  // namespace

bool ClippingStore::loadForBook(const std::string& filePath, const std::string& title, const std::string& author) {
  bookFilePath = filePath;
  bookTitle = title;
  bookAuthor = author;
  storeFilePath = pathForBook(filePath);
  clippings.clear();
  clippings.reserve(INITIAL_RESERVE);
  return !Storage.exists(storeFilePath.c_str()) || readFromFile();
}

void ClippingStore::unload() {
  clippings.clear();
  bookFilePath.clear();
  bookTitle.clear();
  bookAuthor.clear();
  storeFilePath.clear();
}

ClippingStore::AddResult ClippingStore::addClipping(const uint16_t spineIndex, const uint16_t startPage,
                                                    const uint16_t endPage, const uint16_t pageCount,
                                                    const uint16_t startWordIndex, const uint16_t endWordIndex,
                                                    const uint16_t wordCount, const char* chapterTitle,
                                                    const uint16_t paragraphIndex, const std::string& text,
                                                    const ClippingHighlightStyle highlightStyle) {
  if (clippings.size() >= CLIPPING_MAX_PER_BOOK) return AddResult::LimitReached;

  Clipping clipping;
  clipping.spineIndex = spineIndex;
  clipping.startPage = startPage;
  clipping.endPage = endPage;
  clipping.pageCount = std::max<uint16_t>(1, pageCount);
  clipping.startWordIndex = startWordIndex;
  clipping.endWordIndex = endWordIndex;
  clipping.wordCount = wordCount;
  clipping.paragraphIndex = paragraphIndex;
  clipping.timestamp = static_cast<uint32_t>(millis() / 1000UL);
  clipping.highlightStyle = highlightStyle;
  clipping.chapterTitle = chapterTitle ? chapterTitle : "";
  if (clipping.chapterTitle.size() > CLIPPING_CHAPTER_TITLE_MAX) {
    clipping.chapterTitle.resize(
        utf8SafeTruncateBuffer(clipping.chapterTitle.data(), static_cast<int>(CLIPPING_CHAPTER_TITLE_MAX)));
  }
  clipping.text.assign(text.data(), std::min(text.size(), CLIPPING_TEXT_MAX));

  clippings.push_back(std::move(clipping));
  if (writeToFile()) return AddResult::Added;
  clippings.pop_back();
  return AddResult::SaveFailed;
}

bool ClippingStore::removeAt(const size_t index) {
  if (index >= clippings.size()) return false;
  Clipping removed = std::move(clippings[index]);
  clippings.erase(clippings.begin() + index);
  if (writeToFile()) return true;
  clippings.insert(clippings.begin() + index, std::move(removed));
  return false;
}

void ClippingStore::clearAll() {
  clippings.clear();
  if (!storeFilePath.empty() && Storage.exists(storeFilePath.c_str())) Storage.remove(storeFilePath.c_str());
}

bool ClippingStore::readFromFile() {
  FsFile file;
  if (!Storage.openFileForRead("CLIP", storeFilePath, file)) return false;

  uint8_t version = 0;
  uint16_t count = 0;
  std::string storedTitle;
  std::string storedAuthor;
  std::string storedPath;
  if (!readPodChecked(file, version) || (version < 1 || version > FILE_VERSION) || !readPodChecked(file, count) ||
      count > CLIPPING_MAX_PER_BOOK || !readStringChecked(file, storedTitle) ||
      !readStringChecked(file, storedAuthor) || !readStringChecked(file, storedPath)) {
    LOG_ERR("CLIP", "Invalid clipping file: %s", storeFilePath.c_str());
    file.close();
    return false;
  }
  if (storedPath != bookFilePath || storedTitle != bookTitle || storedAuthor != bookAuthor) {
    LOG_ERR("CLIP", "Clipping file belongs to a different book: %s", storeFilePath.c_str());
    file.close();
    return false;
  }

  clippings.clear();
  clippings.reserve(count);
  for (uint16_t i = 0; i < count; ++i) {
    Clipping clipping;
    if (!readPodChecked(file, clipping.spineIndex) || !readPodChecked(file, clipping.startPage) ||
        !readPodChecked(file, clipping.endPage) || !readPodChecked(file, clipping.pageCount) ||
        !readPodChecked(file, clipping.startWordIndex) || !readPodChecked(file, clipping.endWordIndex) ||
        !readPodChecked(file, clipping.wordCount) || !readPodChecked(file, clipping.paragraphIndex) ||
        !readPodChecked(file, clipping.timestamp)) {
      LOG_ERR("CLIP", "Truncated clipping file at record %u", i);
      clippings.clear();
      file.close();
      return false;
    }
    if (version >= 3) {
      uint8_t highlightStyle = 0;
      if (!readPodChecked(file, highlightStyle) ||
          highlightStyle > static_cast<uint8_t>(ClippingHighlightStyle::BlackMarker)) {
        LOG_ERR("CLIP", "Invalid clipping highlight style at record %u", i);
        clippings.clear();
        file.close();
        return false;
      }
      clipping.highlightStyle = static_cast<ClippingHighlightStyle>(highlightStyle);
    }
    if (version == 1) {
      char legacyTitle[LEGACY_CHAPTER_TITLE_MAX] = {};
      if (file.read(reinterpret_cast<uint8_t*>(legacyTitle), sizeof(legacyTitle)) != sizeof(legacyTitle)) {
        LOG_ERR("CLIP", "Truncated clipping title at record %u", i);
        clippings.clear();
        file.close();
        return false;
      }
      legacyTitle[sizeof(legacyTitle) - 1] = '\0';
      clipping.chapterTitle = legacyTitle;
    } else if (!readStringChecked(file, clipping.chapterTitle) ||
               clipping.chapterTitle.size() > CLIPPING_CHAPTER_TITLE_MAX) {
      LOG_ERR("CLIP", "Invalid clipping title at record %u", i);
      clippings.clear();
      file.close();
      return false;
    }
    if (!readStringChecked(file, clipping.text)) {
      LOG_ERR("CLIP", "Truncated clipping text at record %u", i);
      clippings.clear();
      file.close();
      return false;
    }
    if (clipping.text.size() > CLIPPING_TEXT_MAX) clipping.text.resize(CLIPPING_TEXT_MAX);
    clippings.push_back(std::move(clipping));
  }
  file.close();
  return true;
}

bool ClippingStore::writeToFile() const {
  if (storeFilePath.empty()) return false;
  Storage.mkdir("/.crosspoint");
  Storage.mkdir(CLIPPINGS_DIR);

  const std::string tempPath = storeFilePath + ".tmp";
  const std::string backupPath = storeFilePath + ".bak";
  if (Storage.exists(tempPath.c_str())) Storage.remove(tempPath.c_str());

  FsFile file = Storage.open(tempPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) {
    LOG_ERR("CLIP", "Unable to write %s", tempPath.c_str());
    return false;
  }

  const uint16_t count = static_cast<uint16_t>(clippings.size());
  bool ok = writePodChecked(file, FILE_VERSION) && writePodChecked(file, count) &&
            writeStringChecked(file, bookTitle) && writeStringChecked(file, bookAuthor) &&
            writeStringChecked(file, bookFilePath);
  for (const Clipping& clipping : clippings) {
    const uint8_t highlightStyle = static_cast<uint8_t>(clipping.highlightStyle);
    ok = ok && writePodChecked(file, clipping.spineIndex) && writePodChecked(file, clipping.startPage) &&
         writePodChecked(file, clipping.endPage) && writePodChecked(file, clipping.pageCount) &&
         writePodChecked(file, clipping.startWordIndex) && writePodChecked(file, clipping.endWordIndex) &&
         writePodChecked(file, clipping.wordCount) && writePodChecked(file, clipping.paragraphIndex) &&
         writePodChecked(file, clipping.timestamp) && writePodChecked(file, highlightStyle) &&
         writeStringChecked(file, clipping.chapterTitle) && writeStringChecked(file, clipping.text);
    if (!ok) break;
  }
  if (ok) file.flush();
  ok = file.close() && ok;
  if (!ok) {
    Storage.remove(tempPath.c_str());
    LOG_ERR("CLIP", "Failed while writing %s", tempPath.c_str());
    return false;
  }

  const bool hadLiveFile = Storage.exists(storeFilePath.c_str());
  if (hadLiveFile) {
    if (Storage.exists(backupPath.c_str())) Storage.remove(backupPath.c_str());
    if (!Storage.rename(storeFilePath.c_str(), backupPath.c_str())) {
      Storage.remove(tempPath.c_str());
      LOG_ERR("CLIP", "Unable to preserve previous clipping file: %s", storeFilePath.c_str());
      return false;
    }
  }

  if (!Storage.rename(tempPath.c_str(), storeFilePath.c_str())) {
    if (hadLiveFile) Storage.rename(backupPath.c_str(), storeFilePath.c_str());
    Storage.remove(tempPath.c_str());
    LOG_ERR("CLIP", "Unable to install new clipping file: %s", storeFilePath.c_str());
    return false;
  }
  if (hadLiveFile) Storage.remove(backupPath.c_str());
  return true;
}
