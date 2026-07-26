#pragma once
// ContentBinWriter — the STREAMING Stage-1 persistence sink (plan v2). Unlike the whole-book
// ContentSink (which accumulates the entire CompiledContent in RAM before writing — a multi-MB
// regression on a big-single-spine book), this appends each block to content.bin AS THE WALK
// EMITS IT and drops it. Peak added RAM ≈ one block + the (small) style pool + the spine-offset
// index. See docs/stage1-revised-plan-v2-2026-07-26.md.
//
// It is a BlockSink: attach it to the walk (Section::setStage1Sink) exactly like ContentSink, and
// the walk drives it block-by-block. The 8 KB split (appendTextSplit) still applies per block.
//
// Usage (the caller owns the FsFile — opened read/write + seekable, so open()/finish() can
// back-patch the header):
//   FsFile f; Storage.openFileForWrite("SCT", path, f);   // device; host tests: HalFile::forReadWrite()
//   ContentBinWriter w;
//   w.begin(f, spineCount, fingerprint);     // writes a placeholder header
//   for each spine i in [0, spineCount):
//     w.beginSpine();                        // (also happens lazily on the first onBlock)
//     ... walk drives onBlock/onAnchor/onChapter/onFootnote/onPageBreakLabel/onXPathAdvance ...
//     w.onSpineEnd();                        // flushes the spine's aux tables + records its offset
//   w.finish();                              // appends style pool + spine index + chapters,
//                                            // back-patches the header. Caller closes the file.
//
// No device has v4 content.bin, so there is NO migration path: v5 is the only format. A book can
// later be written spine-by-spine across build sessions (per-spine-on-first-visit); this class
// writes a whole book in one begin/finish for now (the host gate + first device test).

#include <cstdint>
#include <string>
#include <vector>

#include <HalStorage.h>  // FsFile

#include "BlockSink.h"
#include "CompiledContent.h"

struct FootnoteEntry;

namespace compiled {

class ContentBinWriter : public BlockSink {
 public:
  ContentBinWriter() = default;
  ~ContentBinWriter() override = default;

  // Begin writing to `file` (caller-owned, opened read/write + seekable) for a book of `spineCount`
  // spines. Writes a placeholder v5 header (offsets 0). Returns false on I/O error. fingerprint is
  // the source book's ZIP content fingerprint.
  bool begin(FsFile& file, uint32_t spineCount, uint64_t fingerprint);

  // BlockSink — driven by the walk. onBlock serializes the block immediately and drops it.
  void onBlock(Block&& block, const CssStyle& style) override;
  void onAnchor(const std::string& id) override;
  void onChapter(uint8_t level, const std::string& title) override;
  void onPageBreakLabel(const std::string& label) override;
  void onFootnote(int wordIndex, const FootnoteEntry& entry) override;
  void onXPathAdvance(uint16_t paragraphIndex, uint16_t listItemIndex, uint32_t bodyChildByteOffset) override;
  void onSpineEnd() override;

  // Open a new spine explicitly (optional — the first onBlock of a spine opens one lazily).
  void beginSpine();

  // Append the style pool + spine-offset index + chapters, back-patch the header, close. Returns
  // false on any I/O error. After finish(), the writer is done (reopen for another book).
  bool finish();

  bool ok() const { return ok_; }

 private:
  bool flushBlock(Block&& block);  // apply 8 KB split, write each record, count blocks
  bool writeOneRecord(const Block& rec);

  FsFile* file_ = nullptr;  // caller-owned
  bool ok_ = false;
  uint32_t spineCount_ = 0;
  uint64_t fingerprint_ = 0;

  std::vector<CssStyle> stylePool_;     // interned styles (small; written at finish)
  std::vector<uint32_t> spineOffsets_;  // file offset of each spine's start (for the index)
  std::vector<Chapter> chapters_;       // book-level; small; written at finish

  // Current spine state (all small; RESET at each spine — never a whole spine of blocks).
  bool spineOpen_ = false;
  bool spineHasBlock_ = false;
  uint32_t spineStartOffset_ = 0;   // where this spine's [firstCharOffset|blockCount] header sits
  uint32_t spineFirstCharOffset_ = 0;
  uint32_t blockCount_ = 0;         // running count; back-patched into the spine header at onSpineEnd
  std::vector<Anchor> anchors_;             // this spine only
  std::vector<PageBreakLabel> pageBreakLabels_;  // this spine only

  // Footnotes/xpath arrive DURING a block's build (before its onBlock). Buffered, attached at onBlock.
  std::vector<FootnoteRef> pendingFootnotes_;
  bool pendingXPath_ = false;
  XPathCounters pendingXPathCounters_;
};

}  // namespace compiled
