#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Preferences.h>
#include <SecureHttpClient.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "DropboxClient.h"
#include "DropboxStore.h"

using namespace crosspoint;
namespace {
constexpr char kTokenReply[] =
    R"({"access_token":"test-access-token","refresh_token":"test-refresh-token","expires_in":14400})";
class DropboxTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dropboxTestMillis = 0;
    dropboxTestNvsFail = false;
    dropboxTestNvs.clear();
    dropboxTestReplies.clear();
    dropboxTestRequests.clear();
    dropboxTestStrictTls = true;
    dropboxTestWriteFail = false;
    dropboxTestFree = 1024 * 1024;
    dropboxTestRoot = std::filesystem::temp_directory_path() / "paperio-dropbox-client-test";
    std::filesystem::remove_all(dropboxTestRoot);
    std::filesystem::create_directories(dropboxTestRoot);
    auto& store = DropboxStore::instance();
    ASSERT_TRUE(store.load());
    ASSERT_TRUE(store.saveConfig("paperiotestkey", "", "/Dropbox"));
    DropboxClient::cancelAuthorization();
  }
  void TearDown() override { std::filesystem::remove_all(dropboxTestRoot); }
  void link() {
    ASSERT_FALSE(DropboxClient::beginAuthorization().empty());
    dropboxTestReplies.push_back({200, kTokenReply});
    ASSERT_TRUE(DropboxClient::completeAuthorization("test-code").ok());
    dropboxTestRequests.clear();
  }
  DropboxClient::Entry entry() { return {"Книга.fb2.zip", "id:abc-123", 5}; }
  size_t localFiles() {
    if (!std::filesystem::exists(dropboxTestRoot / "Dropbox")) return 0;
    return std::distance(std::filesystem::directory_iterator(dropboxTestRoot / "Dropbox"),
                         std::filesystem::directory_iterator());
  }
};

TEST_F(DropboxTest, PkceUsesRfc7636S256AndOfflineReadOnlyScopes) {
  const auto url = DropboxClient::beginAuthorization();
  EXPECT_NE(url.find("code_challenge=E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"), std::string::npos);
  EXPECT_NE(url.find("token_access_type=offline"), std::string::npos);
  EXPECT_NE(url.find("code_challenge_method=S256"), std::string::npos);
  EXPECT_NE(url.find("scope=files.metadata.read%20files.content.read"), std::string::npos);
  EXPECT_EQ(url.find("redirect_uri"), std::string::npos);
  dropboxTestReplies.push_back({200, kTokenReply});
  ASSERT_TRUE(DropboxClient::completeAuthorization("test-code").ok());
  ASSERT_EQ(dropboxTestRequests.size(), 1u);
  EXPECT_NE(dropboxTestRequests[0].body.find("code_verifier=dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"),
            std::string::npos);
  EXPECT_EQ(dropboxTestRequests[0].body.find("client_secret"), std::string::npos);
  EXPECT_TRUE(dropboxTestStrictTls);
}

TEST_F(DropboxTest, AuthorizationExpiresAndCannotSurviveAppKeyChange) {
  ASSERT_FALSE(DropboxClient::beginAuthorization().empty());
  dropboxTestMillis = 600000;
  EXPECT_FALSE(DropboxClient::authorizationPending());
  EXPECT_EQ(DropboxClient::completeAuthorization("code").error, DropboxClient::Error::Auth);
  ASSERT_FALSE(DropboxClient::beginAuthorization().empty());
  ASSERT_TRUE(DropboxStore::instance().saveConfig("anotherkey", "", "/Dropbox"));
  EXPECT_FALSE(DropboxClient::authorizationPending());
  EXPECT_TRUE(dropboxTestRequests.empty());
}

TEST_F(DropboxTest, NvsFailureDoesNotChangeExistingCredentialsOrSettings) {
  link();
  const auto saved = dropboxTestNvs;
  dropboxTestNvsFail = true;
  EXPECT_FALSE(DropboxStore::instance().saveConfig("changedkey", "/new", "/Books"));
  EXPECT_TRUE(DropboxStore::instance().linked());
  EXPECT_EQ(DropboxStore::instance().appKey(), "paperiotestkey");
  EXPECT_EQ(dropboxTestNvs, saved);
  EXPECT_FALSE(DropboxStore::instance().unlink());
  EXPECT_TRUE(DropboxStore::instance().linked());
}

