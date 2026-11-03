#include "DropboxClient.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <CrossPointRoots.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <SecureHttpClient.h>
#include <esp_random.h>
#include <wolfssl/wolfcrypt/sha256.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <limits>

#include "DropboxStore.h"

namespace {
using Error = DropboxClient::Error;
using Result = DropboxClient::Result;
constexpr size_t kMaxReply = 32768;
constexpr size_t kMaxTokenReply = 8192;
constexpr uint32_t kAuthorizationLifetime = 10 * 60 * 1000;
std::atomic_flag busy = ATOMIC_FLAG_INIT;
struct Guard {
  bool held = !busy.test_and_set(std::memory_order_acquire);
  ~Guard() {
    if (held) busy.clear(std::memory_order_release);
  }
};
std::string verifier;
std::string pendingKey;
uint32_t authorizationStarted = 0;
std::string accessToken;
std::string accessKey;
uint32_t accessExpires = 0;

std::string encode(const std::string& value) {
  static constexpr char hex[] = "0123456789ABCDEF";
  std::string result;
  for (unsigned char c : value) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
      result += c;
    else {
      result += '%';
      result += hex[c >> 4];
      result += hex[c & 15];
    }
  }
  return result;
}

std::string base64Url(const uint8_t* bytes, size_t length) {
  static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string result;
  uint32_t bits = 0;
  int count = 0;
  for (size_t i = 0; i < length; ++i) {
    bits = (bits << 8) | bytes[i];
    count += 8;
    while (count >= 6) {
      count -= 6;
      result += alphabet[(bits >> count) & 63];
    }
  }
  if (count) result += alphabet[(bits << (6 - count)) & 63];
  return result;
}

bool safeToken(const std::string& token) {
  return !token.empty() && token.size() <= 4096 &&
         std::all_of(token.begin(), token.end(), [](unsigned char c) { return c > 32 && c < 127; });
}

bool safeId(const std::string& id) {
  return id.rfind("id:", 0) == 0 && id.size() > 3 && id.size() <= 128 &&
         std::all_of(id.begin() + 3, id.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; });
}

bool bookName(const std::string& name) {
  if (name.empty() || name.size() > 240 || name == "." || name == "..") return false;
  std::string lower = name;
  for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (const char* suffix : {".epub", ".fb2", ".fb2.zip", ".txt", ".xtc", ".xtch", ".md", ".markdown"}) {
    const size_t length = std::char_traits<char>::length(suffix);
    if (lower.size() >= length && lower.compare(lower.size() - length, length, suffix) == 0) return true;
  }
  return false;
}

std::string localName(const std::string& source) {
  std::string name = source;
  for (char& c : name) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u < 32 || u == 127 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
        c == '>' || c == '|')
      c = '_';
  }
  while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
  if (name.empty() || name == "." || name == ".." || name.front() == '.') return {};
  return name;
}

Result statusResult(int status) {
  if (status == 200) return {};
  if (status == 401 || status == 400) return {Error::Auth, status};
  if (status == 429) return {Error::RateLimited, status};
  if (status < 0) return {Error::Network, status};
  return {Error::Api, status};
}

bool configure(crosspoint::SecureHttpClient& http) {
  // Unlike general book browsing, Dropbox never downgrades certificate checks.
  if (time(nullptr) < 1735689600 || HalClock::isApproximate()) {
    if (!HalClock::syncNtp()) return false;
  }
  http.setCACert(CROSSPOINT_ROOTS_PEM);
  http.setAllowInsecureFallback(false);
  http.setAllowCertificateDateErrors(false);
  http.setMaxRedirects(0);
  http.setTimeout(30000);
  http.setUserAgent("PaperioReader-Dropbox");
  return true;
}

Result smallPost(crosspoint::SecureHttpClient& http, const std::string& url, const std::string& body,
                 std::string& response, size_t maximum = kMaxReply) {
  response.clear();
  bool overflow = false;
  const int status = http.post(url, body, [&](const uint8_t* bytes, size_t length) {
    if (length > maximum - response.size()) {
      overflow = true;
      return false;
    }
    response.append(reinterpret_cast<const char*>(bytes), length);
    return true;
  });
  if (overflow) return {Error::TooLarge, http.lastStatus()};
  return statusResult(status);
}

