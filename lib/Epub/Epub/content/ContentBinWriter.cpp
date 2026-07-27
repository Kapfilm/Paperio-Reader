#include "ContentBinWriter.h"

#include <utility>

#include "BlockSerialization.h"
#include "Epub/FootnoteEntry.h"
#include "Serialization.h"

namespace compiled {
namespace {

using serialization::writePod;

// Write the v6 fixed header: magic + version + fingerprint + spineCount. The spine-offset index
// (spineCount × u32) follows immediately and is written separately by begin().
void writeHeader(FsFile& f, uint64_t fingerprint, uint32_t spineCount) {
  f.write(reinterpret_cast<const uint8_t*>(kMagic), 4);
  writePod(f, kVersion);
  writePod(f, fingerprint);
  writePod(f, spineCount);
}

}  // namespace

bool ContentBinWriter::begin(FsFile& file, uint32_t spineCount, uint64_t fingerprint) {
  file_ = &file;
  ok_ = static_cast<bool>(file);
  spineCount_ = spineCount;
  fingerprint_ = fingerprint;
  nextSpineIndex_ = 0;
  spineStyles_.clear();
  spineChapters_.clear();
  spineOpen_ = false;
  spineHasBlock_ = false;
  blockCount_ = 0;
  anchors_.clear();
  pageBreakLabels_.clear();
  pendingFootnotes_.clear();
  pendingXPath_ = false;
  if (!ok_) return false;
  // Header, then a pre-allocated spine-offset index of spineCount zeroed slots. onSpineEnd() commits
  // each slot as its spine finishes; a 0 slot = "spine not yet available". File cursor lands right
  // after the index, where the first spine's records begin.
  writeHeader(*file_, fingerprint_, spineCount_);
  for (uint32_t i = 0; i < spineCount_; ++i) writePod(*file_, static_cast<uint32_t>(0));
  ok_ = static_cast<bool>(*file_);
  return ok_;
}

void ContentBinWriter::commitSpineOffset(uint32_t spineIndex, uint32_t offset) {
  if (!ok_ || !file_ || spineIndex >= spineCount_) return;
  const uint32_t here = static_cast<uint32_t>(file_->position());
  if (!file_->seekSet(kHeaderSize + spineIndex * sizeof(uint32_t))) {
    ok_ = false;
    return;
  }
  writePod(*file_, offset);
  file_->seekSet(here);  // resume appending
  ok_ = static_cast<bool>(*file_);
}

void ContentBinWriter::beginSpine() { beginSpineAt(nextSpineIndex_++); }

void ContentBinWriter::beginSpineAt(uint32_t spineIndex) {
  if (!ok_ || !file_) return;
  spineIndexBeingWritten_ = spineIndex;
  spineStartOffset_ = static_cast<uint32_t>(file_->position());
  spineFirstCharOffset_ = 0;
  blockCount_ = 0;
  spineStyles_.clear();
  anchors_.clear();
  pageBreakLabels_.clear();
  spineChapters_.clear();
  spineOpen_ = true;
  spineHasBlock_ = false;
  // Placeholder spine header: firstCharOffset + blockCount + auxOffset, all patched at onSpineEnd
  // once known. auxOffset lets the reader seek straight to the (small) per-spine aux region
  // (anchors + labels + style table + chapters) and load it BEFORE streaming the blocks, so
  // onAnchor/onPageBreakLabel can fire ahead of the block they introduce and styleId resolves
  // against this spine's own pool. Reserve the slots now so blocks follow.
  writePod(*file_, static_cast<uint32_t>(0));  // firstCharOffset
  writePod(*file_, static_cast<uint32_t>(0));  // blockCount
  writePod(*file_, static_cast<uint32_t>(0));  // auxOffset (of the per-spine aux region)
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

  block.styleId = internStyle(spineStyles_, style);  // per-spine local pool (v6)

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
  // The heading block was emitted immediately before this call: point at the last block. v6 stores
  // chapters PER-SPINE (in the spine's aux region); spineIndex is this spine's own index.
  const uint32_t blockIndex = blockCount_ == 0 ? 0 : blockCount_ - 1;
  spineChapters_.push_back(Chapter{static_cast<uint16_t>(spineIndexBeingWritten_), blockIndex, level, title});
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
    // A spine with no blocks at all: still emit an (empty) spine so its index slot commits.
    beginSpine();
  }
  // Write this spine's SELF-CONTAINED aux region after its block stream: anchors + labels + the
  // spine's own style table + the spine's own chapters. Remember where it starts (auxOffset). The
  // reader loads all of it up front in openSpine so styleId/anchors/labels/chapters resolve before
  // the blocks stream. Order here MUST match BlockStreamReader::openSpine's read order.
  const uint32_t auxOffset = static_cast<uint32_t>(file_->position());
  writeAnchors(*file_, anchors_);
  writeLabels(*file_, pageBreakLabels_);
  writeStylePool(*file_, spineStyles_);
  writeChapters(*file_, spineChapters_);
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
  // Commit this spine's start offset into its index slot — the frontier advance. From here a
  // consumer can seek the index, see a non-zero slot, and replay the fully self-contained spine.
  commitSpineOffset(spineIndexBeingWritten_, spineStartOffset_);
  spineOpen_ = false;
  pendingFootnotes_.clear();
  pendingXPath_ = false;
}

bool ContentBinWriter::finish() {
  if (!ok_ || !file_) return false;
  // v6: nothing book-level to append (index committed slot-by-slot; styles + chapters are per-spine).
  // Just make sure any open spine was closed out; a well-formed caller already called onSpineEnd.
  if (spineOpen_) onSpineEnd();
  ok_ = ok_ && static_cast<bool>(*file_);
  return ok_;
}

}  // namespace compiled
