#pragma once
// BlockStreamReader — the STREAMING Stage-2 source (plan v2). Reads a v5 content.bin one LOGICAL
// block at a time, seeking per spine, without ever holding a whole spine (let alone the book) in
// RAM. This is the counterpart to ContentBinWriter and the piece that lets Stage-2 lay out and
// stream pages to disk while resident RAM stays ~one block. See
// docs/stage1-revised-plan-v2-2026-07-26.md.
//
// Only the small, book-level tables are loaded up front: the style pool, the spine-offset index,
// and (optionally) the chapters. Everything else streams.
//
// Usage:
//   FsFile f; Storage.openFileForRead(...);   // host tests: HalFile::fromString(bytes)
//   BlockStreamReader r;
//   if (!r.open(f)) { stale/corrupt -> recompile }
//   if (r.fingerprint() != bookFingerprint) { stale -> recompile }
//   for spine i in [0, r.spineCount()):
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

class BlockStreamReader {
 public:
  BlockStreamReader() = default;

  // Read + validate the header, then load the style pool + spine index. Returns false on a bad
  // magic/version, a truncated file, or an out-of-range offset (caller treats as stale → recompile).
  // `file` is caller-owned and must outlive the reader.
  bool open(FsFile& file);

  uint64_t fingerprint() const { return fingerprint_; }
  uint32_t spineCount() const { return static_cast<uint32_t>(spineOffsets_.size()); }
  const std::vector<CssStyle>& stylePool() const { return stylePool_; }

  // Load the (small, book-level) chapter table on demand.
  bool readChapters(std::vector<Chapter>& out);

  // Position at spine i's block stream. Reads the spine header (firstCharOffset + blockCount).
  // Returns false on I/O error or i out of range.
  bool openSpine(uint32_t i);
  uint32_t spineFirstCharOffset() const { return spineFirstCharOffset_; }

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
  uint32_t stylePoolOffset_ = 0;
  uint32_t spineIndexOffset_ = 0;
  uint32_t chaptersOffset_ = 0;
  std::vector<CssStyle> stylePool_;
  std::vector<uint32_t> spineOffsets_;

  // Current spine cursor.
  uint32_t spineBlocksRemaining_ = 0;  // on-disk records left in the current spine
  uint32_t spineFirstCharOffset_ = 0;

  // One-record lookahead so nextLogicalBlock can peek whether the following record is a
  // continuation of the base it just read.
  bool haveLookahead_ = false;
  Block lookahead_;
};

}  // namespace compiled