bool parseAccess(JsonDocument& doc, std::string& token, uint32_t& lifetime) {
  if (!doc["access_token"].is<const char*>() || !doc["expires_in"].is<uint32_t>()) return false;
  token = doc["access_token"].as<std::string>();
  lifetime = doc["expires_in"].as<uint32_t>();
  return safeToken(token) && lifetime > 0 && lifetime <= 86400;
}

void rememberAccess(const std::string& key, std::string token, uint32_t lifetime) {
  accessKey = key;
  accessToken = std::move(token);
  // Refresh early, never use a token after its server-advertised lifetime.
  const uint32_t usable = lifetime > 30 ? lifetime - 30 : lifetime;
  accessExpires = millis() + usable * 1000;
}

Result ensureAccess(const std::string& refresh, bool force = false) {
  const auto& store = DropboxStore::instance();
  if (!store.configured()) return {Error::NotConfigured};
  if (!store.linked()) {
    accessToken.clear();
    return {Error::NotLinked};
  }
  if (!force && accessKey == store.appKey() && !accessToken.empty() &&
      static_cast<int32_t>(accessExpires - millis()) > 0)
    return {};
  crosspoint::SecureHttpClient http;
  if (!configure(http)) return {Error::Network};
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  std::string response;
  const auto result =
      smallPost(http, "https://api.dropboxapi.com/oauth2/token",
                "grant_type=refresh_token&client_id=" + encode(store.appKey()) + "&refresh_token=" + encode(refresh),
                response, kMaxTokenReply);
  if (!result.ok()) return result;  // A network failure never unlinks a good account.
  JsonDocument doc;
  if (deserializeJson(doc, response) || !doc.is<JsonObject>()) return {Error::InvalidData};
  std::string token;
  uint32_t lifetime = 0;
  if (!parseAccess(doc, token, lifetime)) return {Error::InvalidData};
  rememberAccess(store.appKey(), std::move(token), lifetime);
  return {};
}

bool pending() {
  if (verifier.empty()) return false;
  if (static_cast<uint32_t>(millis() - authorizationStarted) >= kAuthorizationLifetime ||
      pendingKey != DropboxStore::instance().appKey()) {
    verifier.clear();
    pendingKey.clear();
    return false;
  }
  return true;
}
}  // namespace

std::string DropboxClient::beginAuthorization() {
  Guard guard;
  auto& store = DropboxStore::instance();
  if (!guard.held || !store.configured()) return {};
  uint8_t random[32];
  esp_fill_random(random, sizeof(random));
  verifier = base64Url(random, sizeof(random));
  uint8_t digest[32];
  wc_Sha256 sha;
  if (wc_InitSha256(&sha) != 0) {
    verifier.clear();
    return {};
  }
  const bool hashed = wc_Sha256Update(&sha, reinterpret_cast<const uint8_t*>(verifier.data()), verifier.size()) == 0 &&
                      wc_Sha256Final(&sha, digest) == 0;
  wc_Sha256Free(&sha);
  if (!hashed) {
    verifier.clear();
    return {};
  }
  pendingKey = store.appKey();
  authorizationStarted = millis();
  // No redirect URI: Dropbox shows the one-time code to paste into the setup
  // page. Only this device knows the verifier required to exchange that code.
  return "https://www.dropbox.com/oauth2/authorize?response_type=code&token_access_type=offline&client_id=" +
         encode(pendingKey) + "&code_challenge_method=S256&code_challenge=" + base64Url(digest, sizeof(digest)) +
         "&scope=files.metadata.read%20files.content.read";
}

bool DropboxClient::authorizationPending() {
  Guard guard;
  return guard.held && pending();
}

void DropboxClient::cancelAuthorization() {
  Guard guard;
  if (guard.held) {
    verifier.clear();
    pendingKey.clear();
  }
}

