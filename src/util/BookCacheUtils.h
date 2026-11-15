#pragma once

#include <string>

// Cache routing for UI/network code. The source path remains the public book
// identity; FB2's generated package is an implementation detail.
std::string getBookCachePath(const std::string& sourcePath);
bool clearBookCacheForPath(const std::string& sourcePath);
bool clearGeneratedBookCacheForPath(const std::string& sourcePath);

// After a successful rename, keep the previous copy of reading state so that
// an SD error while associating it with the new path is recoverable.
bool transferBookStateAfterMove(const std::string& oldPath, const std::string& newPath);
