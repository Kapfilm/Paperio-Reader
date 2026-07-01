#include "SecureHttpClient.h"

#include <Logging.h>
#include <base64.h>

#include <cstdlib>
#include <cstring>

namespace crosspoint {

namespace {
bool isRedirect(int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}
std::string toLowerAscii(const std::string& s) {
  std::string r(s);
  for (char& c : r) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
  return r;
}
}  // namespace

bool SecureHttpClient::parseUrl(const std::string& in, Url& out) {
  const size_t schemeEnd = in.find("://");
  if (schemeEnd == std::string::npos) return false;
  out.scheme = toLowerAscii(in.substr(0, schemeEnd));
  const size_t hostStart = schemeEnd + 3;
  const size_t pathStart = in.find('/', hostStart);
  const std::string hostPort =
      pathStart == std::string::npos ? in.substr(hostStart) : in.substr(hostStart, pathStart - hostStart);
  out.path = pathStart == std::string::npos ? "/" : in.substr(pathStart);
  const size_t portSep = hostPort.rfind(':');
  if (portSep != std::string::npos && hostPort.find(']') == std::string::npos) {
    out.host = hostPort.substr(0, portSep);
    out.port = static_cast<uint16_t>(atoi(hostPort.substr(portSep + 1).c_str()));
  } else {
    out.host = hostPort;
    out.port = out.scheme == "https" ? 443 : 80;
  }
  return !out.host.empty() && (out.scheme == "http" || out.scheme == "https");
}

std::string SecureHttpClient::resolveRedirect(const Url& base, const std::string& location) {
  if (location.find("://") != std::string::npos) return location;  // absolute
  if (!location.empty() && location[0] == '/') {                   // absolute path
    return base.scheme + "://" + base.host + ":" + std::to_string(base.port) + location;
  }
  // relative path: strip base path back to last '/'
  const size_t slash = base.path.rfind('/');
  const std::string dir = slash == std::string::npos ? "/" : base.path.substr(0, slash + 1);
  return base.scheme + "://" + base.host + ":" + std::to_string(base.port) + dir + location;
}

void SecureHttpClient::close() {
  if (_client) {
    _client->stop();
    _client = nullptr;
  }
  _connHost.clear();
  _connPort = 0;
  _connectedHttps = false;
}

bool SecureHttpClient::ensureConnected(const Url& u) {
  // Reuse a matching kept-open connection.
  if (_client && _client->connected() && _connectedHttps == u.https() && _connHost == u.host && _connPort == u.port) {
    return true;
  }
  close();

  if (u.https()) {
    _secure.setCACert(_rootCA);
    _secure.setAllowInsecureFallback(_allowInsecureFallback);
    _secure.setTimeout(_timeoutMs / 1000);
    if (!_secure.connect(u.host.c_str(), u.port)) {
      LOG_ERR("HTTP", "https connect failed: %s:%u", u.host.c_str(), u.port);
      return false;
    }
    _lastInsecure = _secure.lastConnectWasInsecure();
    _client = &_secure;
    _connectedHttps = true;
  } else {
    _plain.setTimeout(_timeoutMs / 1000);
    if (!_plain.connect(u.host.c_str(), u.port)) {
      LOG_ERR("HTTP", "http connect failed: %s:%u", u.host.c_str(), u.port);
      return false;
    }
    _lastInsecure = false;
    _client = &_plain;
    _connectedHttps = false;
  }
  _connHost = u.host;
  _connPort = u.port;
  return true;
}

bool SecureHttpClient::sendRequest(const char* method, const Url& u, const uint8_t* body, size_t bodyLen) {
  const uint16_t defPort = u.https() ? 443 : 80;
  const std::string hostHeader = (u.port == defPort) ? u.host : (u.host + ":" + std::to_string(u.port));

  std::string req = std::string(method) + " " + u.path + " HTTP/1.1\r\n";
  req += "Host: " + hostHeader + "\r\n";
  req += "User-Agent: " + _userAgent + "\r\n";
  // Keep-alive so a Session can reuse the handshake across files.
  req += "Connection: keep-alive\r\n";
  if (!_user.empty() && !_pass.empty()) {
    const std::string creds = _user + ":" + _pass;
    req += "Authorization: Basic " + std::string(base64::encode(creds.c_str()).c_str()) + "\r\n";
  }
  for (const std::string& h : _headers) req += h + "\r\n";
  if (body && bodyLen) req += "Content-Length: " + std::to_string(bodyLen) + "\r\n";
  req += "\r\n";

  if (_client->write(reinterpret_cast<const uint8_t*>(req.data()), req.size()) != req.size()) return false;
  if (body && bodyLen) {
    if (_client->write(body, bodyLen) != bodyLen) return false;
  }
  return true;
}

bool SecureHttpClient::readLine(std::string& line, uint32_t deadline) {
  line.clear();
  while (static_cast<int32_t>(millis() - deadline) < 0) {
    while (_client->available() > 0) {
      const int ch = _client->read();
      if (ch < 0) break;
      if (ch == '\n') {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return true;
      }
      if (line.size() >= MAX_LINE) return false;
      line += static_cast<char>(ch);
    }
    if (!_client->connected() && _client->available() == 0) return false;
    delay(2);
  }
  return false;
}

bool SecureHttpClient::readHeaders(int& status, long& contentLength, bool& chunked, bool& keepAlive,
                                   std::string& location) {
  status = 0;
  contentLength = -1;
  chunked = false;
  keepAlive = true;  // HTTP/1.1 default
  location.clear();

  const uint32_t deadline = millis() + _timeoutMs;
  std::string line;
  if (!readLine(line, deadline)) return false;
  // "HTTP/1.1 200 OK" -> code at offset 9
  status = line.size() >= 12 ? atoi(line.c_str() + 9) : 0;
  if (status == 0) return false;

  while (readLine(line, deadline)) {
    if (line.empty()) return true;  // end of headers
    const size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string name = toLowerAscii(line.substr(0, colon));
    std::string value = line.substr(colon + 1);
    while (!value.empty() && value.front() == ' ') value.erase(value.begin());
    if (name == "content-length") {
      contentLength = strtol(value.c_str(), nullptr, 10);
    } else if (name == "transfer-encoding") {
      if (toLowerAscii(value).find("chunked") != std::string::npos) chunked = true;
    } else if (name == "connection") {
      if (toLowerAscii(value).find("close") != std::string::npos) keepAlive = false;
    } else if (name == "location") {
      location = value;
    }
  }
  return false;  // ran out before blank line
}

// Truncation-safe body reader. Completion is keyed on framing, NEVER on read()==0
// (which means WANT_READ / no data yet for the wolfSSL transport).
int SecureHttpClient::readBody(const BodySink& sink, const ProgressFn& progress, long contentLength, bool chunked,
                               bool keepAlive) {
  uint8_t buf[READ_CHUNK];
  const size_t total = contentLength > 0 ? static_cast<size_t>(contentLength) : 0;
  size_t downloaded = 0;
  uint32_t idleDeadline = millis() + _timeoutMs;

  auto emit = [&](const uint8_t* d, size_t n) -> bool {
    if (sink && n && !sink(d, n)) return false;
    downloaded += n;
    if (progress && !progress(downloaded, total)) return false;
    return true;
  };

  if (chunked) {
    std::string line;
    for (;;) {
      if (!readLine(line, millis() + _timeoutMs)) return ERR_TRUNCATED;
      const size_t semi = line.find(';');
      const long sz = strtol((semi == std::string::npos ? line : line.substr(0, semi)).c_str(), nullptr, 16);
      if (sz == 0) {  // last chunk; drain trailers
        while (readLine(line, millis() + _timeoutMs) && !line.empty()) {
        }
        return 0;
      }
      size_t remaining = static_cast<size_t>(sz);
      while (remaining > 0) {
        const int n = _client->read(buf, remaining < sizeof(buf) ? remaining : sizeof(buf));
        if (n < 0) return ERR_TRUNCATED;
        if (n == 0) {  // WANT_READ: no full TLS record decrypted yet
          if (!_client->connected() && _client->available() == 0) return ERR_TRUNCATED;
          if (static_cast<int32_t>(millis() - idleDeadline) >= 0) return ERR_TIMEOUT;
          // Only sleep when the transport is truly empty; if TCP bytes are
          // buffered, wolfSSL just needs another read to assemble the record.
          if (_client->available() == 0) delay(2);
          continue;
        }
        idleDeadline = millis() + _timeoutMs;
        if (!emit(buf, static_cast<size_t>(n))) return ERR_ABORTED;
        remaining -= static_cast<size_t>(n);
      }
      readLine(line, millis() + _timeoutMs);  // consume the chunk's trailing CRLF
    }
  }

  if (contentLength >= 0) {  // fixed length
    size_t remaining = static_cast<size_t>(contentLength);
    while (remaining > 0) {
      const int n = _client->read(buf, remaining < sizeof(buf) ? remaining : sizeof(buf));
      if (n < 0) return ERR_TRUNCATED;
      if (n == 0) {
        if (!_client->connected() && _client->available() == 0) return ERR_TRUNCATED;
        if (static_cast<int32_t>(millis() - idleDeadline) >= 0) return ERR_TIMEOUT;
        if (_client->available() == 0) delay(2);
        continue;
      }
      idleDeadline = millis() + _timeoutMs;
      if (!emit(buf, static_cast<size_t>(n))) return ERR_ABORTED;
      remaining -= static_cast<size_t>(n);
    }
    return 0;
  }

  // No length, no chunked: read until the peer closes (Connection: close).
  for (;;) {
    const int n = _client->read(buf, sizeof(buf));
    if (n < 0) return 0;  // treat as end for close-delimited
    if (n == 0) {
      if (!_client->connected() && _client->available() == 0) return 0;  // clean end
      if (static_cast<int32_t>(millis() - idleDeadline) >= 0) return ERR_TIMEOUT;
      if (_client->available() == 0) delay(2);
      continue;
    }
    idleDeadline = millis() + _timeoutMs;
    if (!emit(buf, static_cast<size_t>(n))) return ERR_ABORTED;
  }
}

int SecureHttpClient::get(const std::string& url, const BodySink& sink, const ProgressFn& progress) {
  std::string current = url;
  for (int hop = 0; hop <= _maxRedirects; ++hop) {
    Url u;
    if (!parseUrl(current, u)) return ERR_BAD_URL;
    if (!ensureConnected(u)) return ERR_CONNECT;
    if (!sendRequest("GET", u, nullptr, 0)) {
      close();
      return ERR_SEND;
    }
    int status;
    long contentLength;
    bool chunked, keepAlive;
    std::string location;
    if (!readHeaders(status, contentLength, chunked, keepAlive, location)) {
      close();
      return ERR_TIMEOUT;
    }
    _status = status;

    if (isRedirect(status) && !location.empty()) {
      // Drain the redirect's (usually empty) body to keep the socket usable, then hop.
      readBody(nullptr, nullptr, contentLength, chunked, keepAlive);
      if (!keepAlive) close();
      current = resolveRedirect(u, location);
      if (hop == _maxRedirects) return ERR_TOO_MANY_REDIRECTS;
      continue;
    }

    const int rc = readBody(sink, progress, contentLength, chunked, keepAlive);
    if (!keepAlive) close();
    if (rc < 0) return rc;
    return status;
  }
  return ERR_TOO_MANY_REDIRECTS;
}

int SecureHttpClient::request(const char* method, const std::string& url, const std::string& body) {
  _body.clear();
  auto sink = [this](const uint8_t* d, size_t n) {
    _body.append(reinterpret_cast<const char*>(d), n);
    return true;
  };

  std::string current = url;
  std::string activeMethod = method;
  std::string activeBody = body;
  for (int hop = 0; hop <= _maxRedirects; ++hop) {
    Url u;
    if (!parseUrl(current, u)) return ERR_BAD_URL;
    if (!ensureConnected(u)) return ERR_CONNECT;
    const bool hasBody = !activeBody.empty();
    if (!sendRequest(activeMethod.c_str(), u,
                     hasBody ? reinterpret_cast<const uint8_t*>(activeBody.data()) : nullptr, activeBody.size())) {
      close();
      return ERR_SEND;
    }
    int status;
    long contentLength;
    bool chunked, keepAlive;
    std::string location;
    if (!readHeaders(status, contentLength, chunked, keepAlive, location)) {
      close();
      return ERR_TIMEOUT;
    }
    _status = status;

    if (isRedirect(status) && !location.empty()) {
      readBody(nullptr, nullptr, contentLength, chunked, keepAlive);
      if (!keepAlive) close();
      current = resolveRedirect(u, location);
      // 303 always -> GET; 301/302 commonly downgrade POST->GET too. 307/308
      // preserve method + body. When downgrading to GET, drop the request body.
      if (status == 303 || status == 301 || status == 302) {
        activeMethod = "GET";
        activeBody.clear();
      }
      if (hop == _maxRedirects) return ERR_TOO_MANY_REDIRECTS;
      continue;
    }

    const int rc = readBody(sink, nullptr, contentLength, chunked, keepAlive);
    if (!keepAlive) close();
    if (rc < 0) return rc;
    return status;
  }
  return ERR_TOO_MANY_REDIRECTS;
}

}  // namespace crosspoint
