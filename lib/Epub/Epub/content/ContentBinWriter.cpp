#include "ContentBinWriter.h"

#include <utility>

#include "BlockSerialization.h"
#include "Epub/FootnoteEntry.h"
#include "Serialization.h"

namespace compiled {
namespace {

using serialization::writePod;

// Write the v5 fixed header. Offsets are 0 in the placeholder (begin) and real at finish().
void writeHeader(FsFile& f, uint64_t fingerprint, uint32_t spineCount, uint32_t stylePoolOffset,
                 uint32_t spineIndexOffset, uint32_t chaptersOffset) {
  f.write(reinterpret_cast<const uint8_t*>(kMagic), 4);
  writePod(f, kVersion);
  writePod(f, fingerprint);
  writePod(f, spineCount);
  writePod(f, stylePoolOffset);
  writePod(f, spineIndexOffset);
  writePod(f, chaptersOffset);
}

}  // namespace

bool ContentBinWriter::begin(FsFile& file, uint32_t spineCount, uint64_t fingerprint) {
  file_ = &file;
  ok_ = static_cast<bool>(file);
  spineCount_ = spineCount;
  fingerprint_ = fingerprint;
  stylePool_.clear();
  spineOffsets_.clear();
  spineOffsets_.reserve(spineCount);
  chapters_.clear();
  spineOpen_ = false;
  spineHasBlock_ = false;
  blockCount_ = 0;
  anchors_.clear();
  pageBreakLabels_.clear();
  pendingFootnotes_.clear();
  pendingXPath_ = false;
  if (!ok_) return false;
  // Placeholder header (offsets patched at finish). File cursor now at kHeaderSize.
  writeHeader(*file_, fingerprint_, spineCount_, 0, 0, 0);
  ok_ = static_cast<bool>(*file_);
  return ok_;
}

void ContentBinWriter::beginSpine() {
  if (!ok_ || !file_) return;
  spineOffsets_.push_back(static_cast<uint32_t>(file_->position()));
  spineStartOffset_ = spineOffsets_.back();
  spineFirstCharOffset_ = 0;
  blockCount_ = 0;
  anchors_.clear();
  pageBreakLabels_.clear();
  spineOpen_ = true;
  spineHasBlock_ = false;
  // Placeholder spine header: firstCharOffset + blockCount + auxOffset, all patched at onSpineEnd
  // once known. auxOffset lets the reader seek straight to the (small) anchors/labels — which are
  // keyed by record index — and load them BEFORE streaming the blocks, so onAnchor/onPageBreakLabel
  // can fire ahead of the block they introduce. Reserve the slots now so blocks follow.
  writePod(*file_, static_cast<uint32_t>(0));  // firstCharOffset
  writePod(*file_, static_cast<uint32_t>(0));  // blockCount
  writePod(*file_, static_cast<uint32_t>(0));  // auxOffset (of the anchors/labels section)
  ok_ = static_cast<bool>(*file_);
}

bool ContentBinWriter::writeOneRecord(const Block& rec) {
  if (!ok_ || !file_) return false;
  if (!writeBlock(*file_, rec)) {
    ok_ = false;
    return false;
  }
  ++blockCount_;
  return true;
}

bool ContentBinWriter::flushBlock(Block&& block) {
  // Apply the 8 KB split, writing each resulting record immediately and dropping it.
  splitTextBlock(std::move(block), [this](Block&& rec) { writeOneRecord(rec); });
  return ok_;
}

void ContentBinWriter::onBlock(Block&& block, const CssStyle& style) {
  if (!ok_) return;
  if (!spineOpen_) beginSpine();

  block.styleId = internStyle(stylePool_, style);

  // Attach footnotes/xpath accumulated during this block's build (before onBlock flushed it).
  block.footnotes = std::move(pendingFootnotes_);
  pendingFootnotes_.clear();
  if (pendingXPath_) {
    block.hasXPath = true;
    block.xpath = pendingXPathCounters_;
    pendingXPath_ = false;
  }

  if (!spineHasBlock_) {
    spineFirstCharOffset_ = block.charOffset;
    spineHasBlock_ = true;
  }

  if (block.type == BlockType::Text) {
    flushBlock(std::move(block));  // may emit multiple continuation records
  } else {
    writeOneRecord(block);
  }
}

void ContentBinWriter::onAnchor(const std::string& id) {
  if (!ok_) return;
  if (!spineOpen_) beginSpine();
  // Block granularity: the anchor introduces the block at the current block count (charOffsetInBlock
  // 0), matching the producer's stage1EmitPendingAnchor model.
  anchors_.push_back(Anchor{id, blockCount_, 0});
}

void ContentBinWriter::onChapter(uint8_t level, const std::string& title) {
  if (!ok_) return;
  if (!spineOpen_) beginSpine();
  // The heading block was emitted immediately before this call: point at the last block.
  const uint32_t blockIndex = blockCount_ == 0 ? 0 : blockCount_ - 1;
  chapters_.push_back(Chapter{static_cast<uint16_t>(spineOffsets_.size() - 1), blockIndex, level, title});
}

void ContentBinWriter::onPageBreakLabel(const std::string& label) {
  if (!ok_) return;
  if (!spineOpen_) beginSpine();
  pageBreakLabels_.push_back(PageBreakLabel{label, blockCount_});
}

void ContentBinWriter::onFootnote(int wordIndex, const FootnoteEntry& entry) {
  if (!ok_) return;
  pendingFootnotes_.push_back({static_cast<uint32_t>(wordIndex), entry});
}

void ContentBinWriter::onXPathAdvance(uint16_t paragraphIndex, uint16_t listItemIndex, uint32_t bodyChildByteOffset) {
  if (!ok_) return;
  pendingXPath_ = true;
  pendingXPathCounters_ = {paragraphIndex, listItemIndex, bodyChildByteOffset};
}

void ContentBinWriter::onSpineEnd() {
  if (!ok_ || !file_) return;
  if (!spineOpen_) {
    // A spine with no blocks at all: still emit an (empty) spine so the index has an entry.
    beginSpine();
  }
  // Write this spine's aux tables after its block stream; remember where they start.
  const uint32_t auxOffset = static_cast<uint32_t>(file_->position());
  writeAnchors(*file_, anchors_);
  writeLabels(*file_, pageBreakLabels_);
  // Back-patch the spine header (firstCharOffset + blockCount + auxOffset).
  const uint32_t here = static_cast<uint32_t>(file_->position());
  if (!file_->seekSet(spineStartOffset_)) {
    ok_ = false;
    return;
  }
  writePod(*file_, spineFirstCharOffset_);
  writePod(*file_, blockCount_);
  writePod(*file_, auxOffset);
  file_->seekSet(here);  // resume appending at end
  ok_ = static_cast<bool>(*file_);
  spineOpen_ = false;
  pendingFootnotes_.clear();
  pendingXPath_ = false;
}

bool ContentBinWriter::finish() {
  if (!ok_ || !file_) return false;
  // Append the book-level sections and record their offsets.
  const uint32_t stylePoolOffset = static_cast<uint32_t>(file_->position());
  writeStylePool(*file_, stylePool_);

  const uint32_t spineIndexOffset = static_cast<uint32_t>(file_->position());
  writePod(*file_, static_cast<uint32_t>(spineOffsets_.size()));
  for (uint32_t off : spineOffsets_) writePod(*file_, off);

  const uint32_t chaptersOffset = static_cast<uint32_t>(file_->position());
  writeChapters(*file_, chapters_);

  // Back-patch the header offsets + the real spine count (== spines actually written).
  if (!file_->seekSet(0)) {
    ok_ = false;
    return false;
  }
  writeHeader(*file_, fingerprint_, static_cast<uint32_t>(spineOffsets_.size()), stylePoolOffset, spineIndexOffset,
              chaptersOffset);
  ok_ = static_cast<bool>(*file_);
  return ok_;
}

}  // namespace compiled
