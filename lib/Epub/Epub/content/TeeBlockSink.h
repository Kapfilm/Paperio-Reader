#pragma once
// TeeBlockSink — fan a single walk's BlockSink stream out to TWO sinks (Increment F). The device
// parse-and-display-on-a-content.bin-miss path drives BOTH the LayoutSink (Stage-2 → the transient
// current-spine page store, so the first page shows now) AND a ContentBinWriter (Stage-1 → content.bin,
// so the spine lands in the compiled book for next visit / relayout) from ONE walk — no second parse.
// See docs/stage1-incr-F-content-bin-primary-design-2026-07-27.md.
//
// onBlock moves its Block into the sink, so the tee COPIES the block for the first sink and MOVES the
// original into the second (one bounded Block copy per block; peak added RAM ~one block). Every other
// call (onAnchor/onChapter/onPageBreakLabel/onFootnote/onXPathAdvance/onSpineEnd) takes const-ref /
// scalar args and forwards trivially to both. Both sinks are non-owning; they must outlive the tee.
//
// Order: `first` receives the copy, `second` receives the move. The section-tail getters read from the
// LayoutSink, so pass the LayoutSink as one of the two and read its tables after the walk as today.

#include "BlockSink.h"

namespace compiled {

class TeeBlockSink : public BlockSink {
 public:
  TeeBlockSink(BlockSink* first, BlockSink* second) : first_(first), second_(second) {}

  void onBlock(Block&& block, const CssStyle& style) override {
    if (first_) {
      Block copy = block;  // deep copy (words/text/footnotes/previews/rows) for the first sink
      first_->onBlock(std::move(copy), style);
    }
    if (second_) second_->onBlock(std::move(block), style);
  }
  void onAnchor(const std::string& id) override {
    if (first_) first_->onAnchor(id);
    if (second_) second_->onAnchor(id);
  }
  void onChapter(uint8_t level, const std::string& title) override {
    if (first_) first_->onChapter(level, title);
    if (second_) second_->onChapter(level, title);
  }
  void onPageBreakLabel(const std::string& label) override {
    if (first_) first_->onPageBreakLabel(label);
    if (second_) second_->onPageBreakLabel(label);
  }
  void onFootnote(int wordIndex, const FootnoteEntry& entry) override {
    if (first_) first_->onFootnote(wordIndex, entry);
    if (second_) second_->onFootnote(wordIndex, entry);
  }
  void onXPathAdvance(uint16_t paragraphIndex, uint16_t listItemIndex, uint32_t bodyChildByteOffset) override {
    if (first_) first_->onXPathAdvance(paragraphIndex, listItemIndex, bodyChildByteOffset);
    if (second_) second_->onXPathAdvance(paragraphIndex, listItemIndex, bodyChildByteOffset);
  }
  void onSpineEnd() override {
    if (first_) first_->onSpineEnd();
    if (second_) second_->onSpineEnd();
  }

 private:
  BlockSink* first_ = nullptr;   // receives the Block COPY
  BlockSink* second_ = nullptr;  // receives the Block MOVE
};

}  // namespace compiled
