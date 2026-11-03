#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class DropboxClient {
 public:
  enum class Error {
    Ok,
    NotConfigured,
    NotLinked,
    Auth,
    Network,
    Api,
    RateLimited,
    TooLarge,
    InvalidData,
    File,
    Cancelled,
    AlreadyExists,
    Busy
  };
  struct Result {
    Error error = Error::Ok;
    int httpStatus = 0;
    bool ok() const { return error == Error::Ok; }
  };
  struct Entry {
    std::string name;
    std::string id;
    uint64_t size = 0;
  };
  struct Page {
    std::vector<Entry> books;
    std::string cursor;
    bool hasMore = false;
  };
  using Progress = std::function<bool(size_t downloaded, size_t total)>;

  // Empty URL means settings are absent or another operation owns the client.
  static std::string beginAuthorization();
  static bool authorizationPending();
  static void cancelAuthorization();
  static Result completeAuthorization(const std::string& code);

  Result list(Page& out, const std::string& cursor = "");
  Result download(const Entry& entry, std::string& savedPath, Progress progress = nullptr);
};
