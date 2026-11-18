#include <ReleaseJsonParser.h>
#include <gtest/gtest.h>

#include <string_view>

namespace {
void feedByteByByte(ReleaseJsonParser& parser, std::string_view json) {
  for (const char byte : json) parser.feed(&byte, 1);
}

TEST(ReleaseJsonParserTest, StreamsDigestFromFirmwareAsset) {
  ReleaseJsonParser parser("firmware.bin");
  feedByteByByte(parser, R"([{"tag_name":"2.3.0","assets":[
    {"name":"firmware.bin","size":5082560,
     "digest":"sha256:8bc9cd17e18fd6c30a6e06f246fb0e2274b9f9741f358aca7697212f5017a105",
     "browser_download_url":"https://example.com/firmware.bin"},
    {"name":"partitions.bin","size":3072,"digest":"sha256:other"}]}])");
  ASSERT_TRUE(parser.foundTag());
  ASSERT_TRUE(parser.foundFirmware());
  EXPECT_STREQ(parser.getTagName(), "2.3.0");
  EXPECT_EQ(parser.getFirmwareSize(), 5082560u);
  EXPECT_STREQ(parser.getFirmwareDigest(),
               "sha256:8bc9cd17e18fd6c30a6e06f246fb0e2274b9f9741f358aca7697212f5017a105");
}

TEST(ReleaseJsonParserTest, MissingDigestAndMalformedDigestAreDistinguishable) {
  ReleaseJsonParser parser("firmware.bin");
  feedByteByByte(parser, R"({"tag_name":"1.0.0","assets":[
    {"name":"firmware.bin","size":100,"digest":"sha256:short"}]})");
  ASSERT_TRUE(parser.foundFirmware());
  EXPECT_NE(parser.getFirmwareDigest()[0], '\0');

  parser.reset();
  feedByteByByte(parser, R"({"tag_name":"1.0.0","assets":[
    {"name":"other","digest":"sha256:short"},{"name":"firmware.bin","size":100,"digest":null}]})");
  ASSERT_TRUE(parser.foundFirmware());
  EXPECT_STREQ(parser.getFirmwareDigest(), "");
}
}  // namespace

TEST(ReleaseJsonParserTest, SelectsOnlyTheRequestedDeviceAsset) {
  constexpr std::string_view release = R"({"tag_name":"2.3.2","assets":[
    {"name":"firmware.bin","browser_download_url":"https://example.org/legacy","size":100},
    {"name":"firmware-x4.bin","browser_download_url":"https://example.org/x4","size":104},
    {"name":"firmware-x4c.bin","browser_download_url":"https://example.org/old-classic","size":105},
    {"name":"firmware-x4-classic.bin","browser_download_url":"https://example.org/classic","size":106},
    {"name":"firmware-x3.bin","browser_download_url":"https://example.org/x3","size":103}
  ]})";

  ReleaseJsonParser x3("firmware-x3.bin");
  ReleaseJsonParser x4("firmware-x4.bin");
  ReleaseJsonParser classic("firmware-x4-classic.bin");
  ReleaseJsonParser missing("firmware-other.bin");
  for (auto* parser : {&x3, &x4, &classic, &missing}) {
    feedByteByByte(*parser, release);
    EXPECT_STREQ(parser->getTagName(), "2.3.2");
  }
  EXPECT_STREQ(x3.getFirmwareUrl(), "https://example.org/x3");
  EXPECT_STREQ(x4.getFirmwareUrl(), "https://example.org/x4");
  EXPECT_STREQ(classic.getFirmwareUrl(), "https://example.org/classic");
  EXPECT_EQ(x3.getFirmwareSize(), 103u);
  EXPECT_EQ(x4.getFirmwareSize(), 104u);
  EXPECT_EQ(classic.getFirmwareSize(), 106u);
  EXPECT_FALSE(missing.foundFirmware());
}
