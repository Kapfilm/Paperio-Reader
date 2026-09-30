#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

#include "CooperativeAbort.h"
#include "Fb2Encoding.h"
#include "Fb2Parser.h"
#include "FsFileReader.h"

namespace {

class CoreMemoryReader final : public IByteReader {
 public:
  explicit CoreMemoryReader(std::string data) : data_(std::move(data)) {}

  size_t read(void* dst, size_t len) override {
    const size_t available = position_ < data_.size() ? data_.size() - position_ : 0;
    const size_t count = std::min(len, available);
    if (count != 0) std::memcpy(dst, data_.data() + position_, count);
    position_ += count;
    return count;
  }

  bool seek(uint64_t pos) override {
    if (pos > data_.size()) return false;
    position_ = static_cast<size_t>(pos);
    return true;
  }

  uint64_t tell() const override { return position_; }
  uint64_t size() const override { return data_.size(); }

 private:
  std::string data_;
  size_t position_ = 0;
};

constexpr const char* kBook = R"fb2(<?xml version="1.0" encoding="UTF-8"?>
<FictionBook xmlns:l="http://www.w3.org/1999/xlink">
 <description><title-info>
  <author><first-name>Anna</first-name><last-name>Writer</last-name></author>
  <book-title>Sample Book</book-title><lang>ru</lang>
  <annotation><p>First.</p><p>Second.</p></annotation>
  <coverpage><image l:href="#cover"/></coverpage>
  <sequence name="Series" number="2"/>
 </title-info><stylesheet type="text/css">p { text-align: left; }</stylesheet></description>
 <body><section id="s1"><title><p>Chapter One</p></title>
  <p>Hello <emphasis>world</emphasis> <a l:href="#n1">note</a>.</p>
  <image l:href="#pic"/>
 </section></body>
 <body name="notes"><section id="n1"><p>Footnote text.</p></section></body>
 <binary id="cover" content-type="image/jpeg">TWE=</binary>
 <binary id="pic" content-type="image/png">QUJD</binary>
</FictionBook>)fb2";

class RecordingSink final : public Fb2ContentSink {
 public:
  void onText(const std::string& value, Fb2InlineStyle style) override {
    text += value;
    if ((static_cast<uint8_t>(style) & static_cast<uint8_t>(Fb2InlineStyle::Italic)) != 0) sawItalic = true;
  }
  void onLinkBegin(const std::string& target) override { linkTarget = target; }
  void onImage(const std::string& id) override { imageId = id; }

  std::string text;
  std::string linkTarget;
  std::string imageId;
  bool sawItalic = false;
};

TEST(NativeFb2Parser, ScansMetadataSectionsNotesAndBinaries) {
  CoreMemoryReader reader(kBook);
  Fb2ScanResult result;
  Fb2Parser parser;

  ASSERT_TRUE(parser.scan(reader, result));
  EXPECT_EQ(result.metadata.title, "Sample Book");
  EXPECT_EQ(result.metadata.author, "Anna Writer");
  EXPECT_EQ(result.metadata.language, "ru");
  EXPECT_EQ(result.metadata.coverBinaryId, "cover");
  EXPECT_EQ(result.metadata.annotationText, "First.\n\nSecond.");
  EXPECT_EQ(result.metadata.sequenceName, "Series");
  EXPECT_EQ(result.metadata.sequenceNumber, 2U);
  EXPECT_NE(result.metadata.embeddedStylesheetCss.find("text-align"), std::string::npos);
  ASSERT_EQ(result.bodies.size(), 2U);
  EXPECT_TRUE(result.bodies[0].name.empty());
  EXPECT_EQ(result.bodies[1].name, "notes");
  ASSERT_EQ(result.sections.size(), 2U);
  EXPECT_EQ(result.sectionId(result.sections[0]), "s1");
  EXPECT_EQ(result.sectionTitle(result.sections[0]), "Chapter One");
  EXPECT_EQ(result.sections[0].imageRefCount, 1U);
  EXPECT_EQ(result.sectionId(result.sections[1]), "n1");
  ASSERT_EQ(result.binaries.size(), 2U);
  EXPECT_EQ(result.binaries[0].id, "cover");
  EXPECT_EQ(result.binaries[1].contentType, "image/png");
}