DropboxClient::Result DropboxClient::completeAuthorization(const std::string& code) {
  Guard guard;
  if (!guard.held) return {Error::Busy};
  if (!pending() || code.empty() || code.size() > 1024 ||
      std::any_of(code.begin(), code.end(), [](unsigned char c) { return c <= 32 || c >= 127; }))
    return {Error::Auth};
  crosspoint::SecureHttpClient http;
  if (!configure(http)) return {Error::Network};
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  std::string response;
  auto result = smallPost(http, "https://api.dropboxapi.com/oauth2/token",
                          "grant_type=authorization_code&client_id=" + encode(pendingKey) + "&code=" + encode(code) +
                              "&code_verifier=" + encode(verifier),
                          response, kMaxTokenReply);
  if (!result.ok()) return result;
  JsonDocument doc;
  if (deserializeJson(doc, response) || !doc.is<JsonObject>() || !doc["refresh_token"].is<const char*>())
    return {Error::InvalidData};
  std::string token;
  uint32_t lifetime = 0;
  if (!parseAccess(doc, token, lifetime)) return {Error::InvalidData};
  auto& store = DropboxStore::instance();
  if (!store.saveRefreshToken(doc["refresh_token"].as<std::string>())) return {Error::File};
  rememberAccess(store.appKey(), std::move(token), lifetime);
  verifier.clear();
  pendingKey.clear();
  return {};
}

DropboxClient::Result DropboxClient::list(Page& out, const std::string& cursor) {
  Guard guard;
  out = {};
  if (!guard.held) return {Error::Busy};
  if (cursor.size() > 8192) return {Error::InvalidData};
  auto& store = DropboxStore::instance();
  auto result = ensureAccess(store.refreshToken_);
  if (!result.ok()) return result;
  JsonDocument request;
  const bool continuation = !cursor.empty();
  if (continuation)
    request["cursor"] = cursor;
  else {
    request["path"] = store.remoteFolder();
    request["recursive"] = false;
    request["include_deleted"] = false;
    request["limit"] = 25;
  }
  if (request.overflowed()) return {Error::TooLarge};
  std::string body;
  if (!serializeJson(request, body)) return {Error::TooLarge};
  std::string response;
  for (int attempt = 0; attempt < 2; ++attempt) {
    crosspoint::SecureHttpClient http;
    if (!configure(http)) return {Error::Network};
    http.addHeader("Authorization", "Bearer " + accessToken);
    http.addHeader("Content-Type", "application/json");
    result = smallPost(http,
                       continuation ? "https://api.dropboxapi.com/2/files/list_folder/continue"
                                    : "https://api.dropboxapi.com/2/files/list_folder",
                       body, response);
    if (result.httpStatus != 401 || attempt != 0) break;
    // Release the TLS connection before refreshing on a constrained ESP32-C3.
    http.close();
    result = ensureAccess(store.refreshToken_, true);
    if (!result.ok()) return result;
  }
  if (!result.ok()) return result;
  JsonDocument filter;
  for (const char* key : {".tag", "id", "name", "size", "is_downloadable"}) filter["entries"][0][key] = true;
  filter["cursor"] = true;
  filter["has_more"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, response, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(8)) ||
      !doc["entries"].is<JsonArray>() || !doc["has_more"].is<bool>() || !doc["cursor"].is<const char*>())
    return {Error::InvalidData};
  Page page;
  page.cursor = doc["cursor"].as<std::string>();
  page.hasMore = doc["has_more"].as<bool>();
  if (page.cursor.size() > 8192 || (page.hasMore && (page.cursor.empty() || page.cursor == cursor)))
    return {Error::InvalidData};
  const JsonArray array = doc["entries"].as<JsonArray>();
  if (array.size() > 100) return {Error::TooLarge};
  for (JsonObject object : array) {
    if ((object[".tag"] | "") != std::string("file") || !object["name"].is<const char*>() ||
        !object["id"].is<const char*>() || !object["size"].is<uint64_t>() ||
        (object["is_downloadable"].is<bool>() && !object["is_downloadable"].as<bool>()))
      continue;
    Entry entry{object["name"].as<std::string>(), object["id"].as<std::string>(), object["size"].as<uint64_t>()};
    if (bookName(entry.name) && safeId(entry.id) && !localName(entry.name).empty())
      page.books.push_back(std::move(entry));
  }
  out = std::move(page);
  return {};
}

