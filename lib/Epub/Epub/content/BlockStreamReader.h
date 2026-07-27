#pragma once
// BlockStreamReader — the STREAMING Stage-2 source (plan v2). Reads a v5 content.bin one LOGICAL
// block at a time, seeking per spine, without ever holding a whole spine (let alone the book) in
// RAM. This is the counterpart to ContentBinWriter and the piece that lets Stage-2 lay out and
// stream pages to disk while resident RAM stays ~one block. See
// docs/stage1-revised-plan-v2-2026-07-26.md.
//
// v6 (Increment E): only the small, fixed spine-offset index is loaded up front (it sits right after
// the header). Each spine is SELF-CONTAINED — its aux region carries the spine's own style table and
// chapter entries alongside its anchors/labels — so openSpine() loads them per spine and nothing is
// book-global. A spine whose index slot is 0 is NOT YET AVAILABLE (still being written / never
// compiled): spineAvailable(i) reports this so a consumer can chase a producer's write frontier.
//
// Usage:
//   FsFile f; Storage.openFileForRead(...);   // host tests: HalFile::fromString(bytes)
//   BlockStreamReader r;
//   if (!r.open(f)) { stale/corrupt -> recompile }
//   if (r.fingerprint() != bookFingerprint) { stale -> recompile }
//   for spine i in [0, r.spineCount()):
//     if (!r.spineAvailable(i)) continue;   // frontier: skip spines not yet committed
//     r.openSpine(i);
//     Block b;
//     while (r.nextLogicalBlock(b)) { ...lay out b, stream page, drop b... }
//
// nextLogicalBlock reassembles 8 KB kContinuation splits into one logical block incrementally
// (bounded to one logical block), so the caller sees the same block stream the walk produced.

#include <cstdint>
#include <string>
#include <vector>

#include <HalStorage.h>  // FsFile

#include "CompiledContent.h"

namespace compiled {

class LayoutSink;  // fwd — replaySpine drives one; defined in LayoutSink.h

class BlockStreamReader {
 public:
  BlockStreamReader() = default;

  // Read + validate the header, then load the fixed spine-offset index (right after the header).
  // Returns false on a bad magic/version, a truncated file, or an out-of-range offset (caller treats
  // as stale → recompile). `file` is caller-owned and must outlive the reader. A partially-written
  // file (some index slots still 0) opens fine; use spineAvailable() per spine.
  bool open(FsFile& file);

  uint64_t fingerprint() const { return fingerprint_; }
  uint32_t spineCount() const { return static_cast<uint32_t>(spineOffsets_.size()); }
  // The CURRENT spine's local style table (v6), loaded by openSpine. Empty until openSpine succeeds.
  const std::vector<CssStyle>& spineStylePool() const { return spineStylePool_; }
  // True if spine i's index slot is committed (offset != 0) — i.e. the spine is fully written and
  // replayable. A producer commits the slot only after the whole spine (incl. its aux) is on disk.
  bool spineAvailable(uint32_t i) const { return i < spineOffsets_.size() && spineOffsets_[i] != 0; }

  // The CURRENT spine's chapter entries (v6: per-spine), loaded by openSpine. Empty until openSpine.
  const std::vector<Chapter>& spineChapters() const { return spineChapters_; }

  // Position at spine i's block stream. Reads the spine header (firstCharOffset + blockCount +
  // auxOffset) AND pre-loads the spine's SELF-CONTAINED aux region from auxOffset (anchors + labels +
  // style table + chapters), so spineAnchors()/spineLabels()/spineStylePool()/spineChapters() are
  // available BEFORE the block stream. Returns false on I/O error, i out of range, or i not yet
  // available (slot 0).
  bool openSpine(uint32_t i);
  uint32_t spineFirstCharOffset() const { return spineFirstCharOffset_; }

  // NOTE (v6): the book-level readChapters() is gone — chapters are per-spine (spineChapters()),
  // loaded by openSpine, so a self-contained spine replays without any book-global table.

  // The current spine's anchors/labels (loaded up front by openSpine). Their blockIndex is a RECORD
  // index; nextLogicalBlock() reports the first-record index of each logical block via
  // currentFirstRecordIndex() so callers can cross-reference.
  const std::vector<Anchor>& spineAnchors() const { return spineAnchors_; }
  const std::vector<PageBreakLabel>& spineLabels() const { return spineLabels_; }

  // Record index (0-based within the spine) of the FIRST record of the logical block most recently
  // returned by nextLogicalBlock(). Anchors/labels/chapters are keyed on this.
  uint32_t currentFirstRecordIndex() const { return currentFirstRecordIndex_; }

  // Read the next LOGICAL block of the current spine into `out` (merging any kContinuation records).
  // Returns true and fills `out` when a block was read; false at spine end or on error (check
  // ok()). The caller owns `out` and may move from it.
  bool nextLogicalBlock(Block& out);

  // Read the next RAW on-disk record (no kContinuation merge). Used by the whole-book reader, which
  // stores the split records as-is. Returns false at spine end. Do not mix with nextLogicalBlock.
  bool nextRawRecord(Block& out) { return readOneRecord(out); }

  // Read the current spine's anchor + label tables (positioned after the last block). Call ONLY
  // after nextLogicalBlock has returned false (spine fully consumed), which leaves the cursor at the
  // aux tables. Bounded (per-spine small).
  bool readSpineAux(std::vector<Anchor>& anchors, std::vector<PageBreakLabel>& labels);

  bool ok() const { return ok_; }

 private:
  bool readOneRecord(Block& rec);  // one on-disk record (base or continuation)

  FsFile* file_ = nullptr;
  bool ok_ = false;
  uint64_t fingerprint_ = 0;
  std::vector<uint32_t> spineOffsets_;  // fixed index right after the header; 0 slot = not available

  // Current spine cursor (v6: style pool + chapters are per-spine, loaded by openSpine).
  uint32_t spineBlockCount_ = 0;       // total on-disk records in the current spine
  uint32_t spineBlocksRemaining_ = 0;  // on-disk records left to read
  uint32_t spineFirstCharOffset_ = 0;
  uint32_t currentFirstRecordIndex_ = 0;  // first-record index of the last logical block returned
  std::vector<Anchor> spineAnchors_;
  std::vector<PageBreakLabel> spineLabels_;
  std::vector<CssStyle> spineStylePool_;   // this spine's local style table
  std::vector<Chapter> spineChapters_;     // this spine's chapter entries

  // One-record lookahead so nextLogicalBlock can peek whether the following record is a
  // continuation of the base it just read.
  bool haveLookahead_ = false;
  Block lookahead_;
};

// Replay spine `spineIndex` of an OPEN BlockStreamReader through `sink`, reproducing the walk's
// BlockSink call order (anchors/labels/footnotes/xpath before onBlock, chapters after) so the
// LayoutSink produces the same pages a live parse would. Streams one logical block at a time — no
// spine materialized. v6: chapters + styles are the spine's OWN (loaded by openSpine), so the caller
// passes nothing book-global. Returns false on any read error (check reader.ok()). Shared by the host
// harness (replayFromContentBin) and the device Section read-back build so they never drift.
bool replaySpine(BlockStreamReader& reader, uint32_t spineIndex, LayoutSink& sink);

}  // namespace compiled
