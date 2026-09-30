#pragma once

#include <string>

// Cache routing for UI/network code. The source path remains the public book
// identity; FB2's generated package is an implementation detail.
std::string getBookCachePath(const std::string& sourcePath);
bool clearBookCacheForPath(const std::string& sourcePath);
