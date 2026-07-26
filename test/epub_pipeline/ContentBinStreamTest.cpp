// Streaming content.bin (plan v2): ContentBinWriter appends one block at a time and drops it;
// BlockStreamReader reads one logical block at a time (merging 8 KB kContinuation splits). These
// tests prove the streaming path produces a valid v5 file, reconstructs the block stream, rejects a
// corrupt/truncated file, and validates the fingerprint — without ever holding a whole spine.

#include <HalStorage.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "Epub/content/BlockStreamReader.h"
#include "Epub/content/CompiledContent.h"
#include "Epub/content/ContentBinWriter.h"
#include "Epub/content/ContentSink.h"
#include "PipelineRunner.h"

namespace fs = std::filesystem;

namespace {

using compiled::Block;
using compiled::BlockStreamReader;
using compiled::CompiledContent;
using compiled::ContentBinWriter;
using compiled::ContentSink;

std::string freshDir(const std::string& tag) {
  const auto dir = fs::temp_directory_path() / "content_bin_stream" / tag;
  fs::remove_all(dir);
  fs::create_directories(dir);
  return dir.string();
}

std::string corpus(const char* book) { return std::string(CORPUS_DIR) + "/" + book; }

// Build a whole book's CompiledContent (via the real producer) so we have a realistic block stream
// to re-drive through the STREAMING writer.
bool compileWhole(const char* book, const std::string& cacheDir, ContentSink& sink) {
  std::ostringstream log;
  return pipeline_harness::compileContent(corpus(book), cacheDir, pipeline_harness::Profile{}, sink, log);
}

// Drive a ContentBinWriter from an already-built CompiledContent, faithfully replaying the per-block
// side data (footnotes/xpath) that onBlock expects to arrive before the block.
bool streamWrite(const CompiledContent& c, const std::string& path, uint64_t fingerprint) {
  FsFile f;
  if (!f.openForWrite(path)) return false;
  ContentBinWriter w;
  if (!w.begin(f, static_cast<uint32_t>(c.spines.size()), fingerprint)) return false;
  for (const auto& spine : c.spines) {
    w.beginSpine();
    for (const auto& b : spine.blocks) {
      // Replay the block's own footnotes/xpath as the walk would (before onBlock).
      if (b.hasXPath) w.onXPathAdvance(b.xpath.paragraphIndex, b.xpath.listItemIndex, b.xpath.bodyChildByteOffset);
      for (const auto& fn : b.footnotes) w.onFootnote(static_cast<int>(fn.wordIndex), fn.entry);
      Block copy = b;
      copy.footnotes.clear();  // onBlock re-attaches from the pending buffer we just filled
      copy.hasXPath = false;
      const CssStyle& style = (b.styleId < c.stylePool.size()) ? c.stylePool[b.styleId] : CssStyle{};
      w.onBlock(std::move(copy), style);
    }
    for (const auto& a : spine.anchors) w.onAnchor(a.id);
    for (const auto& pl : spine.pageBreakLabels) w.onPageBreakLabel(pl.label);
    w.onSpineEnd();
  }
  const bool ok = w.finish();
  f.close();
  return ok;
}

}  // namespace

