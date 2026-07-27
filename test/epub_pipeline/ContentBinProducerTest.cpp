// ContentBinProducer (Increment E, sub-step 1b): the sliced, read-position-ordered background
// compiler. These tests validate the NEW producer against the already-proven content.bin path:
//   1. A book compiled by the sliced producer replays byte-identically, per spine, to the same book
//      compiled by the one-pass Section::compileBookToContentBin (which the SectionEquivalence /
//      replay-matrix tests already prove correct).
//   2. Slicing (a tiny budget forcing many step() calls) yields the SAME committed content as an
//      unbounded (budgetMs=0) run.
//   3. setReadPosition(K) commits spine K before spine 0 — read position drives compile order.
//   4. A partially-stepped producer leaves later spines uncommitted (spineAvailable() == false),
//      but every already-committed spine replays — the frontier the consumer chases.

#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <process.h>  // _getpid - per-process temp isolation under parallel ctest
#include <string>
#include <vector>

#include "Epub.h"
#include "Epub/Section.h"
#include "Epub/content/BlockStreamReader.h"
#include "Epub/content/ContentBinProducer.h"

namespace fs = std::filesystem;

namespace {

using compiled::Block;
using compiled::BlockStreamReader;
using compiled::ContentBinProducer;

std::string freshCacheDir(const std::string& tag) {
  const auto dir = fs::temp_directory_path() / ("content_bin_producer_" + std::to_string(_getpid())) / tag;
  fs::remove_all(dir);
  fs::create_directories(dir);
  return dir.string();
}

std::shared_ptr<Epub> loadBook(const std::string& name, const std::string& cacheDir) {
  auto epub = std::make_shared<Epub>(std::string(CORPUS_DIR) + "/" + name, cacheDir);
  EXPECT_TRUE(epub->load(true));
  epub->loadImageManifest();
  return epub;
}

Section::BuildParams goldenParams() {
  Section::BuildParams p;
  p.fontId = 0;
  p.lineCompression = 1.0f;
  p.extraParagraphSpacing = false;
  p.paragraphAlignment = 0;
  p.viewportWidth = 300;
  p.viewportHeight = 400;
  p.hyphenationEnabled = false;
  p.embeddedStyle = true;
  p.bionicReadingEnabled = false;
  p.inlineFootnotePreviews = false;
  p.imageRendering = 0;
  return p;
}

// Read every RAW record of one spine (order + fields) into a flat signature for comparison. Two
// content.bin files that produce identical per-spine raw records are byte-equivalent for that spine.
struct SpineSig {
  bool available = false;
  uint32_t firstCharOffset = 0;
  std::vector<std::string> blockTexts;   // one entry per RAW record
  std::vector<uint16_t> styleIds;        // spine-local style id per record
  std::vector<uint8_t> flags;
  size_t anchorCount = 0;
  size_t labelCount = 0;
  size_t chapterCount = 0;
  size_t styleCount = 0;
};

SpineSig readSpineSig(BlockStreamReader& r, uint32_t si) {
  SpineSig sig;
  sig.available = r.spineAvailable(si);
  if (!sig.available) return sig;
  EXPECT_TRUE(r.openSpine(si));
  sig.firstCharOffset = r.spineFirstCharOffset();
  sig.anchorCount = r.spineAnchors().size();
  sig.labelCount = r.spineLabels().size();
  sig.chapterCount = r.spineChapters().size();
  sig.styleCount = r.spineStylePool().size();
  Block b;
  while (r.nextRawRecord(b)) {
    sig.blockTexts.push_back(b.text);
    sig.styleIds.push_back(b.styleId);
    sig.flags.push_back(b.flags);
  }
  return sig;
}

void expectSpineSigEqual(const SpineSig& a, const SpineSig& b, uint32_t si) {
  EXPECT_EQ(a.available, b.available) << "spine " << si << " availability";
  if (!a.available || !b.available) return;
  EXPECT_EQ(a.firstCharOffset, b.firstCharOffset) << "spine " << si;
  EXPECT_EQ(a.blockTexts, b.blockTexts) << "spine " << si << " raw record texts";
  EXPECT_EQ(a.styleIds, b.styleIds) << "spine " << si << " style ids";
  EXPECT_EQ(a.flags, b.flags) << "spine " << si << " flags";
  EXPECT_EQ(a.anchorCount, b.anchorCount) << "spine " << si << " anchors";
  EXPECT_EQ(a.labelCount, b.labelCount) << "spine " << si << " labels";
  EXPECT_EQ(a.chapterCount, b.chapterCount) << "spine " << si << " chapters";
  EXPECT_EQ(a.styleCount, b.styleCount) << "spine " << si << " styles";
}

std::vector<SpineSig> readAllSigs(const std::string& binPath, uint32_t spineCount) {
  std::vector<SpineSig> sigs;
  FsFile in;
  EXPECT_TRUE(in.openForRead(binPath));
  BlockStreamReader r;
  EXPECT_TRUE(r.open(in));
  EXPECT_EQ(r.spineCount(), spineCount);
  for (uint32_t si = 0; si < spineCount; ++si) sigs.push_back(readSpineSig(r, si));
  in.close();
  return sigs;
}

// Run a producer to completion with the given budget; returns the content.bin path.
std::string runProducer(const std::shared_ptr<Epub>& epub, GfxRenderer& renderer, uint32_t budgetMs) {
  ContentBinProducer prod;
  EXPECT_TRUE(prod.begin(epub, renderer, goldenParams()));
  int guard = 0;
  while (!prod.done() && guard++ < 100000) prod.step(budgetMs);
  EXPECT_TRUE(prod.finish());
  return epub->getCachePath() + "/content.bin";
}

}  // namespace

