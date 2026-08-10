#pragma once

#include <SaxParser/SaxParser.h>

#include <cstddef>
#include <cstdint>

// Resolves an EPUB anchor to the block element that should begin a targeted
// preview. Many books put an id on an empty inline span in the middle of a
// paragraph; starting at that span drops the beginning of the note.
class PreviewBlockLocator {
 public:
  using IsBlockTagFn = bool (*)(const char* tagName);

  PreviewBlockLocator(const char* anchorId, IsBlockTagFn isBlockTag);

  PreviewBlockLocator(const PreviewBlockLocator&) = delete;
  PreviewBlockLocator& operator=(const PreviewBlockLocator&) = delete;

  bool ok() const { return parser_.isActive(); }
  bool done() const { return startOrdinal_ != 0; }
  bool feed(const uint8_t* data, size_t len);
  bool finalize();
  uint32_t startOrdinal() const { return startOrdinal_; }

 private:
  static constexpr uint8_t MAX_BLOCK_NESTING = 16;

  static void handleStartElement(void* userData, const char* name, const char** atts);
  static void handleEndElement(void* userData, const char* name);

  const char* anchorId_ = nullptr;
  IsBlockTagFn isBlockTag_ = nullptr;
  SaxParser parser_;
  int depth_ = 0;
  uint32_t ordinal_ = 0;
  uint32_t startOrdinal_ = 0;
  uint32_t openBlockOrdinals_[MAX_BLOCK_NESTING] = {};
  int openBlockDepths_[MAX_BLOCK_NESTING] = {};
  uint8_t openBlockCount_ = 0;
};
