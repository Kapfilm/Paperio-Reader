#include <Fb2StateMigration.h>
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <unistd.h>
namespace fs = std::filesystem;
class Fb2StateMigrationTest : public ::testing::Test {
 protected:
  std::string root, cache;
  void SetUp() override {
    root = (fs::temp_directory_path() / ("fb2-state-migration-" + std::to_string(getpid()))).string();
    fs::remove_all(root);
    fs::create_directories(root);
    cache = root + "/epub_book";
  }
  void TearDown() override { fs::remove_all(root); }
  template<class T> static void pod(std::ofstream& f, T value) { f.write(reinterpret_cast<const char*>(&value), sizeof(value)); }
  static void write(const std::string& path, const std::string& bytes) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream(path, std::ios::binary).write(bytes.data(), bytes.size());
  }
  static std::string read(const std::string& path) {
    std::ifstream f(path, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}};
  }
  void seed(uint16_t savedSpine = 3, int version = 29) {
    fs::create_directories(cache);
    std::ofstream f(cache + "/.fb2_sections.bin", std::ios::binary);
    f.write("FB2IDX", 6); pod<uint8_t>(f, version);
    for (uint32_t offset : {100u, 200u, 200u, 200u, 300u}) {
      pod<uint8_t>(f, 0); pod(f, offset);
      pod<uint16_t>(f, 0); pod<uint16_t>(f, 0);
      for (int i=0;i<5;i++) pod<uint32_t>(f, 0);
    }
    f.close();
    std::ofstream progress(cache + "/progress.bin", std::ios::binary);
    pod(progress, savedSpine); pod<uint16_t>(progress, 27); pod<uint16_t>(progress, 30);
    write(cache + "/bookmarks.bin", "original-bookmarks");
    write(cache + "/stats.bin", "statistics");
    write(cache + "/reader_settings.bin", "settings");
    write(cache + "/sections/3_hash.bin", "old-rendered-page");
  }
};
TEST_F(Fb2StateMigrationTest, ArchivesEverythingAndStartsSameNativeChapter) {
  seed();
  ASSERT_TRUE(Fb2StateMigration::prepare(cache));
  auto backup = Fb2StateMigration::backupPath(cache);
  EXPECT_EQ(read(backup + "/bookmarks.bin"), "original-bookmarks");
  EXPECT_EQ(read(backup + "/sections/3_hash.bin"), "old-rendered-page");
  EXPECT_FALSE(fs::exists(cache + "/bookmarks.bin"));
  EXPECT_EQ(read(cache + "/stats.bin"), "statistics");
  auto progress = read(cache + "/progress.bin");
  ASSERT_EQ(progress.size(), 11u);
  EXPECT_EQ(static_cast<uint8_t>(progress[0]), 1); // old spine3 -> native source chapter1
  EXPECT_EQ(static_cast<uint8_t>(progress[2]), 0);
  EXPECT_TRUE(Fb2StateMigration::needsReaderMigration(cache));
  EXPECT_TRUE(Fb2StateMigration::needsNotice(cache));
}
TEST_F(Fb2StateMigrationTest, RetryAfterCacheDeletionRestoresBaselineFromArchive) {
  seed(); ASSERT_TRUE(Fb2StateMigration::prepare(cache));
  fs::remove_all(cache);
  ASSERT_TRUE(Fb2StateMigration::prepare(cache));
  EXPECT_EQ(static_cast<uint8_t>(read(cache + "/progress.bin")[0]), 1);
  EXPECT_EQ(read(cache + "/stats.bin"), "statistics");
}
TEST_F(Fb2StateMigrationTest, InterruptedRenameResumesFromCommittedMarker) {
  seed(); ASSERT_TRUE(Fb2StateMigration::prepare(cache));
  fs::remove_all(cache);
  fs::rename(Fb2StateMigration::backupPath(cache), cache); // crash before original rename
  ASSERT_TRUE(Fb2StateMigration::prepare(cache));
  EXPECT_EQ(static_cast<uint8_t>(read(cache + "/progress.bin")[0]), 1);
  EXPECT_EQ(read(Fb2StateMigration::backupPath(cache) + "/bookmarks.bin"), "original-bookmarks");
}
TEST_F(Fb2StateMigrationTest, InvalidPositionAbortsWithoutDeletingOriginals) {
  seed(50); const auto original = read(cache + "/progress.bin");
  EXPECT_FALSE(Fb2StateMigration::prepare(cache));
  EXPECT_EQ(read(cache + "/progress.bin"), original);
  EXPECT_EQ(read(cache + "/bookmarks.bin"), "original-bookmarks");
  EXPECT_FALSE(fs::exists(Fb2StateMigration::backupPath(cache)));
}
TEST_F(Fb2StateMigrationTest, ExistingBackupIsNeverOverwritten) {
  seed(); write(Fb2StateMigration::backupPath(cache) + "/bookmarks.bin", "precious");
  EXPECT_FALSE(Fb2StateMigration::prepare(cache));
  EXPECT_EQ(read(Fb2StateMigration::backupPath(cache) + "/bookmarks.bin"), "precious");
  EXPECT_TRUE(fs::exists(cache + "/progress.bin"));
}
TEST_F(Fb2StateMigrationTest, CompletionIsPersistentAndDoesNotRewindNewReading) {
  seed(); ASSERT_TRUE(Fb2StateMigration::prepare(cache));
  ASSERT_TRUE(Fb2StateMigration::markReaderMigrated(cache));
  EXPECT_FALSE(Fb2StateMigration::needsReaderMigration(cache));
  EXPECT_TRUE(Fb2StateMigration::needsNotice(cache));
  ASSERT_TRUE(Fb2StateMigration::acknowledgeNotice(cache));
  EXPECT_FALSE(Fb2StateMigration::needsNotice(cache));
  write(cache + "/progress.bin", "new-reading-position");
  EXPECT_TRUE(Fb2StateMigration::prepare(cache));
  EXPECT_EQ(read(cache + "/progress.bin"), "new-reading-position");
}
TEST_F(Fb2StateMigrationTest, NewPackagesAndFreshBooksAreUnchanged) {
  EXPECT_TRUE(Fb2StateMigration::prepare(cache));
  EXPECT_FALSE(Fb2StateMigration::needsNotice(cache));
  seed(3, 30); auto original = read(cache + "/progress.bin");
  EXPECT_TRUE(Fb2StateMigration::prepare(cache));
  EXPECT_EQ(read(cache + "/progress.bin"), original);
  EXPECT_FALSE(fs::exists(Fb2StateMigration::backupPath(cache)));
}
TEST_F(Fb2StateMigrationTest, NoSavedPositionDoesNotInventProgress) {
  seed(); fs::remove(cache + "/progress.bin");
  ASSERT_TRUE(Fb2StateMigration::prepare(cache));
  EXPECT_FALSE(fs::exists(cache + "/progress.bin"));
  EXPECT_TRUE(Fb2StateMigration::needsNotice(cache));
}
TEST_F(Fb2StateMigrationTest, RolledBackPackageCannotReuseCompletedMarker) {
  seed(); ASSERT_TRUE(Fb2StateMigration::prepare(cache));
  ASSERT_TRUE(Fb2StateMigration::markReaderMigrated(cache));
  seed();
  EXPECT_FALSE(Fb2StateMigration::prepare(cache));
  EXPECT_EQ(read(cache + "/bookmarks.bin"), "original-bookmarks");
}
TEST_F(Fb2StateMigrationTest, MissingOrCorruptIndexDoesNotReinterpretUnversionedState) {
  seed(); const auto original = read(cache + "/progress.bin");
  fs::remove(cache + "/.fb2_sections.bin");
  EXPECT_FALSE(Fb2StateMigration::prepare(cache));
  EXPECT_EQ(read(cache + "/progress.bin"), original);
  write(cache + "/.fb2_sections.bin", "broken");
  EXPECT_FALSE(Fb2StateMigration::prepare(cache));
  EXPECT_EQ(read(cache + "/progress.bin"), original);
  seed(3, 28);
  EXPECT_FALSE(Fb2StateMigration::prepare(cache));
  EXPECT_EQ(read(cache + "/bookmarks.bin"), "original-bookmarks");
}
TEST_F(Fb2StateMigrationTest, RollbackDuringPendingMigrationDoesNotReuseOldCoordinates) {
  seed(); ASSERT_TRUE(Fb2StateMigration::prepare(cache));
  seed(); // v29 reconstructed by an old firmware before migration acknowledgement
  EXPECT_FALSE(Fb2StateMigration::prepare(cache));
  EXPECT_EQ(read(cache + "/bookmarks.bin"), "original-bookmarks");
}
