// ContentSink tests (Phase 3 step 4 of docs/compiled-book-pipeline-plan.md): the
// Stage-1 consumer that turns the producer's BlockSink stream into a serializable
// CompiledContent. Covers three gates:
//   - the real producer -> ContentSink -> writeContentBin -> readContentBin round-trip
//     survives byte-for-byte (block/word/style/charOffset/anchor/chapter equality);
//   - the 8 KB split-at-write cap produces continuation records that reconstruct the
//     original word run and keep charOffset continuity;
//   - two full compiles produce a byte-identical content.bin (the determinism gate).

#include <HalStorage.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "Epub/content/CompiledContent.h"
#include "Serialization.h"
#include "Epub/content/ContentSink.h"
#include "PipelineRunner.h"

namespace fs = std::filesystem;

namespace {

using compiled::Block;
using compiled::BlockType;
using compiled::CompiledContent;
using compiled::ContentSink;
using compiled::Word;

std::string freshDir(const std::string& tag) {
  const auto dir = fs::temp_directory_path() / "content_sink_test" / tag;
  fs::remove_all(dir);
  fs::create_directories(dir);
  return dir.string();
}

std::string corpus(const char* book) { return std::string(CORPUS_DIR) + "/" + book; }

// Compile a whole book into a CompiledContent via the Stage-1 driver.
bool compile(const char* book, const std::string& cacheDir, ContentSink& sink) {
  std::ostringstream log;
  return pipeline_harness::compileContent(corpus(book), cacheDir, pipeline_harness::Profile{}, sink, log);
}

// Serialize a CompiledContent to a temp file and return its bytes.
std::vector<uint8_t> serialize(const CompiledContent& content, const std::string& path) {
  FsFile out;
  EXPECT_TRUE(out.openForWrite(path));
  EXPECT_TRUE(compiled::writeContentBin(out, content));
  out.close();
  FsFile in;
  EXPECT_TRUE(in.openForRead(path));
  const size_t n = in.fileSize();
  std::vector<uint8_t> bytes(n);
  EXPECT_EQ(in.read(bytes.data(), n), static_cast<int>(n));
  in.close();
  return bytes;
}

std::string wordAt(const std::string& text, uint32_t off) { return std::string(&text[off]); }

}  // namespace

// The real producer -> ContentSink -> content.bin -> readback survives intact.
TEST(ContentSink, RoundTripsThroughContentBin) {
  const std::string cacheDir = freshDir("roundtrip");
  ContentSink sink;
  ASSERT_TRUE(compile("test_headings.epub", cacheDir, sink));

  const CompiledContent& built = sink.content();
  ASSERT_FALSE(built.spines.empty());
  ASSERT_FALSE(built.stylePool.empty()) << "at least one block style was interned";

  // Every block references a valid pooled style; charOffsets are monotonic per spine.
  for (const auto& spine : built.spines) {
    uint32_t prev = spine.firstCharOffset;
    for (const auto& b : spine.blocks) {
      EXPECT_LT(b.styleId, built.stylePool.size());
      EXPECT_GE(b.charOffset, prev) << "charOffset monotonic in document order";
      prev = b.charOffset;
    }
  }
  ASSERT_FALSE(built.chapters.empty()) << "test_headings has headings";

  const std::string binPath = cacheDir + "/content.bin";
  FsFile out;
  ASSERT_TRUE(out.openForWrite(binPath));
  ASSERT_TRUE(compiled::writeContentBin(out, built));
  out.close();

  CompiledContent readback;
  FsFile in;
  ASSERT_TRUE(in.openForRead(binPath));
  ASSERT_TRUE(compiled::readContentBin(in, readback));
  in.close();

  // Model equality across the serialization boundary.
  ASSERT_EQ(readback.stylePool.size(), built.stylePool.size());
  ASSERT_EQ(readback.spines.size(), built.spines.size());
  ASSERT_EQ(readback.chapters.size(), built.chapters.size());
  for (size_t si = 0; si < built.spines.size(); ++si) {
    const auto& a = built.spines[si];
    const auto& b = readback.spines[si];
    EXPECT_EQ(a.firstCharOffset, b.firstCharOffset);
    ASSERT_EQ(a.blocks.size(), b.blocks.size()) << "spine " << si << " block count";
    for (size_t bi = 0; bi < a.blocks.size(); ++bi) {
      EXPECT_EQ(a.blocks[bi].type, b.blocks[bi].type);
      EXPECT_EQ(a.blocks[bi].styleId, b.blocks[bi].styleId);
      EXPECT_EQ(a.blocks[bi].flags, b.blocks[bi].flags);
      EXPECT_EQ(a.blocks[bi].charOffset, b.blocks[bi].charOffset);
      EXPECT_EQ(a.blocks[bi].text, b.blocks[bi].text) << "spine " << si << " block " << bi;
    }
    ASSERT_EQ(a.anchors.size(), b.anchors.size());
    for (size_t ai = 0; ai < a.anchors.size(); ++ai) {
      EXPECT_EQ(a.anchors[ai].id, b.anchors[ai].id);
      EXPECT_EQ(a.anchors[ai].blockIndex, b.anchors[ai].blockIndex);
    }
  }
  for (size_t ci = 0; ci < built.chapters.size(); ++ci) {
    EXPECT_EQ(built.chapters[ci].title, readback.chapters[ci].title);
    EXPECT_EQ(built.chapters[ci].level, readback.chapters[ci].level);
    EXPECT_EQ(built.chapters[ci].blockIndex, readback.chapters[ci].blockIndex);
    EXPECT_EQ(built.chapters[ci].spineIndex, readback.chapters[ci].spineIndex);
  }

  // Chapters point at heading blocks (kStartsChapter is set by the producer).
  for (const auto& ch : built.chapters) {
    ASSERT_LT(ch.spineIndex, built.spines.size());
    const auto& blocks = built.spines[ch.spineIndex].blocks;
    ASSERT_LT(ch.blockIndex, blocks.size());
    EXPECT_NE(blocks[ch.blockIndex].flags & compiled::kStartsChapter, 0);
  }
}