// A multi-spine corpus book (8 spines) — exercises per-spine committing + reordering.
constexpr const char* kMultiSpineBook = "test_text_rendering.epub";

// Sliced producer output == one-pass compileBookToContentBin output, per spine.
TEST(ContentBinProducer, SlicedMatchesOnePass) {
  const std::string book = kMultiSpineBook;

  // Reference: the proven one-pass whole-book compile.
  const std::string refDir = freshCacheDir("ref");
  auto refEpub = loadBook(book, refDir);
  GfxRenderer refRenderer;
  ASSERT_TRUE(Section::compileBookToContentBin(refEpub, refRenderer, goldenParams()));
  const uint32_t spineCount = static_cast<uint32_t>(refEpub->getSpineItemsCount());
  const auto refSigs = readAllSigs(refEpub->getCachePath() + "/content.bin", spineCount);

  // Producer, heavily sliced (1 ms budget → many step() calls per spine).
  const std::string prodDir = freshCacheDir("prod");
  auto prodEpub = loadBook(book, prodDir);
  GfxRenderer prodRenderer;
  const std::string prodBin = runProducer(prodEpub, prodRenderer, /*budgetMs=*/1);
  const auto prodSigs = readAllSigs(prodBin, spineCount);

  ASSERT_EQ(refSigs.size(), prodSigs.size());
  for (uint32_t si = 0; si < spineCount; ++si) {
    EXPECT_TRUE(prodSigs[si].available) << "producer must commit every spine when run to completion";
    expectSpineSigEqual(refSigs[si], prodSigs[si], si);
  }
}

// Slicing is transparent: budget=1 and budget=0 (unbounded) commit identical content.
TEST(ContentBinProducer, BudgetIsTransparent) {
  const std::string book = kMultiSpineBook;

  const std::string aDir = freshCacheDir("budget0");
  auto aEpub = loadBook(book, aDir);
  GfxRenderer aRenderer;
  const std::string aBin = runProducer(aEpub, aRenderer, /*budgetMs=*/0);
  const uint32_t spineCount = static_cast<uint32_t>(aEpub->getSpineItemsCount());
  const auto aSigs = readAllSigs(aBin, spineCount);

  const std::string bDir = freshCacheDir("budget1");
  auto bEpub = loadBook(book, bDir);
  GfxRenderer bRenderer;
  const std::string bBin = runProducer(bEpub, bRenderer, /*budgetMs=*/1);
  const auto bSigs = readAllSigs(bBin, spineCount);

  ASSERT_EQ(aSigs.size(), bSigs.size());
  for (uint32_t si = 0; si < spineCount; ++si) expectSpineSigEqual(aSigs[si], bSigs[si], si);
}

// setReadPosition(K) makes the producer compile spine K before spine 0.
TEST(ContentBinProducer, ReadPositionDrivesOrder) {
  const std::string book = kMultiSpineBook;
  const std::string dir = freshCacheDir("reorder");
  auto epub = loadBook(book, dir);
  const std::string bin = epub->getCachePath() + "/content.bin";
  const uint32_t spineCount = static_cast<uint32_t>(epub->getSpineItemsCount());
  ASSERT_GE(spineCount, 3u) << "need >=3 spines to observe reordering";
  const uint32_t target = spineCount - 1;  // last spine

  GfxRenderer renderer;
  ContentBinProducer prod;
  ASSERT_TRUE(prod.begin(epub, renderer, goldenParams()));
  prod.setReadPosition(target, /*windowAhead=*/1);  // prioritize ONLY the last spine

  // Step until exactly one spine is committed; it must be `target`, not spine 0.
  int guard = 0;
  while (prod.committedCount() == 0 && !prod.done() && guard++ < 100000) prod.step(/*budgetMs=*/0);
  ASSERT_EQ(prod.committedCount(), 1u);

  // Verify on disk: the prioritized spine is available; spine 0 is not yet.
  {
    FsFile in;
    ASSERT_TRUE(in.openForRead(bin));
    BlockStreamReader r;
    ASSERT_TRUE(r.open(in));
    EXPECT_TRUE(r.spineAvailable(target)) << "prioritized spine must commit first";
    EXPECT_FALSE(r.spineAvailable(0)) << "spine 0 must not be committed yet";
    in.close();
  }

  // Drain the rest; now every spine is available and replayable.
  while (!prod.done() && guard++ < 100000) prod.step(/*budgetMs=*/0);
  ASSERT_TRUE(prod.finish());
  {
    FsFile in;
    ASSERT_TRUE(in.openForRead(bin));
    BlockStreamReader r;
    ASSERT_TRUE(r.open(in));
    for (uint32_t si = 0; si < spineCount; ++si) EXPECT_TRUE(r.spineAvailable(si)) << "spine " << si;
    in.close();
  }
}

