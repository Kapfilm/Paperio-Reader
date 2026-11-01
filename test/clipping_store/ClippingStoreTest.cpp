#include <ClippingStore.h>
#include <HalStorage.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <vector>

class ClippingStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    char root[] = "/tmp/paperio-clippings-XXXXXX";
    ASSERT_NE(mkdtemp(root), nullptr);
    Storage.root = root;
  }
  void TearDown() override { std::filesystem::remove_all(Storage.root); }
  static ClippingStore::AddResult add(ClippingStore& store, const std::string& anchor = {}, bool fullNote = false) {
    return store.addClipping(2, 0, 0, 3, 1, 4, 4, "Chapter", 8, "Selected text", ClippingHighlightStyle::Underline,
                             anchor, fullNote);
  }
  std::filesystem::path filePath() const {
    for (const auto& file : std::filesystem::directory_iterator(Storage.root + "/.crosspoint/clippings"))
      if (file.path().extension() == ".bin") return file.path();
    return {};
  }
};

TEST_F(ClippingStoreTest, KeepsIdenticalPageCoordinatesInDifferentPreviewContexts) {
  ClippingStore store;
  ASSERT_TRUE(store.loadForBook("/book.epub", "Book", "Author"));
  ASSERT_EQ(add(store), ClippingStore::AddResult::Added);
  ASSERT_EQ(add(store, "verse-5"), ClippingStore::AddResult::Added);
  ASSERT_EQ(add(store, "verse-8", true), ClippingStore::AddResult::Added);
  ClippingStore loaded;
  ASSERT_TRUE(loaded.loadForBook("/book.epub", "Book", "Author"));
  ASSERT_EQ(loaded.getAll().size(), 3u);
  EXPECT_TRUE(loaded.getAll()[0].previewAnchor.empty());
  EXPECT_EQ(loaded.getAll()[1].previewAnchor, "verse-5");
  EXPECT_EQ(loaded.getAll()[2].previewAnchor, "verse-8");
  EXPECT_TRUE(loaded.getAll()[2].fullNote);
  EXPECT_FALSE(loaded.getAll()[0].fullNote);
  EXPECT_EQ(loaded.getAll()[1].highlightStyle, ClippingHighlightStyle::Underline);
}

TEST_F(ClippingStoreTest, ReadsVersionThreeWithoutPreviewIdentity) {
  ClippingStore store;
  ASSERT_TRUE(store.loadForBook("/book.epub", "Book", "Author"));
  ASSERT_EQ(add(store), ClippingStore::AddResult::Added);
  const auto path = filePath();
  std::ifstream in(path, std::ios::binary);
  std::vector<char> bytes((std::istreambuf_iterator<char>(in)), {});
  in.close();
  ASSERT_GT(bytes.size(), 4u);
  bytes[0] = 3;
  bytes.resize(bytes.size() - 5);  // v3 had no trailing empty anchor length or note context.
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), bytes.size());
  out.close();
  ClippingStore loaded;
  ASSERT_TRUE(loaded.loadForBook("/book.epub", "Book", "Author"));
  ASSERT_EQ(loaded.getAll().size(), 1u);
  EXPECT_TRUE(loaded.getAll()[0].previewAnchor.empty());
  EXPECT_EQ(loaded.getAll()[0].text, "Selected text");
}

TEST_F(ClippingStoreTest, RejectsOversizedAnchorWithoutChangingSavedClippings) {
  ClippingStore store;
  ASSERT_TRUE(store.loadForBook("/book.epub", "Book", "Author"));
  ASSERT_EQ(add(store, std::string(191, 'a')), ClippingStore::AddResult::Added);
  ASSERT_EQ(add(store, std::string(192, 'a')), ClippingStore::AddResult::SaveFailed);
  ClippingStore loaded;
  ASSERT_TRUE(loaded.loadForBook("/book.epub", "Book", "Author"));
  ASSERT_EQ(loaded.getAll().size(), 1u);
  EXPECT_EQ(loaded.getAll()[0].previewAnchor.size(), 191u);
}