// A block over the 8 KB cap splits into continuation records that reconstruct the
// original word sequence, with the continuation flag on every record after the first.
TEST(ContentSink, SplitsOversizedTextBlockAtWrite) {
  ContentSink sink;
  sink.beginSpine();

  // Build one text block whose serialized body exceeds kMaxSerializedBody: many words,
  // each a few bytes, so several hundred cross 8 KB (~11 bytes/word serialized).
  constexpr int kWordCount = 2000;
  Block big;
  big.type = BlockType::Text;
  big.charOffset = 100;  // arbitrary book-absolute start
  std::vector<std::string> expectWords;
  for (int i = 0; i < kWordCount; ++i) {
    Word w;
    w.textOff = static_cast<uint32_t>(big.text.size());
    w.styleSpan = (i == 0) ? 0 : compiled::kSpanAttachPrev;  // preserved across the split
    w.sizePct = 100;
    big.words.push_back(w);
    const std::string word = "w" + std::to_string(i);  // ASCII: 1 codepoint per byte
    expectWords.push_back(word);
    big.text.append(word);
    big.text.push_back('\0');
  }
  const CssStyle style;  // empty style is fine; it interns to a valid id
  sink.onBlock(std::move(big), style);
  sink.onSpineEnd();

  const auto& blocks = sink.content().spines.at(0).blocks;
  ASSERT_GT(blocks.size(), 1u) << "the oversized block must split into multiple records";

  // First record carries the original flags (no continuation); the rest are continuations.
  EXPECT_EQ(blocks.front().flags & compiled::kContinuation, 0);
  for (size_t i = 1; i < blocks.size(); ++i)
    EXPECT_NE(blocks[i].flags & compiled::kContinuation, 0) << "record " << i << " must be a continuation";

  // Each record stays within the cap (allowing the fixed overhead + a single word slack).
  for (const auto& b : blocks) {
    const size_t body = b.words.size() * (sizeof(uint32_t) + 3) + b.text.size();
    EXPECT_LE(body, compiled::kMaxSerializedBody + 64u) << "record body within the 8 KB cap";
  }

  // Concatenating the records' words in order reconstructs the original sequence, and
  // charOffset advances by the codepoints consumed so far.
  std::vector<std::string> gotWords;
  uint32_t expectedCharOffset = 100;
  size_t consumed = 0;
  for (const auto& b : blocks) {
    EXPECT_EQ(b.styleId, blocks.front().styleId) << "all records share the interned style";
    EXPECT_EQ(b.charOffset, expectedCharOffset) << "continuation charOffset is contiguous";
    for (const auto& w : b.words) {
      gotWords.push_back(wordAt(b.text, w.textOff));
      expectedCharOffset += static_cast<uint32_t>(expectWords[consumed].size());  // ASCII => bytes == codepoints
      ++consumed;
    }
  }
  EXPECT_EQ(gotWords, expectWords);
}

