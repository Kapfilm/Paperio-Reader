#pragma once
// ContentBinProducer — the Increment E background compiler ("producer"). It writes content.bin
// spine-by-spine, sliced and RESUMABLE across ticks, in an order driven by the reader's position, so
// the consumer (Section::buildSectionFromContentBin) can replay each spine the instant it commits
// while later spines are still compiling. See docs/stage1-incr-E-producer-consumer-design-2026-07-26.md.
//
// Contract:
//   ContentBinProducer prod;
//   prod.begin(epub, renderer, params);      // opens <cache>/content.bin, writes header + zeroed index
//   prod.setReadPosition(spineIndex);        // (re)prioritize: compile this spine + a window ahead first
//   while (!prod.done()) prod.step(budgetMs); // one bounded slice per call — drive from idle time
//   prod.finish();                            // flush; caller may destroy
//
// A spine already covered on disk (its index slot committed from an earlier session, fingerprint OK)
// is skipped. The producer compiles CONTENT ONLY (a Section with setStage1Sink(&writer), no pages),
// so peak added RAM ≈ one block — the same bound as a normal parse's back half. It reuses
// Section::stepSectionBuild for the actual sliced walk, so the yield granularity + memory discipline
// match the reader's own incremental builds.
//
// Ordering: setReadPosition(p) builds the queue as [p, p+1, …, p+window-1, then the rest ascending].
// A later setReadPosition re-heads the queue toward the new position WITHOUT recompiling committed
// spines (decisions #1 + #2 of the E design: read position drives compile order + rolling window).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <HalStorage.h>  // FsFile

#include "ContentBinWriter.h"
#include "Epub/Section.h"

class Epub;
class GfxRenderer;

namespace compiled {

class ContentBinProducer {
 public:
  ContentBinProducer() = default;
  ~ContentBinProducer();

  ContentBinProducer(const ContentBinProducer&) = delete;
  ContentBinProducer& operator=(const ContentBinProducer&) = delete;

  // Open content.bin for the book and prepare the queue (all spines, ascending, minus any already
  // committed on disk with a matching fingerprint — an interrupted earlier compile resumes cleanly).
  // Returns false on I/O error (nothing to step). `params` is the Stage-1 (settings-independent)
  // build config; the same values a normal parse would use.
  bool begin(const std::shared_ptr<Epub>& epub, GfxRenderer& renderer, const Section::BuildParams& params);

  // Re-prioritize the compile queue toward `spineIndex`: the reader's current spine and the next
  // `windowAhead` spines move to the front (skipping any already compiled), the rest follow. Cheap;
  // call whenever the read position moves. No effect before begin() or after done().
  void setReadPosition(uint32_t spineIndex, uint32_t windowAhead = kDefaultWindowAhead);

  // Advance the current spine's compile by at most ~budgetMs of work (0 = run the current spine to
  // completion in one call). Returns true while work remains (call again), false once the queue is
  // drained (done()). A spine that fails to compile is logged and skipped (its slot stays 0; the
  // consumer will fall back to a live parse for it).
  bool step(uint32_t budgetMs);

  // True once every queued spine has been compiled (or skipped). begin() with no work leaves done().
  bool done() const { return queueHead_ >= queue_.size() && !building_; }

  // Flush the writer + close the file. Safe to call once; the destructor also calls it.
  bool finish();

  uint32_t committedCount() const { return committedCount_; }

  static constexpr uint32_t kDefaultWindowAhead = 4;  // rolling window size ahead of the reader

 private:
  bool startSpine(uint32_t spineIndex);  // beginSpineAt + fresh Section + attach sink
  void abortSpine();                     // drop the in-flight Section (partial slot stays 0)

  std::shared_ptr<Epub> epub_;
  GfxRenderer* renderer_ = nullptr;
  Section::BuildParams params_;

  FsFile binFile_;
  ContentBinWriter writer_;
  bool open_ = false;

  std::vector<uint32_t> queue_;   // spine indices left to compile, in priority order
  size_t queueHead_ = 0;          // next queue slot to compile
  std::vector<bool> committed_;   // committed_[spine] = its index slot is written (skip on re-queue)
  uint32_t committedCount_ = 0;

  // In-flight spine (one at a time; consumer-priority arbitration lives in the reader integration).
  std::unique_ptr<Section> building_;
  uint32_t buildingSpine_ = 0;
};

}  // namespace compiled