TEST_F(DropboxTest, FolderValidationPreventsSdCacheAndTraversalDestinations) {
  auto& store = DropboxStore::instance();
  EXPECT_FALSE(store.saveConfig("key", "/../Books", "/Dropbox"));
  EXPECT_FALSE(store.saveConfig("key", "", "/.crosspoint/books"));
  EXPECT_FALSE(store.saveConfig("key", "", "/.CROSSPOINT/books"));
  EXPECT_FALSE(store.saveConfig("key", "", "/.crosspoint./books"));
  EXPECT_FALSE(store.saveConfig("key", "", "/Books /other"));
  EXPECT_FALSE(store.saveConfig("key", "", "/Books/../other"));
  EXPECT_FALSE(store.saveConfig("key\r\n", "", "/Books"));
  EXPECT_TRUE(store.saveConfig("key", "/", "/Books/"));
  EXPECT_EQ(store.remoteFolder(), "");
  EXPECT_EQ(store.downloadFolder(), "/Books");
}

TEST_F(DropboxTest, ListIncludesFoldersFiltersNonBooksAndPreservesCursorOnEmptyPage) {
  link();
  dropboxTestReplies.push_back(
      {200,
       R"({"entries":[{".tag":"folder","name":"folder","id":"id:folder"},{".tag":"file","id":"id:abc","name":"Книга.FB2.ZIP","size":5},{".tag":"file","id":"id:image","name":"photo.jpg","size":4}],"cursor":"first-page","has_more":true})"});
  DropboxClient client;
  DropboxClient::Page page;
  ASSERT_TRUE(client.list(page).ok());
  ASSERT_EQ(page.books.size(), 2u);
  EXPECT_TRUE(page.books[0].isFolder);
  EXPECT_EQ(page.books[0].id, "id:folder");
  EXPECT_FALSE(page.books[1].isFolder);
  EXPECT_EQ(page.books[1].name, "Книга.FB2.ZIP");
  EXPECT_EQ(page.cursor, "first-page");
  EXPECT_TRUE(page.hasMore);
  JsonDocument request;
  ASSERT_FALSE(deserializeJson(request, dropboxTestRequests.back().body));
  EXPECT_FALSE(request["recursive"].as<bool>());
  EXPECT_EQ(request["limit"].as<int>(), 25);
  dropboxTestReplies.push_back({200, R"({"entries":[],"cursor":"second-page","has_more":true})"});
  ASSERT_TRUE(client.list(page, "first-page").ok());
  EXPECT_TRUE(page.books.empty());
  EXPECT_TRUE(page.hasMore);
  EXPECT_EQ(page.cursor, "second-page");
  EXPECT_NE(dropboxTestRequests.back().url.find("/continue"), std::string::npos);
}

TEST_F(DropboxTest, NestedFoldersUseAsciiIdsAndContinueTheirOwnCursor) {
  link();
  DropboxClient client;
  DropboxClient::Page page;
  dropboxTestReplies.push_back(
      {200,
       R"({"entries":[{".tag":"file","id":"id:book","name":"Книга.epub","size":5},{".tag":"folder","id":"id:child-2","name":"Другие книги"}],"cursor":"nested-first","has_more":true})"});
  ASSERT_TRUE(client.list(page, "", "id:parent-1").ok());
  ASSERT_EQ(page.books.size(), 2u);
  EXPECT_TRUE(page.books[0].isFolder);
  EXPECT_EQ(page.books[0].name, "Другие книги");
  EXPECT_EQ(page.books[0].size, 0u);
  EXPECT_FALSE(page.books[1].isFolder);
  JsonDocument request;
  ASSERT_FALSE(deserializeJson(request, dropboxTestRequests.back().body));
  EXPECT_EQ(request["path"].as<std::string>(), "id:parent-1");
  EXPECT_FALSE(request["recursive"].as<bool>());
  dropboxTestReplies.push_back({200, R"({"entries":[],"cursor":"nested-last","has_more":false})"});
  ASSERT_TRUE(client.list(page, "nested-first", "id:parent-1").ok());
  ASSERT_FALSE(deserializeJson(request, dropboxTestRequests.back().body));
  EXPECT_EQ(request["cursor"].as<std::string>(), "nested-first");
  EXPECT_TRUE(request["path"].isNull());
  EXPECT_NE(dropboxTestRequests.back().url.find("/continue"), std::string::npos);
}

