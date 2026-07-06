#pragma once
#include <EpdFontFamily.h>
#include <HalStorage.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "Block.h"
#include "BlockStyle.h"

// Represents a line of text on a page
class TextBlock final : public Block {
 private:
  std::vector<std::string> words;
  std::vector<int16_t> wordXpos;
  std::vector<EpdFontFamily::Style> wordStyles;
  // Per-word font size, percent of the block font size (inline CSS font-size).
  // Empty means every word is at 100% — the overwhelmingly common case, kept empty
  // so ordinary lines pay no RAM or section-cache cost. Non-empty vectors always
  // match words.size().
  std::vector<uint8_t> wordSizes;
  BlockStyle blockStyle;

  // Effective render scale of words[i]: block multiplier × the word's size percent.
  float wordScale(const size_t i) const {
    return wordSizes.empty() ? blockStyle.fontSizeMultiplier : blockStyle.fontSizeMultiplier * (wordSizes[i] / 100.0f);
  }

 public:
  explicit TextBlock(std::vector<std::string> words, std::vector<int16_t> word_xpos,
                     std::vector<EpdFontFamily::Style> word_styles, const BlockStyle& blockStyle = BlockStyle(),
                     std::vector<uint8_t> word_sizes = {})
      : words(std::move(words)),
        wordXpos(std::move(word_xpos)),
        wordStyles(std::move(word_styles)),
        wordSizes(std::move(word_sizes)),
        blockStyle(blockStyle) {
    // Normalize the all-100% case to an empty vector so uniform lines keep the
    // zero-cost fast paths in render/serialize/line-height.
    if (std::all_of(wordSizes.begin(), wordSizes.end(), [](const uint8_t s) { return s == 100; })) {
      wordSizes.clear();
      wordSizes.shrink_to_fit();
    }
  }
  ~TextBlock() override = default;
  void setBlockStyle(const BlockStyle& blockStyle) { this->blockStyle = blockStyle; }
  const BlockStyle& getBlockStyle() const { return blockStyle; }
  const std::vector<std::string>& getWords() const { return words; }
  bool isEmpty() override { return words.empty(); }
  size_t wordCount() const { return words.size(); }
  bool hasWordSizes() const { return !wordSizes.empty(); }
  const std::vector<uint8_t>& getWordSizes() const { return wordSizes; }
  // Largest per-word size percent on this line (100 when uniform). The line's
  // vertical advance scales with this so an enlarged inline word doesn't collide
  // with the next line.
  uint8_t maxSizePct() const {
    return wordSizes.empty() ? static_cast<uint8_t>(100) : *std::max_element(wordSizes.begin(), wordSizes.end());
  }
  // given a renderer works out where to break the words into lines
  void render(const GfxRenderer& renderer, int fontId, int x, int y) const;
  BlockType getType() override { return TEXT_BLOCK; }
  bool serialize(FsFile& file) const;
  static std::unique_ptr<TextBlock> deserialize(FsFile& file);
};
