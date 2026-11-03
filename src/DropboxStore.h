#pragma once

#include <string>

class DropboxClient;

// Dropbox credentials are kept outside exported settings and SD-card backups.
class DropboxStore {
 public:
  static DropboxStore& instance();
  bool load();
  bool saveConfig(const std::string& appKey, const std::string& remoteFolder, const std::string& downloadFolder);
  bool unlink();
  const std::string& appKey() const { return appKey_; }
  const std::string& remoteFolder() const { return remoteFolder_; }
  const std::string& downloadFolder() const { return downloadFolder_; }
  bool configured() const { return !appKey_.empty(); }
  bool linked() const { return !refreshToken_.empty(); }

 private:
  friend class DropboxClient;
  bool saveRefreshToken(const std::string& token);
  bool persist(const std::string& appKey, const std::string& remoteFolder, const std::string& downloadFolder,
               const std::string& refreshToken);
  std::string appKey_;
  std::string remoteFolder_;
  std::string downloadFolder_ = "/Dropbox";
  std::string refreshToken_;
};
