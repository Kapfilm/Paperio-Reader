#include <ReleaseJsonParser.h>
#include <gtest/gtest.h>

#include <string_view>

namespace {
void feedByteByByte(ReleaseJsonParser& parser, std::string_view json) {
  for (const char byte : json) parser.feed(&byte, 1);
}

TEST(ReleaseJsonParserTest, StreamsDigestFromFirmwareAsset) {
  ReleaseJsonParser parser;
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
  ReleaseJsonParser parser;
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
