#include "ContentBinProducer.h"

#include <algorithm>

#include <HalStorage.h>
#include <Logging.h>

namespace compiled {

ContentBinProducer::~ContentBinProducer() { finish(); }

bool ContentBinProducer::begin(const std::shared_ptr<Epub>& epub, GfxRenderer& renderer,
                               const Section::BuildParams& params) {
  epub_ = epub;
  renderer_ = &renderer;
  params_ = params;
  queue_.clear();
  queueHead_ = 0;
  committedCount_ = 0;
  building_.reset();
  open_ = false;

  const int spineCount = epub_->getSpineItemsCount();
  if (spineCount <= 0) return false;
  committed_.assign(static_cast<size_t>(spineCount), false);

  const std::string binPath = epub_->getCachePath() + "/content.bin";
  { const auto dir = epub_->getCachePath(); Storage.mkdir(dir.c_str()); }

  uint64_t fingerprint = 0;
  epub_->zipContentFingerprint(&fingerprint);

  // First cut: always start a FRESH content.bin (header + zeroed index). Resuming an interrupted
  // earlier compile (reading back the committed slots and skipping them) is a documented refinement —
  // it needs a read+append handle; today's producer rebuilds the whole book's queue. A stale file is
  // harmless: it is truncated by openFileForWrite below.
  if (!Storage.openFileForWrite("SCT", binPath, binFile_)) {
    LOG_ERR("CBP", "ContentBinProducer: cannot open %s for write", binPath.c_str());
    return false;
  }
  if (!writer_.begin(binFile_, static_cast<uint32_t>(spineCount), fingerprint)) {
    binFile_.close();
    Storage.remove(binPath.c_str());
    return false;
  }
  open_ = true;

  // Default queue: every spine ascending. setReadPosition re-heads it toward the reader.
  queue_.reserve(static_cast<size_t>(spineCount));
  for (int i = 0; i < spineCount; ++i) queue_.push_back(static_cast<uint32_t>(i));
  return true;
}

void ContentBinProducer::setReadPosition(uint32_t spineIndex, uint32_t windowAhead) {
  if (!open_) return;
  const uint32_t spineCount = static_cast<uint32_t>(committed_.size());
  if (spineIndex >= spineCount) return;

  // Rebuild the pending queue (everything not yet committed and not the in-flight spine) in priority
  // order: the window [spineIndex, spineIndex+windowAhead) first, then the remaining spines ascending.
  // The in-flight spine (building_) is left running — re-heading never interrupts a spine mid-build
  // (consumer-priority arbitration happens in the reader integration, not here).
  std::vector<uint32_t> reordered;
  reordered.reserve(spineCount);
  const auto stillPending = [&](uint32_t s) {
    return s < spineCount && !committed_[s] && !(building_ && s == buildingSpine_);
  };
  const uint32_t windowEnd = std::min(spineCount, spineIndex + std::max<uint32_t>(1, windowAhead));
  for (uint32_t s = spineIndex; s < windowEnd; ++s)
    if (stillPending(s)) reordered.push_back(s);
  for (uint32_t s = 0; s < spineCount; ++s)
    if (s < spineIndex || s >= windowEnd)
      if (stillPending(s)) reordered.push_back(s);

  queue_ = std::move(reordered);
  queueHead_ = 0;
}

bool ContentBinProducer::startSpine(uint32_t spineIndex) {
  writer_.beginSpineAt(spineIndex);
  building_ = std::make_unique<Section>(epub_, static_cast<int>(spineIndex), *renderer_);
  building_->setStage1Sink(&writer_);  // content-only compile: parser drives the writer, no pages
  buildingSpine_ = spineIndex;
  return true;
}

void ContentBinProducer::abortSpine() {
  // The in-flight Section's dtor aborts its partial build; its index slot was never committed
  // (commit happens only at onSpineEnd), so a 0 slot correctly reads as "not available".
  building_.reset();
}

bool ContentBinProducer::step(uint32_t budgetMs) {
  if (!open_) return false;

  // Pick up the next queued spine if none is in flight, skipping any already committed (a re-head
  // can leave a just-finished spine in an old queue position).
  if (!building_) {
    while (queueHead_ < queue_.size() && committed_[queue_[queueHead_]]) ++queueHead_;
    if (queueHead_ >= queue_.size()) return false;  // drained
    startSpine(queue_[queueHead_]);
  }

  const Section::BuildStep stepResult =
      building_->stepSectionBuild(params_, budgetMs, /*progressFn=*/{}, /*skipEviction=*/true);

  if (stepResult == Section::BuildStep::More) {
    return true;  // same spine continues next call
  }

  if (stepResult == Section::BuildStep::Failed) {
    LOG_ERR("CBP", "ContentBinProducer: spine %lu compile failed; skipping (slot stays 0)",
            static_cast<unsigned long>(buildingSpine_));
    abortSpine();
  } else {  // Done: the walk fired onSpineEnd -> writer committed this spine's index slot.
    committed_[buildingSpine_] = true;
    ++committedCount_;
    building_.reset();
  }
  ++queueHead_;  // advance past this spine's queue slot
  return queueHead_ < queue_.size();
}

bool ContentBinProducer::finish() {
  if (!open_) return true;
  abortSpine();  // drop any in-flight partial (its slot stays 0)
  const bool ok = writer_.finish();
  binFile_.close();
  open_ = false;
  return ok;
}

}  // namespace compiled