TEST(NativeFb2Parser, RendersLinksStylesImagesAndDecodesBinary) {
  CoreMemoryReader reader(kBook);
  Fb2ScanResult result;
  Fb2Parser parser;
  ASSERT_TRUE(parser.scan(reader, result));

  RecordingSink sink;
  ASSERT_TRUE(parser.renderSection(reader, result.sections[0], sink));
  EXPECT_NE(sink.text.find("Hello"), std::string::npos);
  EXPECT_NE(sink.text.find("world"), std::string::npos);
  EXPECT_TRUE(sink.sawItalic);
  EXPECT_EQ(sink.linkTarget, "n1");
  EXPECT_EQ(sink.imageId, "pic");

  std::string decoded;
  ASSERT_TRUE(parser.decodeBinary(reader, result.binaries[0],
                                  [&](const uint8_t* data, size_t len) {
                                    decoded.append(reinterpret_cast<const char*>(data), len);
                                  }));
  EXPECT_EQ(decoded, "Ma");
}

TEST(NativeFb2Parser, InfersCalibreStyledFirstParagraphAsSectionTitleWithoutDuplicatingIt) {
  constexpr const char* book = R"fb2(<FictionBook><body><section><p> </p>
    <p><strong>  Chapter from Calibre  </strong></p><p>Body text.</p>
  </section></body></FictionBook>)fb2";
  CoreMemoryReader reader(book);
  Fb2ScanResult result;
  Fb2Parser parser;
  ASSERT_TRUE(parser.scan(reader, result));
  ASSERT_EQ(result.sections.size(), 1U);
  EXPECT_TRUE(result.sections[0].fallbackTitle);
  EXPECT_EQ(result.sectionTitle(result.sections[0]), "Chapter from Calibre");

  RecordingSink titleSink;
  ASSERT_TRUE(parser.renderSectionTitle(reader, result.sections[0], titleSink, 1));
  EXPECT_NE(titleSink.text.find("Chapter from Calibre"), std::string::npos);

  RecordingSink bodySink;
  ASSERT_TRUE(parser.renderSection(reader, result.sections[0], bodySink));
  EXPECT_EQ(bodySink.text.find("Chapter from Calibre"), std::string::npos);
  EXPECT_NE(bodySink.text.find("Body text."), std::string::npos);
}

class AnchorRecordingSink final : public Fb2ContentSink {
 public:
  bool streamsTableCells() const override { return true; }
  void onParagraphBegin() override { events += "<p>"; }
  void onParagraphEnd() override { events += "</p>"; }
  void onAnchor(const std::string& id) override { events += "[" + id + "]"; }
  void onText(const std::string& text, Fb2InlineStyle) override { events += text; }
  void onLinkBegin(const std::string& id) override { events += "<a:" + id + ">"; }
  void onLinkEnd() override { events += "</a>"; }
  void onTableCellBegin(const Fb2TableCellAttrs& attrs) override {
    events += attrs.isHeader ? "<th>" : "<td>";
    lastColspan = attrs.colspan;
  }
  void onTableCellEnd() override { events += "</cell>"; }
  void onTableCell(const std::string&, const Fb2TableCellAttrs&) override { ADD_FAILURE() << "Unexpected buffered cell"; }
  std::string events;
  int lastColspan = 0;
};