// The streaming writer + reader reproduce the LOGICAL block stream (continuation records merged
// back). We compare against the whole-book CompiledContent read via readContentBin, which stores
// the RAW split records — so we re-coalesce the whole-book side the same way for comparison.
TEST(ContentBinStream, WriteThenLogicalReadReconstructsBlocks) {
  const std::string dir = freshDir("roundtrip");
  ContentSink sink;
  ASSERT_TRUE(compileWhole("test_headings.epub", dir, sink));
  const CompiledContent& built = sink.content();

  const std::string bin = dir + "/content.bin";
  ASSERT_TRUE(streamWrite(built, bin, 0x1234'5678'9abc'def0ull));

  FsFile in;
  ASSERT_TRUE(in.openForRead(bin));
  BlockStreamReader r;
  ASSERT_TRUE(r.open(in));
  EXPECT_EQ(r.fingerprint(), 0x1234'5678'9abc'def0ull);
  ASSERT_EQ(r.spineCount(), built.spines.size());

  for (uint32_t si = 0; si < r.spineCount(); ++si) {
    ASSERT_TRUE(r.openSpine(si));
    // Reconstruct the built spine's LOGICAL blocks (merge kContinuation) for comparison.
    std::vector<std::string> builtLogicalText;
    for (const auto& b : built.spines[si].blocks) {
      if ((b.flags & compiled::kContinuation) != 0 && !builtLogicalText.empty()) {
        builtLogicalText.back() += b.text;
      } else {
        builtLogicalText.push_back(b.text);
      }
    }
    std::vector<std::string> streamedText;
    Block b;
    while (r.nextLogicalBlock(b)) streamedText.push_back(b.text);
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(streamedText.size(), builtLogicalText.size()) << "spine " << si << " logical block count";
    for (size_t i = 0; i < streamedText.size(); ++i) {
      EXPECT_EQ(streamedText[i], builtLogicalText[i]) << "spine " << si << " block " << i;
    }
    // After the block stream, the aux tables must read back.
    std::vector<compiled::Anchor> anchors;
    std::vector<compiled::PageBreakLabel> labels;
    ASSERT_TRUE(r.readSpineAux(anchors, labels));
    EXPECT_EQ(anchors.size(), built.spines[si].anchors.size());
    EXPECT_EQ(labels.size(), built.spines[si].pageBreakLabels.size());
  }
  in.close();
}

// Footnote-bearing book: footnotes must survive the streaming round-trip on the LOGICAL block.
TEST(ContentBinStream, FootnotesSurviveStreaming) {
  const std::string dir = freshDir("footnotes");
  ContentSink sink;
  ASSERT_TRUE(compileWhole("test_inline_footnotes.epub", dir, sink));
  const CompiledContent& built = sink.content();
  size_t builtFootnotes = 0;
  for (const auto& s : built.spines)
    for (const auto& b : s.blocks) builtFootnotes += b.footnotes.size();
  ASSERT_EQ(builtFootnotes, 3u);

  const std::string bin = dir + "/content.bin";
  ASSERT_TRUE(streamWrite(built, bin, 42));

  FsFile in;
  ASSERT_TRUE(in.openForRead(bin));
  BlockStreamReader r;
  ASSERT_TRUE(r.open(in));
  size_t streamedFootnotes = 0;
  for (uint32_t si = 0; si < r.spineCount(); ++si) {
    ASSERT_TRUE(r.openSpine(si));
    Block b;
    while (r.nextLogicalBlock(b)) streamedFootnotes += b.footnotes.size();
  }
  EXPECT_EQ(streamedFootnotes, 3u) << "the fixture's three footnotes must survive streaming";
  in.close();
}

// A truncated / never-finished (offsets still 0) content.bin must fail cleanly at open, not crash.
TEST(ContentBinStream, RejectsCorruptAndUnfinished) {
  const std::string dir = freshDir("corrupt");
  ContentSink sink;
  ASSERT_TRUE(compileWhole("test_headings.epub", dir, sink));

  // (1) A file whose header was written but never finish()ed: offsets stay 0 → reject.
  const std::string unfinished = dir + "/unfinished.bin";
  {
    FsFile f;
    ASSERT_TRUE(f.openForWrite(unfinished));
    ContentBinWriter w;
    ASSERT_TRUE(w.begin(f, static_cast<uint32_t>(sink.content().spines.size()), 7));
    // ... deliberately NO spines, NO finish() ...
    f.close();
  }
  {
    FsFile in;
    ASSERT_TRUE(in.openForRead(unfinished));
    BlockStreamReader r;
    EXPECT_FALSE(r.open(in)) << "unfinished (offsets==0) file must be rejected as stale";
    in.close();
  }

  // (2) A valid file truncated mid-stream → reject.
  const std::string full = dir + "/full.bin";
  ASSERT_TRUE(streamWrite(sink.content(), full, 9));
  const auto fullSize = fs::file_size(full);
  const std::string truncated = dir + "/truncated.bin";
  {
    std::string bytes(fullSize, '\0');
    FILE* fp = fopen(full.c_str(), "rb");
    ASSERT_TRUE(fp);
    ASSERT_EQ(fread(&bytes[0], 1, fullSize, fp), fullSize);
    fclose(fp);
    bytes.resize(fullSize / 2);  // chop in half — offsets now point past EOF
    FILE* out = fopen(truncated.c_str(), "wb");
    ASSERT_TRUE(out);
    fwrite(bytes.data(), 1, bytes.size(), out);
    fclose(out);
  }
  {
    FsFile in;
    ASSERT_TRUE(in.openForRead(truncated));
    BlockStreamReader r;
    EXPECT_FALSE(r.open(in)) << "truncated file (offsets past EOF) must be rejected";
    in.close();
  }
}
