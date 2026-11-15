#include <Fb2.h>
#include <Fb2AnchorIndex.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {
class Fb2AnchorScanTest : public testing::Test {
 protected:
  void SetUp() override {
    auto name = (std::filesystem::temp_directory_path() / "fb2-anchor-scan-XXXXXX").string();
    ASSERT_NE(mkdtemp(name.data()), nullptr);
    root = name;
  }

  void TearDown() override {
    if (!root.empty()) std::filesystem::remove_all(root);
  }

  std::filesystem::path root;
};

TEST_F(Fb2AnchorScanTest, InitialScanMatchesRenderBasedIndex) {
  const auto source = (root / "book.fb2").string();
  std::ofstream file(source);
  file << R"(<FictionBook xmlns:l="http://www.w3.org/1999/xlink">
    <description><title-info><book-title>Anchor index regression</book-title>
      <annotation id="annotation"><p id="annotation-paragraph">Summary</p></annotation>
    </title-info></description>
    <body>
      <title id="preamble-title"><p id="preamble-title-line">Book title</p></title>
      <epigraph id="preamble-epigraph"><p id="preamble-epigraph-line">Motto</p></epigraph>
      <section id="parent"><title id="chapter-title"><p id="title-line">Parent</p></title>
        <p id="duplicate">Parent before child <a id="note-link" l:href="#note-inline">Note</a></p>
        <section id="child"><title><p>Child</p></title><p id="duplicate">Duplicate</p>
          <custom id="unknown-wrapper"><p id="wrapped-paragraph">Wrapped</p></custom>
          <table id="table"><tr id="row"><td id="cell"><p id="cell-paragraph">Cell</p></td></tr></table>
        </section>
        <p id="parent-tail">Parent after child</p>
      </section>
      <section id="calibre"><p id="fallback-paragraph"><strong id="fallback-strong">Styled heading</strong></p>
        <p id="fallback-body">Body</p></section>
      <section id="late-title"><p id="before-title">Body</p>
        <title id="hidden-title"><p id="hidden-title-line">Late heading</p></title>
        <p id="after-title">Body</p></section>
      <section id="empty-title"><title id="blank-title"/><p id="after-blank-title">Body</p></section>
    </body>
    <body name="notes"><section id="note"><title><p>Notes</p></title>
      <p id="note-inline"><a l:href="#note-link">Return</a></p></section></body>
  </FictionBook>)";
  file.close();
  ASSERT_TRUE(file.good());

  const auto cache = (root / "cache").string();
  Fb2 cold(source, cache);
  ASSERT_TRUE(cold.load());
  const auto packageCache = cold.getCachePath();
  const auto sections = packageCache + "/.fb2_sections.bin";
  const auto indexPath = packageCache + "/.fb2_anchors.bin";
  uint8_t version = 0;
  std::ifstream sectionIndex(sections, std::ios::binary);
  sectionIndex.seekg(6);
  sectionIndex.read(reinterpret_cast<char*>(&version), sizeof(version));
  ASSERT_TRUE(sectionIndex.good());
  EXPECT_FALSE(std::filesystem::exists(packageCache + "/.fb2_scan_anchors.tmp"));

  const std::pair<const char*, int> targets[] = {
      {"annotation", 0}, {"annotation-paragraph", 0}, {"preamble-title", 0},
      {"preamble-title-line", 0}, {"preamble-epigraph", 0}, {"preamble-epigraph-line", 0},
      {"parent", 0}, {"chapter-title", 0}, {"title-line", 0}, {"duplicate", 0},
      {"note-link", 0}, {"parent-tail", 0}, {"child", 1}, {"unknown-wrapper", 1}, {"wrapped-paragraph", 1},
      {"table", 1}, {"row", 1},
      {"cell", 1}, {"cell-paragraph", 1}, {"fallback-paragraph", 2}, {"fallback-strong", 2},
      {"fallback-body", 2}, {"before-title", 3}, {"after-title", 3}, {"after-blank-title", 4},
      {"note-inline", 5}, {"note", 5}, {"hidden-title", -1}, {"hidden-title-line", -1},
      {"blank-title", -1}, {"missing", -1},
  };
  auto verify = [&] {
    Fb2AnchorIndex index;
    ASSERT_TRUE(index.open(sections, indexPath, version));
    for (const auto& [id, chapter] : targets) EXPECT_EQ(index.chapterForId(id), chapter) << id;
  };
  verify();

  ASSERT_TRUE(std::filesystem::remove(indexPath));
  Fb2 repair(source, cache);
  ASSERT_TRUE(repair.load());
  verify();
}
}  // namespace
