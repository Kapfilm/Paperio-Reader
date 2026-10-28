#pragma once

#include <ArduinoJson.h>

#include <cmath>
#include <limits>
#include <string>
#include <utility>

#include "KOReaderSyncClient.h"

// Adapted from Witchhunt Reader 80c31051: servers may return percentage-only
// records or several empty JSON shapes. Never turn absent progress into 0%.
namespace koreader_reply {
inline size_t contentStart(const std::string& body) {
  size_t at = body.size() >= 3 && static_cast<unsigned char>(body[0]) == 0xEF &&
                      static_cast<unsigned char>(body[1]) == 0xBB && static_cast<unsigned char>(body[2]) == 0xBF
                  ? 3
                  : 0;
  while (at < body.size() && (body[at] == ' ' || body[at] == '\t' || body[at] == '\r' || body[at] == '\n')) ++at;
  return at;
}

inline KOReaderSyncClient::Error parseProgressReply(const std::string& body, const std::string& documentHash,
                                                    KOReaderProgress& outProgress) {
  using Client = KOReaderSyncClient;
  const size_t at = contentStart(body);
  if (at == body.size()) return Client::NOT_FOUND;
  if (body[at] == '<') return Client::INVALID_RESPONSE;

  JsonDocument doc;
  if (deserializeJson(doc, body.c_str() + at)) return Client::JSON_ERROR;

  if (!doc.is<JsonObject>()) {
    // Known no-record conventions: null, an empty array or an empty string.
    if (doc.isNull() || (doc.is<JsonArray>() && doc.size() == 0) ||
        (doc.is<const char*>() && doc.as<std::string>().empty())) {
      return Client::NOT_FOUND;
    }
    return Client::INVALID_RESPONSE;
  }

  const auto locatorValue = doc["progress"];
  if (!locatorValue.isNull() && !locatorValue.is<const char*>()) return Client::INVALID_RESPONSE;
  const char* const locator = locatorValue | "";
  const auto percentageValue = doc["percentage"];
  const bool hasPercentage = !percentageValue.isNull();
  float percentage = std::numeric_limits<float>::quiet_NaN();
  if (hasPercentage) {
    if (!percentageValue.is<float>()) return Client::INVALID_RESPONSE;
    percentage = percentageValue.as<float>();
    if (!std::isfinite(percentage) || percentage < 0 || percentage > 1) return Client::INVALID_RESPONSE;
  }
  if (locator[0] == '\0' && !hasPercentage) return Client::NOT_FOUND;

  KOReaderProgress parsed{};
  parsed.document = documentHash;
  parsed.progress = locator;
  parsed.percentage = percentage;
  parsed.device = doc["device"] | "";
  parsed.deviceId = doc["device_id"] | "";
  parsed.timestamp = doc["timestamp"].as<int64_t>();
  outProgress = std::move(parsed);
  return Client::OK;
}

// Keep status/body handling testable without connecting to a user's sync account.
inline KOReaderSyncClient::Error parseProgressHttpReply(int status, const std::string& body,
                                                        const std::string& documentHash,
                                                        KOReaderProgress& outProgress) {
  using Client = KOReaderSyncClient;
  if (status >= 300 && status < 400) return Client::REDIRECT_ERROR;
  const size_t at = contentStart(body);
  if (((status >= 200 && status < 300) || status == 401 || status == 404) && at < body.size() && body[at] == '<') {
    return Client::INVALID_RESPONSE;
  }
  if (status >= 200 && status < 300) return parseProgressReply(body, documentHash, outProgress);
  if (status == 401) return Client::AUTH_FAILED;
  if (status == 404) return Client::NOT_FOUND;
  return Client::SERVER_ERROR;
}
}  // namespace koreader_reply
