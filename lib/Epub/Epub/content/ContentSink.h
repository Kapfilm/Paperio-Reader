#pragma once
// ContentSink — the Stage-1 consumer that turns the walk's BlockSink stream into a
// serializable compiled::CompiledContent (Phase 3 step 4 of
// docs/compiled-book-pipeline-plan.md; design in docs/stage1-extraction-design.md).
//
// One ContentSink fills ONE CompiledContent across a whole book: the driver builds
// each spine in document order with the same sink attached (Section::setStage1Sink),
// calling beginSpine() before each spine's build and relying on the producer's
// onSpineEnd() to close it. After all spines, writeContentBin(sink.content()) serializes.
//
// Responsibilities (the settings-independent half — never touches GfxRenderer):
//   - intern each block's CssStyle into the book-level pool, stamp Block::styleId;
//   - preserve the producer's book-absolute charOffset on every block;
//   - enforce the 8 KB split-at-write record cap (microreader MrbConverter rationale),
//     tagging continuation records with BlockFlags::kContinuation;
//   - build the per-spine anchor table and the book-level chapter/heading table.
//
// Deferred (documented, closed by later master-plan steps, NOT this one):
//   - onFootnote is ignored: footnotes get their own content.bin scan in Phase 3
//     step 4 of the master plan, not folded into records here.
//   - onPageBreakLabel is recorded in memory (labels()) but NOT serialized: the WBC1
//     format has no label table yet; it folds in with Phase 4. Kept so the driver /
//     dump tool can observe them without a format bump mid-sequence.
//
// Compiles unconditionally: nothing on the shipping device path constructs a
// ContentSink, so it is inert there. Only the host content_stage1_dump tool and the
// ContentSink tests drive it (behind EPUB_STAGE1 on those targets).

#include <cstdint>
#include <string>
#include <vector>

#include "BlockSink.h"
#include "CompiledContent.h"

struct FootnoteEntry;

namespace compiled {

// A block longer than this many serialized bytes is split into continuation records
// at write time, bounding read-time allocations. Matches the WBC1 format spec's 8 KB
// record cap (docs/compiled-content-format.md "Record cap") and microreader's
// kMaxSerializedBody. Only TEXT blocks split; image/table blocks pass through whole.
inline constexpr size_t kMaxSerializedBody = 8192;

class ContentSink : public BlockSink {
 public:
  // A printed-page label anchored to a block position (recorded, not yet serialized).
  struct PageLabel {
    std::string label;
    uint16_t spineIndex = 0;
    uint32_t blockIndex = 0;  // block index within the spine at which the label occurs
  };

  ContentSink() = default;

  // Open a new spine's block run. Call once per spine, before that spine's build runs.
  // firstCharOffset is captured from the first block the producer emits for the spine.
  void beginSpine();

  // BlockSink — driven by the walk during a Section build.
  void onBlock(Block&& block, const CssStyle& style) override;
  void onAnchor(const std::string& id) override;
  void onChapter(uint8_t level, const std::string& title) override;
  void onPageBreakLabel(const std::string& label) override;
  void onFootnote(int wordIndex, const FootnoteEntry& entry) override;
  void onSpineEnd() override;

  const CompiledContent& content() const { return content_; }
  CompiledContent& content() { return content_; }
  const std::vector<PageLabel>& labels() const { return labels_; }

 private:
  // Append a fully-styled TEXT block, splitting into <=kMaxSerializedBody records.
  void appendTextSplit(Block&& block);

  CompiledContent content_;
  std::vector<PageLabel> labels_;
  bool spineOpen_ = false;
  bool spineHasBlock_ = false;  // firstCharOffset captured from the first block
};

}  // namespace compiled
