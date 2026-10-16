#pragma once

#include <cstdint>
#include <string>

namespace BookCoverUtils {

// Path stem used by managed covers. For "book.fb2.zip" this returns "book",
// matching the reader's existing sidecar-cover convention.
std::string basePath(const std::string& bookPath);

// Managed covers are kept beside the book as <book>.cover.jpg/.png so they do
// not overwrite an unrelated legacy sidecar such as <book>.jpg.
std::string findCustomCover(const std::string& bookPath);
bool hasCustomCover(const std::string& bookPath);

// Monotonically increasing identifier for the installed managed cover. Legacy
// managed covers without a marker start at revision 1. Embedded/legacy covers
// return 0. Thumbnail filenames include this value so an old BMP can never be
// mistaken for a newly selected cover even if cache deletion failed.
uint32_t customCoverRevision(const std::string& bookPath);

// JPG/JPEG and PNG are supported. The copy is staged and size-verified before
// replacing the previous managed cover.
bool install(const std::string& bookPath, const std::string& imagePath);
bool reset(const std::string& bookPath);

// Drop only derived cover artifacts; progress, bookmarks and book indexes stay intact.
void invalidateDerivedFiles(const std::string& bookPath);

}  // namespace BookCoverUtils