// A partially-stepped producer: committed spines replay; the frontier of uncommitted spines does not.
TEST(ContentBinProducer, PartialFrontierIsReplayable) {
  const std::string book = kMultiSpineBook;
  const std::string dir = freshCacheDir("partial");
  auto epub = loadBook(book, dir);
  const std::string bin = epub->getCachePath() + "/content.bin";
  const uint32_t spineCount = static_cast<uint32_t>(epub->getSpineItemsCount());
  ASSERT_GE(spineCount, 2u);

  GfxRenderer renderer;
  ContentBinProducer prod;
  ASSERT_TRUE(prod.begin(epub, renderer, goldenParams()));

  // Commit exactly the first spine (ascending default order), then STOP without finishing the book.
  int guard = 0;
  while (prod.committedCount() < 1 && !prod.done() && guard++ < 100000) prod.step(/*budgetMs=*/0);
  ASSERT_GE(prod.committedCount(), 1u);

  // The file is mid-compile (writer not finished). A consumer opening it now must be able to replay
  // spine 0 and see later spines as unavailable.
  FsFile in;
  ASSERT_TRUE(in.openForRead(bin));
  BlockStreamReader r;
  ASSERT_TRUE(r.open(in)) << "a partially-written content.bin must open (frontier model)";
  EXPECT_TRUE(r.spineAvailable(0));
  EXPECT_TRUE(r.openSpine(0)) << "the committed spine must be replayable mid-compile";
  Block b;
  size_t records = 0;
  while (r.nextRawRecord(b)) ++records;
  EXPECT_GT(records, 0u) << "spine 0 has content";
  EXPECT_TRUE(r.ok());
  in.close();
}

// A consumer that opened content.bin before a spine was committed picks the spine up via
// refreshIndex() once the producer commits it — the frontier handshake (sub-step 3). Exercises the
// durable per-spine flush: the newly-committed slot + its data are visible to the second read handle.
TEST(ContentBinProducer, RefreshIndexPicksUpNewCommits) {
  const std::string book = kMultiSpineBook;
  const std::string dir = freshCacheDir("refresh");
  auto epub = loadBook(book, dir);
  const std::string bin = epub->getCachePath() + "/content.bin";
  const uint32_t spineCount = static_cast<uint32_t>(epub->getSpineItemsCount());
  ASSERT_GE(spineCount, 3u);

  GfxRenderer renderer;
  ContentBinProducer prod;
  ASSERT_TRUE(prod.begin(epub, renderer, goldenParams()));

  // Commit exactly spine 0 (ascending default order).
  int guard = 0;
  while (prod.committedCount() < 1 && !prod.done() && guard++ < 100000) prod.step(/*budgetMs=*/0);
  ASSERT_EQ(prod.committedCount(), 1u);

  // Consumer opens now: spine 0 available, spine 1 not yet.
  FsFile in;
  ASSERT_TRUE(in.openForRead(bin));
  BlockStreamReader r;
  ASSERT_TRUE(r.open(in));
  ASSERT_TRUE(r.spineAvailable(0));
  ASSERT_FALSE(r.spineAvailable(1)) << "spine 1 not committed at open time";

  // Producer commits the next spine while the consumer's handle stays open.
  while (prod.committedCount() < 2 && !prod.done() && guard++ < 100000) prod.step(/*budgetMs=*/0);
  ASSERT_GE(prod.committedCount(), 2u);

  // Without a refresh the consumer's snapshot is stale; refreshIndex() picks up the new commit and the
  // now-committed spine is fully replayable (its data was flushed before its slot).
  EXPECT_FALSE(r.spineAvailable(1)) << "stale snapshot before refresh";
  ASSERT_TRUE(r.refreshIndex());
  EXPECT_TRUE(r.spineAvailable(1)) << "refreshIndex must surface the newly committed spine";
  ASSERT_TRUE(r.openSpine(1));
  Block b;
  size_t records = 0;
  while (r.nextRawRecord(b)) ++records;
  EXPECT_GT(records, 0u);
  EXPECT_TRUE(r.ok());
  in.close();
}
