#include "BlockStreamReader.h"

#include <utility>

#include "BlockSerialization.h"
#include "Serialization.h"

namespace compiled {
namespace {

using serialization::readPod;

// Merge a continuation record's words/text/footnotes/previews into the logical block being built,
// rebasing textOff and word-indexed side data by the current word base. Mirrors the coalescing the
// two-pass harness did per spine, but here it is incremental (one logical block at a time).
void mergeContinuation(Block& into, const Block& cont) {
  const uint32_t wordBase = static_cast<uint32_t>(into.words.size());
  const uint32_t textBase = static_cast<uint32_t>(into.text.size());
  for (Word w : cont.words) {
    w.textOff += textBase;
    into.words.push_back(w);
  }
  into.text += cont.text;
  for (FootnoteRef fn : cont.footnotes) {
    fn.wordIndex += wordBase;
    into.footnotes.push_back(fn);
  }
  for (PreviewRun pr : cont.footnotePreviews) {
    pr.startWord += wordBase;
    into.footnotePreviews.push_back(pr);
  }
}

}  // namespace

bool BlockStreamReader::open(FsFile& file) {
  file_ = &file;
  ok_ = false;
  stylePool_.clear();
  spineOffsets_.clear();
  if (!file) return false;

  if (!file.seekSet(0)) return false;
  char magic[4] = {};
  if (file.read(magic, 4) != 4) return false;
  for (int i = 0; i < 4; ++i) {
    if (magic[i] != kMagic[i]) return false;
  }
  uint8_t version = 0;
  readPod(file, version);
  if (version != kVersion) return false;
  readPod(file, fingerprint_);
  uint32_t spineCount = 0;
  readPod(file, spineCount);
  readPod(file, stylePoolOffset_);
  readPod(file, spineIndexOffset_);
  readPod(file, chaptersOffset_);

  const uint32_t fileSize = static_cast<uint32_t>(file.fileSize());
  // A placeholder header whose offsets were never back-patched (interrupted write) → treat as stale.
  if (stylePoolOffset_ == 0 || spineIndexOffset_ == 0 || chaptersOffset_ == 0) return false;
  if (stylePoolOffset_ > fileSize || spineIndexOffset_ > fileSize || chaptersOffset_ > fileSize) return false;

  // Load the (small) style pool.
  if (!file.seekSet(stylePoolOffset_)) return false;
  if (!readStylePool(file, stylePool_)) return false;

  // Load the spine-offset index.
  if (!file.seekSet(spineIndexOffset_)) return false;
  uint32_t indexCount = 0;
  readPod(file, indexCount);
  if (indexCount != spineCount) return false;  // header/index disagree → corrupt
  spineOffsets_.resize(indexCount);
  for (uint32_t i = 0; i < indexCount; ++i) {
    readPod(file, spineOffsets_[i]);
    if (spineOffsets_[i] > fileSize) return false;  // offset past EOF → corrupt
  }

  ok_ = static_cast<bool>(file);
  return ok_;
}

bool BlockStreamReader::readChapters(std::vector<Chapter>& out) {
  if (!ok_ || !file_) return false;
  if (!file_->seekSet(chaptersOffset_)) return false;
  return compiled::readChapters(*file_, out);
}

bool BlockStreamReader::openSpine(uint32_t i) {
  if (!ok_ || !file_ || i >= spineOffsets_.size()) return false;
  if (!file_->seekSet(spineOffsets_[i])) return false;
  readPod(*file_, spineFirstCharOffset_);
  readPod(*file_, spineBlocksRemaining_);
  haveLookahead_ = false;
  return static_cast<bool>(*file_);
}

bool BlockStreamReader::readOneRecord(Block& rec) {
  if (spineBlocksRemaining_ == 0) return false;
  rec = Block{};
  if (!readBlock(*file_, rec)) {
    ok_ = false;
    return false;
  }
  --spineBlocksRemaining_;
  return true;
}

bool BlockStreamReader::nextLogicalBlock(Block& out) {
  if (!ok_ || !file_) return false;

  // Take the base: either the lookahead from a prior call, or the next on-disk record.
  if (haveLookahead_) {
    out = std::move(lookahead_);
    haveLookahead_ = false;
  } else if (!readOneRecord(out)) {
    return false;  // spine end
  }

  // Absorb following kContinuation records into `out` until the next record is a fresh block (which
  // becomes the lookahead) or the spine ends. Bounded to ONE logical block.
  while (spineBlocksRemaining_ > 0) {
    Block next;
    if (!readOneRecord(next)) return false;  // read error mid-spine
    if ((next.flags & kContinuation) != 0 && next.type == BlockType::Text) {
      mergeContinuation(out, next);
    } else {
      lookahead_ = std::move(next);
      haveLookahead_ = true;
      break;
    }
  }
  return true;
}

bool BlockStreamReader::readSpineAux(std::vector<Anchor>& anchors, std::vector<PageBreakLabel>& labels) {
  if (!ok_ || !file_) return false;
  // Must be called after the block stream is fully consumed (cursor sits at the aux tables). A
  // pending lookahead would mean blocks remain — refuse.
  if (haveLookahead_ || spineBlocksRemaining_ != 0) return false;
  if (!readAnchors(*file_, anchors)) return false;
  return readLabels(*file_, labels);
}

}  // namespace compiled
