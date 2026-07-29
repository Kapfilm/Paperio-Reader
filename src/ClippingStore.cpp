#include "ClippingStore.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
constexpr uint8_t FILE_VERSION = 1;
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
  return writePodChecked(file, length) &&
         (length == 0 ||
          file.write(reinterpret_cast<const uint8_t*>(value.data()), length) == static_cast<size_t>(length));
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

ClippingStore::AddResult ClippingStore::addClipping(
    const uint16_t spineIndex, const uint16_t startPage, const uint16_t endPage, const uint16_t pageCount,
    const uint16_t startWordIndex, const uint16_t endWordIndex, const uint16_t wordCount, const char* chapterTitle,
    const uint16_t paragraphIndex, const std::string& text) {
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
  snprintf(clipping.chapterTitle, sizeof(clipping.chapterTitle), "%s", chapterTitle ? chapterTitle : "");
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
  if (!readPodChecked(file, version) || version != FILE_VERSION || !readPodChecked(file, count) ||
      count > CLIPPING_MAX_PER_BOOK || !readStringChecked(file, storedTitle) ||
      !readStringChecked(file, storedAuthor) || !readStringChecked(file, storedPath)) {
    LOG_ERR("CLIP", "Invalid clipping file: %s", storeFilePath.c_str());
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
        !readPodChecked(file, clipping.timestamp) ||
        file.read(reinterpret_cast<uint8_t*>(clipping.chapterTitle), sizeof(clipping.chapterTitle)) !=
            sizeof(clipping.chapterTitle) ||
        !readStringChecked(file, clipping.text)) {
      LOG_ERR("CLIP", "Truncated clipping file at record %u", i);
      clippings.clear();
      file.close();
      return false;
    }
    clipping.chapterTitle[sizeof(clipping.chapterTitle) - 1] = '\0';
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

  FsFile file = Storage.open(storeFilePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) {
    LOG_ERR("CLIP", "Unable to write %s", storeFilePath.c_str());
    return false;
  }

  const uint16_t count = static_cast<uint16_t>(clippings.size());
  bool ok = writePodChecked(file, FILE_VERSION) && writePodChecked(file, count) &&
            writeStringChecked(file, bookTitle) && writeStringChecked(file, bookAuthor) &&
            writeStringChecked(file, bookFilePath);
  for (const Clipping& clipping : clippings) {
    ok = ok && writePodChecked(file, clipping.spineIndex) && writePodChecked(file, clipping.startPage) &&
         writePodChecked(file, clipping.endPage) && writePodChecked(file, clipping.pageCount) &&
         writePodChecked(file, clipping.startWordIndex) && writePodChecked(file, clipping.endWordIndex) &&
         writePodChecked(file, clipping.wordCount) && writePodChecked(file, clipping.paragraphIndex) &&
         writePodChecked(file, clipping.timestamp) &&
         file.write(reinterpret_cast<const uint8_t*>(clipping.chapterTitle), sizeof(clipping.chapterTitle)) ==
             sizeof(clipping.chapterTitle) &&
         writeStringChecked(file, clipping.text);
    if (!ok) break;
  }
  if (ok) file.flush();
  file.close();
  if (!ok) LOG_ERR("CLIP", "Failed while writing %s", storeFilePath.c_str());
  return ok;
}