DropboxClient::Result DropboxClient::download(const Entry& entry, std::string& savedPath, Progress progress) {
  Guard guard;
  savedPath.clear();
  if (!guard.held) return {Error::Busy};
  const std::string name = localName(entry.name);
  if (!bookName(entry.name) || name.empty() || !safeId(entry.id)) return {Error::InvalidData};
  if (entry.size == 0 || entry.size > std::numeric_limits<uint32_t>::max()) return {Error::TooLarge};
  auto& store = DropboxStore::instance();
  const auto destination = store.downloadFolder() + (store.downloadFolder() == "/" ? "" : "/") + name;
  if (!Storage.ready()) return {Error::File};
  if (Storage.exists(destination.c_str())) return {Error::AlreadyExists};
  if (Storage.sdFreeBytes() < entry.size + 4096) return {Error::File};
  if (progress && !progress(0, entry.size)) return {Error::Cancelled};
  auto result = ensureAccess(store.refreshToken_);
  if (!result.ok()) return result;
  if (!Storage.ensureDirectoryExists(store.downloadFolder().c_str())) return {Error::File};
  JsonDocument argument;
  argument["path"] = entry.id;  // ASCII ID avoids non-ASCII JSON in HTTP headers.
  if (argument.overflowed()) return {Error::TooLarge};
  std::string apiArgument;
  if (!serializeJson(argument, apiArgument)) return {Error::TooLarge};
  for (int attempt = 0; attempt < 2; ++attempt) {
    char suffix[48];
    uint32_t random = 0;
    esp_fill_random(&random, sizeof(random));
    snprintf(suffix, sizeof(suffix), "/.paperio-dropbox-%08lx.part", static_cast<unsigned long>(random));
    const std::string temporary = store.downloadFolder() + suffix;
    auto file = Storage.open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL);
    if (!file) return {Error::File};
    crosspoint::SecureHttpClient http;
    if (!configure(http)) {
      file.close();
      Storage.remove(temporary.c_str());
      return {Error::Network};
    }
    http.addHeader("Authorization", "Bearer " + accessToken);
    http.addHeader("Dropbox-API-Arg", apiArgument);
    size_t received = 0;
    bool writeFailed = false;
    bool cancelled = false;
    bool oversized = false;
    const int status =
        http.post("https://content.dropboxapi.com/2/files/download", "", [&](const uint8_t* bytes, size_t length) {
          if (http.lastStatus() != 200) return false;
          if (length > entry.size - received) {
            oversized = true;
            return false;
          }
          if (file.write(bytes, length) != length) {
            writeFailed = true;
            return false;
          }
          received += length;
          if (progress && !progress(received, entry.size)) {
            cancelled = true;
            return false;
          }
          delay(1);
          return true;
        });
    file.flush();
    const bool closed = file.close();
    const int actualStatus = http.lastStatus();
    http.close();
    if (actualStatus == 401 && attempt == 0) {
      Storage.remove(temporary.c_str());
      result = ensureAccess(store.refreshToken_, true);
      if (!result.ok()) return result;
      continue;
    }
    result = statusResult(actualStatus != 200 && actualStatus > 0 ? actualStatus : status);
    if (writeFailed || !closed)
      result = {Error::File};
    else if (cancelled)
      result = {Error::Cancelled};
    else if (oversized)
      result = {Error::InvalidData};
    else if (result.ok() && received != entry.size)
      result = {Error::InvalidData};
    if (result.ok()) {
      if (Storage.exists(destination.c_str()))
        result = {Error::AlreadyExists};
      else if (!Storage.rename(temporary.c_str(), destination.c_str()))
        result = {Error::File};
      else {
        savedPath = destination;
        return {};
      }
    }
    Storage.remove(temporary.c_str());
    return result;
  }
  return {Error::Auth};
}
