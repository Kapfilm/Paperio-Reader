#pragma once

#include <HalStorage.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class Epub;

// Book-level inline-footnote preview cache ("footnotes.bin" in the book's cache dir).
//
// Real-world books rarely carry the EPUB 3 semantic markup the original per-chapter
// prepass relied on: notes are centralised in a separate rearnotes spine file (classic
// French/academic layout), or scattered across tiny Calibre split files addressed by
// bare `filepos` anchors with untagged `*` reference links (MOBI conversions). Both
// are invisible to a same-file, epub:type-gated scan.
//
// This module instead gathers ONCE per book, foreground (caller shows a popup):
//   Pass A: stream-scan every spine document for footnote-shaped links — short
//           marker text (*, †, digits …) or noteref-tagged — and collect their
//           resolved targets (spine index + fragment).
//   Pass B: stream-scan each target spine once, capturing the note text at every
//           wanted anchor id (subtree text, or the text FOLLOWING an empty inline
//           anchor until its parent block closes — the Calibre filepos pattern).
// Results are persisted to footnotes.bin; section builds then use Lookup, which keeps
// only an 8-byte-per-entry hash index in RAM and reads preview text from SD on hit.
//
// File format v1 (little-endian):
//   u32 magic 'FNP1' | u16 version | u16 count | u32 indexOffset
//   blob:  count x { u16 textLen, char text[textLen] }   (gather order)
//   index: count x { u32 keyHash, u32 blobOffset }        (sorted by keyHash)
// Key string: "<targetSpineIndex>#<fragment>", FNV-1a 32-bit. A 32-bit collision
// inside one book shows a wrong preview — cosmetically harmless, astronomically rare.
namespace FootnotePreviews {

constexpr const char* CACHE_FILENAME = "/footnotes.bin";
constexpr size_t MAX_ENTRIES = 512;      // gather cap; index cap in Lookup matches
constexpr size_t MAX_TEXT_BYTES = 240;   // per-preview text cap (matches old table)
constexpr size_t MIN_SUBTREE_BYTES = 8;  // below this, an id'd element is treated as an
                                         // empty anchor and capture continues past it

// True when the book's preview cache file exists (cheap existence probe).
bool cacheExists(const std::string& bookCachePath);

// Build footnotes.bin for the book. Blocking; run foreground with a popup. Returns
// false only on I/O failure — a book with no detectable footnotes writes a valid
// empty cache so the gather is never repeated. progressFn (optional) gets 0..100.
bool gather(Epub& epub, const std::function<void(int)>& progressFn = {});

// Disk-backed lookup used during a section build. Holds the sorted hash index in RAM
// (8 bytes/entry) and an open read handle; find() seeks and reads the text on a hit.
class Lookup {
 public:
  // Opens and validates footnotes.bin. currentSpineIndex anchors same-file ("#frag")
  // references. Returns false when the file is missing or invalid (previews skipped).
  bool open(const std::string& bookCachePath, const Epub* epub, int currentSpineIndex);

  // Resolves an href from the chapter being parsed ("#frag", "notes.xhtml#frag",
  // "../Text/notes.xhtml#frag") against the index. On a hit fills outText and
  // returns true.
  bool find(const char* href, std::string& outText);

  bool isOpen() const { return entryCount_ > 0; }

 private:
  struct IndexEntry {
    uint32_t keyHash;
    uint32_t blobOffset;
  };
  std::unique_ptr<IndexEntry[]> index_;
  uint16_t entryCount_ = 0;
  FsFile file_;
  const Epub* epub_ = nullptr;
  int currentSpineIndex_ = -1;
};

}  // namespace FootnotePreviews
