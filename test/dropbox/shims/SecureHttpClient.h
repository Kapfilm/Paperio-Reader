#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>
namespace crosspoint {
struct DropboxTestReply {
  int status;
  std::string body;
  int result = 0;
};
struct DropboxTestRequest {
  std::string url, body;
  std::map<std::string, std::string> headers;
};
inline std::deque<DropboxTestReply> dropboxTestReplies;
inline std::vector<DropboxTestRequest> dropboxTestRequests;
inline bool dropboxTestStrictTls = true;
class SecureHttpClient {
 public:
  using BodySink = std::function<bool(const uint8_t*, size_t)>;
  using ProgressFn = std::function<bool(size_t, size_t)>;
  static constexpr int ERR_ABORTED = -6;
  void setCACert(const char* ca) { dropboxTestStrictTls &= ca && *ca; }
  void setAllowInsecureFallback(bool b) { dropboxTestStrictTls &= !b; }
  void setAllowCertificateDateErrors(bool b) { dropboxTestStrictTls &= !b; }
  void setMaxRedirects(int n) { dropboxTestStrictTls &= n == 0; }
  void setTimeout(uint32_t) {}
  void setUserAgent(const std::string&) {}
  void addHeader(const std::string& k, const std::string& v) { headers[k] = v; }
  void close() {}
  int lastStatus() const { return status; }
  int post(const std::string& url, const std::string& body, const BodySink& sink, const ProgressFn& = nullptr) {
    dropboxTestRequests.push_back({url, body, headers});
    if (dropboxTestReplies.empty()) {
      status = 0;
      return -2;
    }
    auto reply = std::move(dropboxTestReplies.front());
    dropboxTestReplies.pop_front();
    status = reply.status;
    for (size_t i = 0; i < reply.body.size(); i += 17) {
      size_t n = std::min<size_t>(17, reply.body.size() - i);
      if (sink && !sink(reinterpret_cast<const uint8_t*>(reply.body.data() + i), n)) return ERR_ABORTED;
    }
    return reply.result ? reply.result : status;
  }

 private:
  int status = 0;
  std::map<std::string, std::string> headers;
};
}  // namespace crosspoint