TEST(NativeFb2Parser, StreamsCellLinksAndAnchorsInSourceOrderWithoutNestedSectionLeak) {
  CoreMemoryReader reader(R"fb2(<FictionBook><body><section><title><p>Heading</p></title><p id="verse">A <emphasis id="word">word</emphasis></p><table><tr><td id="cell" colspan="2">Before <a href="#verse">reference</a> after</td><td id="empty"/></tr></table><section><p id="child">Hidden</p></section><p id="tail">End</p></section></body></FictionBook>)fb2");
  Fb2ScanResult result;
  Fb2Parser parser;
  ASSERT_TRUE(parser.scan(reader, result));
  AnchorRecordingSink sink;
  ASSERT_TRUE(parser.renderSection(reader, result.sections[0], sink));
  EXPECT_EQ(sink.events, "<p>[verse]A [word]word</p><td>[cell]Before <a:verse>reference</a> after</cell><td>[empty]</cell><p>[tail]End</p>");
  EXPECT_EQ(sink.lastColspan, 1);
}

TEST(NativeFb2Parser, EmitsTitleAndFallbackTitleAnchorsOnlyThroughTitleRendering) {
  for (const auto& heading : {std::string("<title id='heading'><p id='line'>Title</p></title>"),
                              std::string("<p id='line'><strong id='heading'>Title</strong></p>")}) {
    CoreMemoryReader reader("<FictionBook><body><section>" + heading + "<p id='body'>Text</p></section></body></FictionBook>");
    Fb2ScanResult result;
    Fb2Parser parser;
    ASSERT_TRUE(parser.scan(reader, result));
    AnchorRecordingSink title, body;
    ASSERT_TRUE(parser.renderSectionTitle(reader, result.sections[0], title, 1));
    ASSERT_TRUE(parser.renderSection(reader, result.sections[0], body));
    EXPECT_NE(title.events.find("[heading]"), std::string::npos);
    EXPECT_NE(title.events.find("[line]"), std::string::npos);
    EXPECT_EQ(body.events, "<p>[body]Text</p>");
  }
}

TEST(NativeFb2Parser, PreservesBufferedCellApiForExistingSinks) {
  class BufferedSink final : public Fb2ContentSink {
   public:
    void onTableCell(const std::string& value, const Fb2TableCellAttrs& attrs) override {
      text = value;
      colspan = attrs.colspan;
    }
    std::string text;
    int colspan = 0;
  } sink;
  CoreMemoryReader reader("<FictionBook><body><section><table><tr><td colspan='2'>A <strong>B</strong> C</td></tr></table></section></body></FictionBook>");
  Fb2Parser parser;
  Fb2ScanResult result;
  ASSERT_TRUE(parser.scan(reader, result));
  ASSERT_TRUE(parser.renderSection(reader, result.sections[0], sink));
  EXPECT_EQ(sink.text, "A B C");
  EXPECT_EQ(sink.colspan, 2);
}

bool alwaysAbort() { return true; }

TEST(NativeFb2Parser, UsesPaperioCooperativeAbortWithoutReaderWork) {
  CoreMemoryReader reader(kBook);
  Fb2ScanResult result;
  Fb2Parser parser;
  ASSERT_TRUE(parser.scan(reader, result));

  CooperativeAbort::clearAborted();
  CooperativeAbort::setLongTaskAbortPredicate(&alwaysAbort);
  RecordingSink sink;
  EXPECT_FALSE(parser.renderSection(reader, result.sections[0], sink));
  EXPECT_TRUE(CooperativeAbort::consumeAborted());
  CooperativeAbort::setLongTaskAbortPredicate(nullptr);
}

TEST(NativeFb2Encoding, DecodesSupportedSingleByteEncodings) {
  EXPECT_EQ(Fb2Encoding::decodeByte("windows-1251", 0xC0), 0x0410);
  EXPECT_EQ(Fb2Encoding::decodeByte("CP1251", 0xFF), 0x044F);
  EXPECT_EQ(Fb2Encoding::decodeByte("koi8_r", 0xE1), 0x0410);
  EXPECT_EQ(Fb2Encoding::decodeByte("utf-8", 0xC0), -1);
  EXPECT_EQ(Fb2Encoding::decodeByte("windows-1251", 'A'), 'A');
}

TEST(NativeFb2Io, DeviceAdapterCompilesAgainstPaperioHalFile) {
  HalFile file = HalFile::fromString("reader");
  FsFileReader reader(file);
  EXPECT_EQ(reader.size(), 6U);
  char bytes[3] = {};
  EXPECT_EQ(reader.read(bytes, sizeof(bytes)), sizeof(bytes));
  EXPECT_EQ(std::string(bytes, sizeof(bytes)), "rea");
}

class TestScanStorage final : public Fb2ScanStorage {
 public:
  TestScanStorage() : records(std::tmpfile()), strings(std::tmpfile()) {}
  ~TestScanStorage() override { if (records) std::fclose(records); if (strings) std::fclose(strings); }
  bool read(bool text, uint32_t offset, void* data, size_t size) override {
    FILE* file = text ? strings : records;
    return !failRead && file && std::fseek(file, offset, SEEK_SET) == 0 && std::fread(data, 1, size, file) == size;
  }
  bool write(bool text, uint32_t offset, const void* data, size_t size) override {
    ++writes;
    FILE* file = text ? strings : records;
    return !failWrite && file && std::fseek(file, offset, SEEK_SET) == 0 && std::fwrite(data, 1, size, file) == size;
  }
  bool failRead = false, failWrite = false;
  size_t writes = 0;
 private:
  FILE* records;
  FILE* strings;
};

TEST(NativeFb2Parser, DiskIndexKeepsAllNestedSectionsAndStringsWithBoundedWrites) {
  std::string book = "<FictionBook><body><section id='parent'><title><p>Parent</p></title><p>Before</p>";
  for (int i = 0; i < 5000; ++i) {
    const auto n = std::to_string(i);
    book += "<section id='id" + n + "'><title><p>Chapter " + n + "</p></title><p>Text</p><image href='#pic'/></section>";
  }
  book += "<p>After</p></section></body></FictionBook>";
  CoreMemoryReader reader(book);
  Fb2ScanResult result;
  auto storage = std::make_shared<TestScanStorage>();
  result.sections.storage = storage;
  Fb2Parser parser;
  ASSERT_TRUE(parser.scan(reader, result));
  ASSERT_EQ(result.sections.size(), 5001U);
  EXPECT_TRUE(result.stringPool.empty());
  EXPECT_GT(result.storedStringBytes, 28000U);
  EXPECT_EQ(result.sections[0].approxTextBytes, 11U);
  EXPECT_EQ(result.sectionId(result.sections[0]), "parent");
  EXPECT_EQ(result.sectionTitle(result.sections[0]), "Parent");
  for (int i = 0; i < 5000; ++i) {
    const auto entry = result.sections[i + 1];
    EXPECT_EQ(entry.level, 1U);
    EXPECT_EQ(entry.approxTextBytes, 4U);
    EXPECT_EQ(entry.imageRefCount, 1U);
    EXPECT_EQ(result.sectionId(entry), "id" + std::to_string(i));
    EXPECT_EQ(result.sectionTitle(entry), "Chapter " + std::to_string(i));
  }
  EXPECT_LT(storage->writes, 21000U);
}

TEST(NativeFb2Parser, DiskIndexPropagatesReadAndWriteFailures) {
  Fb2Parser parser;
  for (const bool readFailure : {false, true}) {
    CoreMemoryReader reader("<FictionBook><body><section><section><p>Child</p></section><p>Parent</p></section></body></FictionBook>");
    Fb2ScanResult result;
    auto storage = std::make_shared<TestScanStorage>();
    storage->failWrite = !readFailure;
    storage->failRead = readFailure;
    result.sections.storage = storage;
    EXPECT_FALSE(parser.scan(reader, result));
    EXPECT_FALSE(result.good());
  }
}

TEST(NativeFb2Parser, DiskIndexClosesEmptySectionsAndKeepsSiblingDepth) {
  CoreMemoryReader reader("<FictionBook><body><section id='empty'/><section id='next'><title/><p>Body</p></section></body></FictionBook>");
  Fb2ScanResult result;
  result.sections.storage = std::make_shared<TestScanStorage>();
  Fb2Parser parser;
  ASSERT_TRUE(parser.scan(reader, result));
  ASSERT_EQ(result.sections.size(), 2U);
  EXPECT_EQ(result.sections[0].approxTextBytes, 0U);
  EXPECT_EQ(result.sections[1].level, 0U);
  EXPECT_EQ(result.sections[1].approxTextBytes, 4U);
  EXPECT_EQ(result.sectionId(result.sections[1]), "next");
}

TEST(NativeFb2Parser, RejectsExcessiveNestingBeforeAllocatingUnboundedStack) {
  std::string xml = "<FictionBook><body>";
  for (int i = 0; i < 33; ++i) xml += "<section>";
  for (int i = 0; i < 33; ++i) xml += "</section>";
  xml += "</body></FictionBook>";
  CoreMemoryReader reader(xml);
  Fb2ScanResult result;
  result.sections.storage = std::make_shared<TestScanStorage>();
  Fb2Parser parser;
  EXPECT_FALSE(parser.scan(reader, result));
  EXPECT_EQ(result.sections.size(), 32U);
}

TEST(NativeFb2Parser, DiskIndexDetectsFinalFlushFailureAndStringOffsetOverflow) {
  Fb2ScanResult result;
  auto storage = std::make_shared<TestScanStorage>();
  result.sections.storage = storage;
  result.sections.push_back({});
  storage->failWrite = true;
  EXPECT_FALSE(result.good());
  storage->failWrite = false;
  uint32_t offset = 0;
  uint16_t length = 0;
  result.storedStringBytes = UINT32_MAX - 1;
  EXPECT_FALSE(result.appendString("abc", 3, offset, length));
  EXPECT_EQ(offset, UINT32_MAX);
  EXPECT_EQ(length, 0);
}

TEST(NativeFb2Parser, DiskIndexActualRegressionBook) {
  const char* path = std::getenv("FB2_REGRESSION_BOOK");
  if (!path) GTEST_SKIP() << "Set FB2_REGRESSION_BOOK to test a local book";
  std::ifstream file(path, std::ios::binary);
  ASSERT_TRUE(file.good());
  CoreMemoryReader reader(std::string(std::istreambuf_iterator<char>(file), {}));
  Fb2ScanResult result;
  result.sections.storage = std::make_shared<TestScanStorage>();
  Fb2Parser parser;
  ASSERT_TRUE(parser.scan(reader, result, 4096));
  EXPECT_TRUE(result.stringPool.empty());
  EXPECT_FALSE(result.sections.empty());
  for (const auto& section : result.sections) {
    EXPECT_EQ(result.sectionId(section).size(), section.idLength);
    EXPECT_EQ(result.sectionTitle(section).size(), section.titleLength);
  }
  if (const char* expectedPath = std::getenv("FB2_REGRESSION_EXPECTED_TSV")) {
    std::ifstream expected(expectedPath);
    ASSERT_TRUE(expected.good());
    std::string line;
    size_t index = 0;
    while (std::getline(expected, line)) {
      const auto tab = line.find('\t');
      ASSERT_NE(tab, std::string::npos);
      ASSERT_LT(index, result.sections.size());
      const auto entry = result.sections[index];
      EXPECT_EQ(result.sectionId(entry), line.substr(0, tab)) << index;
      EXPECT_EQ(result.sectionTitle(entry), line.substr(tab + 1)) << index;
      ++index;
    }
    EXPECT_EQ(index, result.sections.size());
  }
  EXPECT_TRUE(result.good());
  std::cout << "Indexed " << result.sections.size() << " sections; disk strings=" << result.storedStringBytes << std::endl;
}

}  // namespace
