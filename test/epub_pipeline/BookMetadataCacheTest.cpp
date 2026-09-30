#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstring>
#include <cstdlib>
#include "Epub/BookMetadataCache.h"
#include "Epub.h"

namespace {
class BookMetadataCacheRecovery : public ::testing::Test {
 protected:
  std::string dir;
  void SetUp() override {
    char path[] = "/tmp/paperio-book-cache-XXXXXX";
    const char* made = mkdtemp(path);
    ASSERT_NE(made, nullptr);
    dir = made;
  }
  void TearDown() override { std::filesystem::remove_all(dir); }
  void write(const std::vector<unsigned char>& bytes) {
    std::ofstream out(dir + "/book.bin", std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  }
  static std::vector<unsigned char> valid() {
    // Version 11, metadata boundary 42, no spine/TOC entries, eight empty strings.
    std::vector<unsigned char> bytes(42, 0);
    bytes[0] = 11;
    bytes[1] = 42;
    return bytes;
  }
};
TEST_F(BookMetadataCacheRecovery, RejectsEveryTruncatedHeader) {
  const auto bytes = valid();
  for (size_t n = 0; n < bytes.size(); ++n) {
    write({bytes.begin(), bytes.begin() + n});
    BookMetadataCache cache(dir);
    EXPECT_FALSE(cache.load()) << "truncated length=" << n;
  }
  write(bytes);
  BookMetadataCache complete(dir);
  EXPECT_TRUE(complete.load());
}
TEST_F(BookMetadataCacheRecovery, RejectsStringOutsideMetadataAndMissingLookupTable) {
  auto bytes = valid();
  bytes[10] = 255;  // First string length goes beyond the metadata region.
  write(bytes);
  { BookMetadataCache cache(dir); EXPECT_FALSE(cache.load()); }
  bytes = valid();
  bytes[5] = 1;  // One spine entry requires a 4-byte lookup absent from this file.
  write(bytes);
  { BookMetadataCache cache(dir); EXPECT_FALSE(cache.load()); }
}
// FB2 chapters are split into virtual spines; note sections also count as spines
// but deliberately do not appear in the generated TOC. Conclave has 145 spines
// and only 22 named main-text entries, below the generic EPUB 25% heuristic.
TEST_F(BookMetadataCacheRecovery, Fb2KeepsSparseNamedTocWithSyntheticFallbackEnabled) {
  const std::string package = dir + "/fb2_book/package.epub";
  std::filesystem::create_directories(package);
  std::ofstream(dir + "/fb2_book/.fb2_source") << "source.fb2";
  Epub epub(package, dir);
  ASSERT_TRUE(epub.isFb2Package());
  {
    BookMetadataCache cache(epub.getCachePath());
    ASSERT_TRUE(cache.beginWrite());
    ASSERT_TRUE(cache.beginContentOpfPass());
    for (int i = 0; i < 145; ++i) cache.createSpineEntry("chapter" + std::to_string(i) + ".xhtml");
    ASSERT_TRUE(cache.endContentOpfPass());
    ASSERT_TRUE(cache.beginTocPass());
    cache.createTocEntry("От автора", "chapter1.xhtml", "section1", 1);
    cache.createTocEntry("1. Sede vacante[2]", "chapter3.xhtml", "section3", 1);
    for (int i = 2; i < 22; ++i)
      cache.createTocEntry("Глава " + std::to_string(i), "chapter" + std::to_string(i + 3) + ".xhtml", "", 1);
    ASSERT_TRUE(cache.endTocPass());
    ASSERT_TRUE(cache.endWrite());
    ASSERT_TRUE(cache.buildBookBin(package, {}, true));
    ASSERT_TRUE(cache.load());
    ASSERT_FALSE(cache.isTocReliable());  // Reproduce the persisted fix1 cache.
  }
  ASSERT_TRUE(epub.load(false, true));
  epub.setSyntheticTocFallbackEnabled(true);
  EXPECT_TRUE(epub.hasReliableToc());
  EXPECT_EQ(epub.getTocItemsCount(), 22);
  EXPECT_EQ(epub.getTocItem(0).title, "От автора");
  EXPECT_EQ(epub.getTocItem(1).title, "1. Sede vacante[2]");
  EXPECT_EQ(epub.getSpineIndexForTocIndex(1), 3);
  EXPECT_EQ(epub.getTocIndexForSpineIndex(3), 1);

  // The same sparse cache for a regular EPUB retains its existing fallback.
  const std::string regular = dir + "/regular.epub";
  std::filesystem::create_directories(regular);
  Epub other(regular, dir);
  std::filesystem::create_directories(other.getCachePath());
  std::filesystem::copy_file(epub.getCachePath() + "/book.bin", other.getCachePath() + "/book.bin");
  ASSERT_TRUE(other.load(false, true));
  other.setSyntheticTocFallbackEnabled(true);
  EXPECT_FALSE(other.hasReliableToc());
  EXPECT_EQ(other.getTocItemsCount(), 145);
  EXPECT_EQ(other.getSpineIndexForTocIndex(3), 3);
}
}  // namespace

TEST_F(BookMetadataCacheRecovery, SequentialTocHandlesBibleSizeRepeatedBackwardAndMissingTargets) {
  BookMetadataCache cache(dir);
  ASSERT_TRUE(cache.beginWrite());
  ASSERT_TRUE(cache.beginContentOpfPass());
  for (int i = 0; i < 3941; ++i) cache.createSpineEntry("chapter" + std::to_string(i));
  ASSERT_TRUE(cache.endContentOpfPass());
  ASSERT_TRUE(cache.beginTocPass(true));
  for (int i = 0; i < 3941; ++i) cache.createTocEntry(std::to_string(i), "chapter" + std::to_string(i), "", 1);
  cache.createTocEntry("repeat", "chapter3940", "", 1);
  cache.createTocEntry("back", "chapter2", "", 1);
  cache.createTocEntry("missing", "missing", "", 1);
  cache.createTocEntry("first", "chapter0", "", 1);
  ASSERT_TRUE(cache.endTocPass());
  ASSERT_TRUE(cache.endWrite());
  // Inspect the streamed temp records directly: no package/ZIP needed for this lookup test.
  std::ifstream in(dir + "/toc.bin.tmp", std::ios::binary);
  auto string = [&]() { uint32_t size = 0; in.read(reinterpret_cast<char*>(&size), 4); in.seekg(size, std::ios::cur); };
  for (int i = 0; i < 3945; ++i) {
    string(); string(); string();
    uint8_t level; int16_t spine;
    in.read(reinterpret_cast<char*>(&level), 1);
    in.read(reinterpret_cast<char*>(&spine), 2);
    ASSERT_TRUE(in.good());
    const int expected = i < 3941 ? i : i == 3941 ? 3940 : i == 3942 ? 2 : i == 3943 ? -1 : 0;
    EXPECT_EQ(spine, expected) << i;
  }
}
