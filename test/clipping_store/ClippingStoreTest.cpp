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