TEST_F(ClippingStoreTest, RejectsCorruptAnchorLength) {
  ClippingStore store;
  ASSERT_TRUE(store.loadForBook("/book.epub", "Book", "Author"));
  ASSERT_EQ(add(store), ClippingStore::AddResult::Added);
  std::fstream file(filePath(), std::ios::binary | std::ios::in | std::ios::out);
  file.seekp(-5, std::ios::end);
  const uint32_t oversized = 192;
  file.write(reinterpret_cast<const char*>(&oversized), sizeof(oversized));
  file.close();
  ClippingStore loaded;
  EXPECT_FALSE(loaded.loadForBook("/book.epub", "Book", "Author"));
  EXPECT_TRUE(loaded.empty());
}

TEST_F(ClippingStoreTest, NativeChapterMigrationArchivesCoordinatesAndAllowsRetry) {
  ClippingStore original;
  ASSERT_TRUE(original.loadForBook("/book.fb2", "Book", "Author"));
  ASSERT_EQ(add(original, "note-1"), ClippingStore::AddResult::Added);
  const auto path = filePath();
  std::ifstream input(path, std::ios::binary);
  const std::string expected((std::istreambuf_iterator<char>(input)), {});
  input.close();
  ASSERT_TRUE(Storage.mkdir("/.crosspoint/book.before-native-chapters"));
  ASSERT_TRUE(ClippingStore::archiveForBook("/book.fb2", "/.crosspoint/book.before-native-chapters"));
  EXPECT_FALSE(std::filesystem::exists(path));
  std::ifstream archive(Storage.root + "/.crosspoint/book.before-native-chapters/clippings.bin", std::ios::binary);
  EXPECT_EQ(std::string((std::istreambuf_iterator<char>(archive)), {}), expected);
  EXPECT_TRUE(ClippingStore::archiveForBook("/book.fb2", "/.crosspoint/book.before-native-chapters"));
  ClippingStore newLayout;
  ASSERT_TRUE(newLayout.loadForBook("/book.fb2", "Book", "Author"));
  EXPECT_TRUE(newLayout.empty());
}

TEST_F(ClippingStoreTest, NativeChapterMigrationNeverOverwritesExistingArchive) {
  ClippingStore original;
  ASSERT_TRUE(original.loadForBook("/book.fb2", "Book", "Author"));
  ASSERT_EQ(add(original), ClippingStore::AddResult::Added);
  ASSERT_TRUE(Storage.mkdir("/.crosspoint/backup"));
  std::ofstream(Storage.root + "/.crosspoint/backup/clippings.bin") << "original backup";
  EXPECT_FALSE(ClippingStore::archiveForBook("/book.fb2", "/.crosspoint/backup"));
  ClippingStore stillThere;
  ASSERT_TRUE(stillThere.loadForBook("/book.fb2", "Book", "Author"));
  EXPECT_EQ(stillThere.getAll().size(), 1u);
}

namespace {
uint32_t firstChoice(uint32_t) { return 0; }
uint32_t lastChoice(uint32_t bound) { return bound - 1; }
uint32_t randomState = 0x12345678;
uint32_t seededChoice(uint32_t bound) {
  randomState ^= randomState << 13;
  randomState ^= randomState >> 17;
  randomState ^= randomState << 5;
  return randomState % bound;
}
}  // namespace

TEST_F(ClippingStoreTest, RandomHighlightHandlesEmptyLibraryAndDeletedClippings) {
  ClippingQuote quote{"stale", "old", "old"};
  EXPECT_FALSE(ClippingStore::randomHighlight(quote, firstChoice));
  EXPECT_TRUE(quote.text.empty());
  ClippingStore store;
  ASSERT_TRUE(store.loadForBook("/book.epub", "Book", "Author"));
  ASSERT_EQ(add(store), ClippingStore::AddResult::Added);
  ASSERT_TRUE(ClippingStore::randomHighlight(quote, firstChoice));
  EXPECT_EQ(quote.text, "Selected text");
  EXPECT_EQ(quote.title, "Book");
  EXPECT_EQ(quote.author, "Author");
  ASSERT_TRUE(store.removeAt(0));
  std::ofstream(Storage.root + "/My Clippings.txt") << "Deleted exported quote";
  EXPECT_FALSE(ClippingStore::randomHighlight(quote, firstChoice));
}