TEST_F(DropboxTest, DefaultListingStillUsesConfiguredRootAndInvalidFolderIdsDoNotRequest) {
  link();
  ASSERT_TRUE(DropboxStore::instance().saveConfig("paperiotestkey", "/Мои книги", "/Dropbox"));
  DropboxClient client;
  DropboxClient::Page page;
  dropboxTestReplies.push_back({200, R"({"entries":[],"cursor":"root-last","has_more":false})"});
  ASSERT_TRUE(client.list(page).ok());
  JsonDocument request;
  ASSERT_FALSE(deserializeJson(request, dropboxTestRequests.back().body));
  EXPECT_EQ(request["path"].as<std::string>(), "/Мои книги");
  dropboxTestRequests.clear();
  for (const auto* invalid : {"/outside", "../parent", "id:bad/child", "id:", "ns:123"}) {
    EXPECT_EQ(client.list(page, "", invalid).error, DropboxClient::Error::InvalidData);
  }
  EXPECT_TRUE(dropboxTestRequests.empty());
}

TEST_F(DropboxTest, FolderEntriesNeedValidNamesAndIdsButNeverBookSizes) {
  link();
  DropboxClient client;
  DropboxClient::Page page;
  dropboxTestReplies.push_back(
      {200,
       R"({"entries":[{".tag":"folder","id":"id:valid","name":"Серия.fb2"},{".tag":"folder","id":"id:empty","name":""},{".tag":"folder","id":"id:control","name":"bad\nname"},{".tag":"folder","id":"../../escape","name":"Bad"},{".tag":"deleted","id":"id:deleted","name":"Gone"},{".tag":"file","id":"id:nondownloadable","name":"No.epub","size":5,"is_downloadable":false}],"cursor":"last","has_more":false})"});
  ASSERT_TRUE(client.list(page).ok());
  ASSERT_EQ(page.books.size(), 1u);
  EXPECT_TRUE(page.books[0].isFolder);
  EXPECT_EQ(page.books[0].id, "id:valid");
  EXPECT_EQ(page.books[0].size, 0u);
  dropboxTestRequests.clear();
  std::string saved;
  EXPECT_EQ(client.download(page.books[0], saved).error, DropboxClient::Error::InvalidData);
  EXPECT_TRUE(saved.empty());
  EXPECT_TRUE(dropboxTestRequests.empty());
  EXPECT_EQ(localFiles(), 0u);
}

TEST_F(DropboxTest, JsonLimitAndNonAdvancingCursorFailSafely) {
  link();
  DropboxClient client;
  DropboxClient::Page page;
  dropboxTestReplies.push_back({200, std::string(32769, ' ')});
  EXPECT_EQ(client.list(page).error, DropboxClient::Error::TooLarge);
  dropboxTestReplies.push_back({200, R"({"entries":[],"cursor":"same","has_more":true})"});
  EXPECT_EQ(client.list(page, "same").error, DropboxClient::Error::InvalidData);
}

TEST_F(DropboxTest, UnauthorizedListRefreshesOnceAndNeverDropsAccountOnNetworkFailure) {
  link();
  DropboxClient client;
  DropboxClient::Page page;
  dropboxTestReplies.push_back({401, "unauthorized"});
  dropboxTestReplies.push_back({200, R"({"access_token":"new-access","expires_in":14400})"});
  dropboxTestReplies.push_back({200, R"({"entries":[],"cursor":"done","has_more":false})"});
  EXPECT_TRUE(client.list(page).ok());
  ASSERT_EQ(dropboxTestRequests.size(), 3u);
  EXPECT_NE(dropboxTestRequests[1].body.find("grant_type=refresh_token"), std::string::npos);
  EXPECT_EQ(dropboxTestRequests[2].headers["Authorization"], "Bearer new-access");
  dropboxTestMillis += 14400000;
  EXPECT_EQ(client.list(page).error, DropboxClient::Error::Network);
  EXPECT_TRUE(DropboxStore::instance().linked());
}

TEST_F(DropboxTest, DownloadStreamsByAsciiIdAndCommitsExactFileOnly) {
  link();
  dropboxTestReplies.push_back({200, "12345"});
  DropboxClient client;
  std::string saved;
  EXPECT_TRUE(client.download(entry(), saved).ok());
  EXPECT_EQ(saved, "/Dropbox/Книга.fb2.zip");
  ASSERT_EQ(dropboxTestRequests.size(), 1u);
  EXPECT_EQ(dropboxTestRequests[0].headers["Dropbox-API-Arg"], R"({"path":"id:abc-123"})");
  EXPECT_EQ(localFiles(), 1u);
  std::ifstream f(dropboxTestRoot / "Dropbox/Книга.fb2.zip");
  std::string data((std::istreambuf_iterator<char>(f)), {});
  EXPECT_EQ(data, "12345");
  EXPECT_TRUE(dropboxTestStrictTls);
}

