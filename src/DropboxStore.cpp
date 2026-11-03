#include "DropboxStore.h"

#include <ArduinoJson.h>
#include <Preferences.h>

#include <algorithm>
#include <cctype>

namespace {
constexpr size_t kMaxConfigBytes = 7168;

bool safeFolder(const std::string& path, bool local) {
  if (path.size() > (local ? 192u : 1024u)) return false;
  if (!path.empty() && path.front() != '/') return false;
  if (local) {
    std::string folded = path;
    std::transform(folded.begin(), folded.end(), folded.begin(), [](unsigned char c) { return std::tolower(c); });
    if (folded.empty() || folded == "/.crosspoint" || folded.rfind("/.crosspoint/", 0) == 0) return false;
  }
  for (unsigned char c : path)
    if (c < 32 || c == 127 || c == '\\') return false;
  size_t start = 1;
  while (start < path.size()) {
    size_t end = path.find('/', start);
    if (end == std::string::npos) end = path.size();
    const auto part = path.substr(start, end - start);
    if (part.empty() || part == "." || part == "..") return false;
    if (local && (part.front() == ' ' || part.back() == ' ' || part.back() == '.')) return false;
    start = end + 1;
  }
  return true;
}

bool safeKey(const std::string& key) {
  if (key.size() > 128) return false;
  return std::all_of(key.begin(), key.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; });
}

bool safeToken(const std::string& token) {
  return token.size() <= 4096 &&
         std::all_of(token.begin(), token.end(), [](unsigned char c) { return c > 32 && c < 127; });
}
}  // namespace

DropboxStore& DropboxStore::instance() {
  static DropboxStore store;
  return store;
}

bool DropboxStore::load() {
  Preferences prefs;
  if (!prefs.begin("paperio_dbx", false)) return false;
  const String packed = prefs.getString("config", "");
  prefs.end();
  if (packed.isEmpty()) {
    appKey_.clear();
    remoteFolder_.clear();
    downloadFolder_ = "/Dropbox";
    refreshToken_.clear();
    return true;
  }
  if (packed.length() > kMaxConfigBytes) return false;
  JsonDocument doc;
  if (deserializeJson(doc, packed.c_str()) || !doc.is<JsonObject>()) return false;
  const std::string key = doc["key"] | "";
  const std::string folder = doc["folder"] | "";
  const std::string local = doc["local"] | "/Dropbox";
  const std::string token = doc["refresh"] | "";
  if (!safeKey(key) || !safeFolder(folder, false) || !safeFolder(local, true) || !safeToken(token)) return false;
  appKey_ = key;
  remoteFolder_ = folder == "/" ? "" : folder;
  downloadFolder_ = local;
  refreshToken_ = token;
  return true;
}

bool DropboxStore::persist(const std::string& key, const std::string& folder, const std::string& local,
                           const std::string& token) {
  JsonDocument doc;
  doc["key"] = key;
  doc["folder"] = folder;
  doc["local"] = local;
  doc["refresh"] = token;
  if (doc.overflowed()) return false;
  std::string packed;
  if (serializeJson(doc, packed) == 0 || packed.size() > kMaxConfigBytes) return false;
  Preferences prefs;
  if (!prefs.begin("paperio_dbx", false)) return false;
  // One NVS string is one atomic update: credentials and their app key cannot
  // become mismatched after a reset or a partially failed multi-key write.
  const bool saved = prefs.putString("config", packed.c_str()) == packed.size();
  prefs.end();
  if (!saved) return false;
  appKey_ = key;
  remoteFolder_ = folder;
  downloadFolder_ = local;
  refreshToken_ = token;
  return true;
}

bool DropboxStore::saveConfig(const std::string& key, const std::string& folder, const std::string& local) {
  std::string normalized = folder;
  while (normalized.size() > 1 && normalized.back() == '/') normalized.pop_back();
  if (normalized == "/") normalized.clear();
  std::string destination = local.empty() ? "/Dropbox" : local;
  while (destination.size() > 1 && destination.back() == '/') destination.pop_back();
  if (!safeKey(key) || !safeFolder(normalized, false) || !safeFolder(destination, true)) return false;
  return persist(key, normalized, destination, key == appKey_ ? refreshToken_ : "");
}

bool DropboxStore::saveRefreshToken(const std::string& token) {
  if (token.empty() || !safeToken(token)) return false;
  return persist(appKey_, remoteFolder_, downloadFolder_, token);
}

bool DropboxStore::unlink() { return persist(appKey_, remoteFolder_, downloadFolder_, ""); }