TEST_F(ClippingStoreTest, RandomHighlightSamplesAcrossAllBooksAndHighlights) {
  ClippingStore a, b;
  ASSERT_TRUE(a.loadForBook("/a.fb2", "Alpha", "Author A"));
  ASSERT_TRUE(b.loadForBook("/b.epub", "Beta", "Author B"));
  for (int i = 0; i < 3; ++i)
    ASSERT_EQ(a.addClipping(0, 0, 0, 1, 0, 1, 1, "", 0, "a" + std::to_string(i), ClippingHighlightStyle::Marker),
              ClippingStore::AddResult::Added);
  ASSERT_EQ(b.addClipping(0, 0, 0, 1, 0, 1, 1, "", 0, "b", ClippingHighlightStyle::Marker),
            ClippingStore::AddResult::Added);
  int hits[4] = {};
  randomState = 0x12345678;
  for (int i = 0; i < 2000; ++i) {
    ClippingQuote quote;
    ASSERT_TRUE(ClippingStore::randomHighlight(quote, seededChoice));
    if (quote.text == "b") {
      ++hits[3];
      EXPECT_EQ(quote.title, "Beta");
    } else {
      ASSERT_EQ(quote.text.size(), 2u);
      ++hits[quote.text[1] - '0'];
      EXPECT_EQ(quote.author, "Author A");
    }
  }
  for (int hit : hits) EXPECT_GT(hit, 350);
}

TEST_F(ClippingStoreTest, RandomHighlightRejectsWholeTruncatedBookAndIgnoresBackups) {
  ClippingStore store;
  ASSERT_TRUE(store.loadForBook("/bad.epub", "Bad", "Author"));
  ASSERT_EQ(add(store), ClippingStore::AddResult::Added);
  ASSERT_EQ(add(store), ClippingStore::AddResult::Added);
  auto path = filePath();
  std::filesystem::copy_file(path, path.string() + ".bak");
  std::filesystem::resize_file(path, std::filesystem::file_size(path) - 1);
  ClippingQuote quote;
  EXPECT_FALSE(ClippingStore::randomHighlight(quote, firstChoice));
  ClippingStore valid;
  ASSERT_TRUE(valid.loadForBook("/good.epub", "Good", "Author"));
  ASSERT_EQ(add(valid), ClippingStore::AddResult::Added);
  ASSERT_TRUE(ClippingStore::randomHighlight(quote, firstChoice));
  EXPECT_EQ(quote.title, "Good");
}

TEST_F(ClippingStoreTest, RandomHighlightReadsAllLegacyRecordVersions) {
  for (uint8_t version = 1; version <= 4; ++version) {
    ClippingStore store;
    const std::string path = "/v" + std::to_string(version) + ".epub";
    ASSERT_TRUE(store.loadForBook(path, "Legacy", "Author"));
    ASSERT_EQ(add(store), ClippingStore::AddResult::Added);
    auto dir = Storage.open("/.crosspoint/clippings", O_RDONLY);
    auto file = dir.openNextFile();
    char name[64] = {};
    file.getName(name, sizeof(name));
    file.close();
    dir.close();
    const auto diskPath = Storage.root + "/.crosspoint/clippings/" + name;
    std::ifstream in(diskPath, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), {});
    in.close();
    const size_t header = 3 + 4 + 6 + 4 + 6 + 4 + path.size();
    if (version < 4) bytes.resize(bytes.size() - 5);
    if (version < 3) bytes.erase(bytes.begin() + header + 20);
    if (version == 1) {
      bytes.erase(bytes.begin() + header + 20, bytes.begin() + header + 20 + 4 + 7);
      std::vector<char> title(48);
      std::memcpy(title.data(), "Chapter", 7);
      bytes.insert(bytes.begin() + header + 20, title.begin(), title.end());
    }
    bytes[0] = version;
    std::ofstream out(diskPath, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), bytes.size());
    out.close();
    ClippingQuote quote;
    ASSERT_TRUE(ClippingStore::randomHighlight(quote, lastChoice)) << "version " << int(version);
    EXPECT_EQ(quote.text, "Selected text");
    store.clearAll();
  }
}
