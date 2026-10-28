#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "KOReaderProgressReply.h"

namespace {
constexpr char hash[] = "0123456789abcdef0123456789abcdef";
using Client = KOReaderSyncClient;
Client::Error parse(const std::string& body, KOReaderProgress& out) {
  return koreader_reply::parseProgressReply(body, hash, out);
}
}  // namespace

TEST(KOReaderProgressReply, FullRecordPreservesLocatorAndDevice) {
  KOReaderProgress out;
  ASSERT_EQ(
      parse(
          R"({"progress":"/body/DocFragment[12]/body/p[3]/text().42","percentage":0.4567,"device":"KOReader","device_id":"a1","timestamp":1700000000})",
          out),
      Client::OK);
  EXPECT_EQ(out.document, hash);
  EXPECT_EQ(out.progress, "/body/DocFragment[12]/body/p[3]/text().42");
  EXPECT_FLOAT_EQ(out.percentage, 0.4567f);
  EXPECT_EQ(out.device, "KOReader");
  EXPECT_EQ(out.deviceId, "a1");
  EXPECT_EQ(out.timestamp, 1700000000);
}
TEST(KOReaderProgressReply, EmptyConventionsAreNotFound) {
  for (const char* body : {"", " \r\n\t", "{}", "[]", "\"\"", "null", R"({"progress":null})", R"({"progress":""})",
                           R"({"position_kinds_available":["percentage"]})"}) {
    KOReaderProgress out;
    EXPECT_EQ(parse(body, out), Client::NOT_FOUND) << body;
  }
}
TEST(KOReaderProgressReply, PercentageOnlyRecordsIncludeStartAndEnd) {
  for (const char* body :
       {R"({"percentage":0})", R"({"progress":null,"percentage":0.25})", R"({"progress":"","percentage":1})"}) {
    KOReaderProgress out;
    ASSERT_EQ(parse(body, out), Client::OK) << body;
    EXPECT_TRUE(out.progress.empty());
    EXPECT_TRUE(std::isfinite(out.percentage));
  }
}
TEST(KOReaderProgressReply, LocatorOnlyDoesNotInventZeroPercentage) {
  KOReaderProgress out;
  ASSERT_EQ(parse(R"({"progress":"/body/p[2]"})", out), Client::OK);
  EXPECT_EQ(out.progress, "/body/p[2]");
  EXPECT_TRUE(std::isnan(out.percentage));
}
TEST(KOReaderProgressReply, OutOfRangeAndWrongPercentageTypesAreRejected) {
  for (const char* body :
       {R"({"percentage":-0.01})", R"({"percentage":1.01})", R"({"percentage":1e100})", R"({"percentage":"0.5"})",
        R"({"percentage":true})", R"({"progress":"/body/p[2]","percentage":[]})"}) {
    KOReaderProgress out;
    EXPECT_EQ(parse(body, out), Client::INVALID_RESPONSE) << body;
  }
}
TEST(KOReaderProgressReply, WrongLocatorAndNonemptyScalarAreRejected) {
  for (const char* body : {R"({"progress":4,"percentage":0.4})", R"({"progress":{},"percentage":0.4})", "true", "42",
                           "[1]", "\"not a record\""}) {
    KOReaderProgress out;
    EXPECT_EQ(parse(body, out), Client::INVALID_RESPONSE) << body;
  }
}
TEST(KOReaderProgressReply, MissingDeviceFieldsAreEmptyAndStaleMetadataIsCleared) {
  KOReaderProgress out;
  out.metadata = KOReaderMetadata{"old", "title", "authors"};
  ASSERT_EQ(parse(R"({"percentage":0.2})", out), Client::OK);
  EXPECT_TRUE(out.device.empty());
  EXPECT_TRUE(out.deviceId.empty());
  EXPECT_FALSE(out.metadata);
}
TEST(KOReaderProgressReply, FailedParseLeavesPreviousOutputUntouched) {
  KOReaderProgress out;
  out.progress = "old";
  out.percentage = 0.8f;
  EXPECT_EQ(parse(R"({"percentage":2})", out), Client::INVALID_RESPONSE);
  EXPECT_EQ(out.progress, "old");
  EXPECT_FLOAT_EQ(out.percentage, 0.8f);
  EXPECT_EQ(parse(R"({"progress":)", out), Client::JSON_ERROR);
  EXPECT_EQ(out.progress, "old");
}
TEST(KOReaderProgressReply, BomWhitespaceAndShortPrefixesAreSafe) {
  KOReaderProgress out;
  EXPECT_EQ(parse(std::string("\xEF\xBB\xBF \n") + R"({"percentage":0.4})", out), Client::OK);
  EXPECT_EQ(parse(std::string("\xEF\xBB\xBF \n"), out), Client::NOT_FOUND);
  EXPECT_EQ(parse(std::string("\xEF"), out), Client::JSON_ERROR);
}
TEST(KOReaderProgressReply, StatusHandlingAcceptsBodilessSuccessWithoutFalseProgress) {
  KOReaderProgress out;
  EXPECT_EQ(koreader_reply::parseProgressHttpReply(200, "", hash, out), Client::NOT_FOUND);
  EXPECT_EQ(koreader_reply::parseProgressHttpReply(204, "", hash, out), Client::NOT_FOUND);
  EXPECT_EQ(koreader_reply::parseProgressHttpReply(202, R"({"percentage":0.2})", hash, out), Client::OK);
  EXPECT_EQ(koreader_reply::parseProgressHttpReply(401, "{}", hash, out), Client::AUTH_FAILED);
  EXPECT_EQ(koreader_reply::parseProgressHttpReply(404, "{}", hash, out), Client::NOT_FOUND);
  EXPECT_EQ(koreader_reply::parseProgressHttpReply(302, "", hash, out), Client::REDIRECT_ERROR);
  EXPECT_EQ(koreader_reply::parseProgressHttpReply(500, "oops", hash, out), Client::SERVER_ERROR);
}
TEST(KOReaderProgressReply, HtmlPortalIsNotAStoredPositionOrAuthFailure) {
  KOReaderProgress out;
  for (int status : {200, 401, 404})
    EXPECT_EQ(koreader_reply::parseProgressHttpReply(status, " \n<html>sign in</html>", hash, out),
              Client::INVALID_RESPONSE);
}
