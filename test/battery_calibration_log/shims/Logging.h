#pragma once

namespace calibration_test {
inline unsigned errorLogs = 0;
inline unsigned infoLogs = 0;
template <typename... Args>
void logError(Args&&...) {
  ++errorLogs;
}
template <typename... Args>
void logInfo(Args&&...) {
  ++infoLogs;
}
}  // namespace calibration_test

#define LOG_ERR(...) ::calibration_test::logError(__VA_ARGS__)
#define LOG_INF(...) ::calibration_test::logInfo(__VA_ARGS__)
