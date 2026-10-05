#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

inline constexpr size_t CLIPPING_CHAPTER_TITLE_MAX = 256;
inline constexpr size_t CLIPPING_TEXT_MAX = 512;
inline constexpr size_t CLIPPING_PREVIEW_ANCHOR_MAX = 191;
inline constexpr uint16_t CLIPPING_MAX_PER_BOOK = 64;

enum class ClippingHighlightStyle : uint8_t {
  Marker = 0,
  Underline = 1,
  BlackMarker = 2,
};

struct Clipping {
  uint16_t spineIndex = 0;
  uint16_t startPage = 0;
  uint16_t endPage = 0;
  uint16_t pageCount = 1;
  uint16_t startWordIndex = 0;
  uint16_t endWordIndex = 0;
  uint16_t wordCount = 0;
  uint16_t paragraphIndex = UINT16_MAX;
  uint32_t timestamp = 0;
  ClippingHighlightStyle highlightStyle = ClippingHighlightStyle::Marker;
  std::string chapterTitle;
  std::string text;
  // Empty for ordinary chapter pages; preview pages are relative to this anchor.
  std::string previewAnchor;
  bool fullNote = false;
};

class ClippingStore {
 public:
  enum class AddResult : uint8_t { Added, LimitReached, SaveFailed };

  // Preserve old page/word coordinates before an FB2 pagination migration.
  static bool archiveForBook(const std::string& filePath, const std::string& backupDirectory);

  bool loadForBook(const std::string& filePath, const std::string& title, const std::string& author);
  void unload();

  AddResult addClipping(uint16_t spineIndex, uint16_t startPage, uint16_t endPage, uint16_t pageCount,
                        uint16_t startWordIndex, uint16_t endWordIndex, uint16_t wordCount, const char* chapterTitle,
                        uint16_t paragraphIndex, const std::string& text, ClippingHighlightStyle highlightStyle,
                        const std::string& previewAnchor = {}, bool fullNote = false);
  bool removeAt(size_t index);
  void clearAll();

  [[nodiscard]] bool empty() const { return clippings.empty(); }
  [[nodiscard]] const std::vector<Clipping>& getAll() const { return clippings; }

 private:
  std::vector<Clipping> clippings;
  std::string bookFilePath;
  std::string bookTitle;
  std::string bookAuthor;
  std::string storeFilePath;

  bool readFromFile();
  bool writeToFile() const;
};