// Regression (design-review 2026-07-24, bug #1): a block of LONG words can have serialized body
// under kMaxSerializedBody (8 KB) while its raw text exceeds serialization MAX_STRING_LENGTH
// (4 KB) — which writes fine but fails readback via readString. The split must bound each record's
// TEXT bytes, not just the serialized body. Assert each record's text fits AND the whole thing
// round-trips through writeContentBin/readContentBin (the pre-fix code failed the readback).
TEST(ContentSink, SplitsBoundRecordTextToStringCap) {
  ContentSink sink;
  sink.beginSpine();

  // ~80-byte words: with ~7 bytes/word serialized overhead, the serialized-body cap would allow a
  // run whose text alone is ~7 KB (> 4 KB) — the exact case that broke readback before the fix.
  const std::string longWord(80, 'x');
  constexpr int kWordCount = 400;  // ~32 KB of text total -> several records
  Block big;
  big.type = BlockType::Text;
  big.charOffset = 0;
  for (int i = 0; i < kWordCount; ++i) {
    Word w;
    w.textOff = static_cast<uint32_t>(big.text.size());
    w.sizePct = 100;
    big.words.push_back(w);
    big.text.append(longWord);
    big.text.push_back('\0');
  }
  sink.onBlock(std::move(big), CssStyle{});
  sink.onSpineEnd();

  const auto& blocks = sink.content().spines.at(0).blocks;
  ASSERT_GT(blocks.size(), 1u);
  for (const auto& b : blocks) {
    EXPECT_LE(b.text.size(), static_cast<size_t>(serialization::MAX_STRING_LENGTH))
        << "each record's text must fit the readString cap";
  }

  // The real proof: it must survive a content.bin round-trip (pre-fix, readback returned false).
  const std::string dir = freshDir("textcap");
  FsFile out;
  ASSERT_TRUE(out.openForWrite(dir + "/c.bin"));
  ASSERT_TRUE(compiled::writeContentBin(out, sink.content()));
  out.close();
  FsFile in;
  ASSERT_TRUE(in.openForRead(dir + "/c.bin"));
  CompiledContent readback;
  EXPECT_TRUE(compiled::readContentBin(in, readback)) << "long-word block must survive readback";
  in.close();
}

// Two full compiles of the same book produce a byte-identical content.bin.
TEST(ContentSink, ProducesDeterministicContentBin) {
  ContentSink a;
  ContentSink b;
  ASSERT_TRUE(compile("test_headings.epub", freshDir("det_a"), a));
  ASSERT_TRUE(compile("test_headings.epub", freshDir("det_b"), b));

  const std::string dir = freshDir("det_bytes");
  const auto bytesA = serialize(a.content(), dir + "/a.bin");
  const auto bytesB = serialize(b.content(), dir + "/b.bin");
  EXPECT_EQ(bytesA, bytesB) << "content.bin must be byte-identical across runs";
  EXPECT_FALSE(bytesA.empty());
}

// An image-bearing book compiles image blocks into content.bin intact.
TEST(ContentSink, CompilesImageBlocks) {
  ContentSink sink;
  ASSERT_TRUE(compile("test_png_images.epub", freshDir("images"), sink));

  size_t imageBlocks = 0;
  for (const auto& spine : sink.content().spines) {
    for (const auto& b : spine.blocks) {
      if (b.type != BlockType::Image) continue;
      ++imageBlocks;
      EXPECT_FALSE(b.entryPath.empty());
      EXPECT_GT(b.width, 0);
      EXPECT_GT(b.height, 0);
    }
  }
  EXPECT_GT(imageBlocks, 0u) << "test_png_images has block images";
}