TEST_F(DropboxTest, FailedTruncatedOrOversizedDownloadsLeaveNoBookOrPartFile) {
  link();
  DropboxClient client;
  std::string saved;
  for (const auto& reply :
       std::vector<DropboxTestReply>{{200, "123"}, {200, "123456"}, {500, "error"}, {200, "12345", -7}}) {
    dropboxTestReplies.push_back(reply);
    EXPECT_FALSE(client.download(entry(), saved).ok());
    EXPECT_TRUE(saved.empty());
    EXPECT_EQ(localFiles(), 0u);
  }
}

TEST_F(DropboxTest, ExistingFilesAndInsufficientSpaceAreNotTouched) {
  link();
  DropboxClient client;
  std::string saved;
  std::filesystem::create_directories(dropboxTestRoot / "Dropbox");
  std::ofstream(dropboxTestRoot / "Dropbox/Книга.fb2.zip") << "original";
  EXPECT_EQ(client.download(entry(), saved).error, DropboxClient::Error::AlreadyExists);
  EXPECT_TRUE(dropboxTestRequests.empty());
  std::filesystem::remove(dropboxTestRoot / "Dropbox/Книга.fb2.zip");
  dropboxTestFree = 0;
  EXPECT_EQ(client.download(entry(), saved).error, DropboxClient::Error::File);
  EXPECT_TRUE(dropboxTestRequests.empty());
}

TEST_F(DropboxTest, CancellationWriteFailureAndRateLimitCleanTemporaryFiles) {
  link();
  DropboxClient client;
  std::string saved;
  dropboxTestReplies.push_back({200, "12345"});
  EXPECT_EQ(client.download(entry(), saved, [](size_t bytes, size_t) { return bytes == 0; }).error,
            DropboxClient::Error::Cancelled);
  EXPECT_EQ(localFiles(), 0u);
  dropboxTestReplies.push_back({200, "12345"});
  dropboxTestWriteFail = true;
  EXPECT_EQ(client.download(entry(), saved).error, DropboxClient::Error::File);
  EXPECT_EQ(localFiles(), 0u);
  dropboxTestWriteFail = false;
  dropboxTestReplies.push_back({429, "slow down"});
  EXPECT_EQ(client.download(entry(), saved).error, DropboxClient::Error::RateLimited);
  EXPECT_EQ(localFiles(), 0u);
}

TEST_F(DropboxTest, DownloadUnauthorizedResponseIsNeverWrittenBeforeSingleRefreshRetry) {
  link();
  dropboxTestReplies.push_back({401, "not a book"});
  dropboxTestReplies.push_back({200, R"({"access_token":"retry-access","expires_in":14400})"});
  dropboxTestReplies.push_back({200, "12345"});
  DropboxClient client;
  std::string saved;
  ASSERT_TRUE(client.download(entry(), saved).ok());
  ASSERT_EQ(dropboxTestRequests.size(), 3u);
  EXPECT_EQ(dropboxTestRequests[2].headers["Authorization"], "Bearer retry-access");
  EXPECT_EQ(localFiles(), 1u);
}

TEST_F(DropboxTest, InvalidAuthorizationResponseCannotOverwriteWorkingAccount) {
  link();
  const auto good = dropboxTestNvs;
  ASSERT_FALSE(DropboxClient::beginAuthorization().empty());
  dropboxTestReplies.push_back(
      {200, R"({"access_token":"bad\r\nheader","refresh_token":"other-token","expires_in":14400})"});
  EXPECT_EQ(DropboxClient::completeAuthorization("new-code").error, DropboxClient::Error::InvalidData);
  EXPECT_EQ(dropboxTestNvs, good);
  EXPECT_TRUE(DropboxStore::instance().linked());
}

TEST_F(DropboxTest, ReentrantClientCallsAreBusyDuringDownload) {
  link();
  DropboxClient client;
  std::string saved;
  bool checked = false;
  dropboxTestReplies.push_back({200, "12345"});
  EXPECT_TRUE(client
                  .download(entry(), saved,
                            [&](size_t, size_t) {
                              DropboxClient::Page page;
                              checked = true;
                              EXPECT_EQ(client.list(page).error, DropboxClient::Error::Busy);
                              return true;
                            })
                  .ok());
  EXPECT_TRUE(checked);
}
}  // namespace
