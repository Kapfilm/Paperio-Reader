#pragma once
#include <cstdint>
#include <string>

// v29 positions refer to artificial FB2 slices. They must never be interpreted
// as native-chapter positions. Original data stays in a sibling backup folder.
namespace Fb2StateMigration {
inline constexpr char NATIVE_LAYOUT_FILE[] = ".fb2_native_layout";
inline constexpr char NATIVE_LAYOUT_BACKUP_SUFFIX[] = ".preserve_fb2_native_layout";
inline constexpr char PACKAGE_STATE_FILE[] = "fb2_package.bin";
std::string backupPath(const std::string& cachePath);
// Call BEFORE deleting generated cache. False means leave the old book intact
// and abort this rebuild. Interrupted operations are safe to retry.
bool prepare(const std::string& cachePath);
bool needsReaderMigration(const std::string& cachePath);
bool needsNotice(const std::string& cachePath);
bool markReaderMigrated(const std::string& cachePath);
bool acknowledgeNotice(const std::string& cachePath);
}
