// Fb2Types.h
//
// FB2 has no "files" the way EPUB has spine hrefs — it's one XML stream with
// nested <section> elements. To slot into CrossPoint's existing
// BookMetadataCache / book.bin shape (see docs/file-formats.md: Metadata,
// SpineEntry, TocEntry) with minimal changes downstream, each <section> is
// treated as one spine entry, addressed by a synthetic href of the form
// "#fb2sec:<index>" instead of a real file path. cumulativeSize is an
// estimate (decoded-text byte count) used the same way EPUB's is: to seed
// progress-bar math before real pagination has happened.

#pragma once
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <cstring>

struct Fb2Author {
    std::string firstName;
    std::string middleName;
    std::string lastName;
    std::string nickname;
};

struct Fb2Metadata {
    std::string title;                 // book-title
    std::string author;                // formatted "First Middle Last" of the
                                        // first <author>, joined with "; " if
                                        // there are more — matches EPUB's
                                        // single `author` string field
    std::vector<Fb2Author> authors;     // full structured list, for callers
                                        // that want more than the flattened
                                        // string (e.g. an "About" screen)
    std::string language;              // <lang>
    std::string date;                  // display text from <title-info><date>, if present
    std::string coverBinaryId;          // id= of the <binary> referenced by
                                        // <coverpage><image l:href="#id"/></coverpage>,
                                        // WITHOUT the leading '#'
    std::string annotationText;        // flattened <annotation> paragraphs,
                                        // plain text, for a library-view blurb
    std::string sequenceName;          // <sequence name="..."> if present
    uint32_t sequenceNumber = 0;

    // Raw CSS text from <description><stylesheet type="text/css">...</stylesheet>,
    // if the producer embedded one (mirrors the `embeddedStyle` cache-busting
    // flag EPUB sections already carry per docs/file-formats.md — same idea:
    // a CSS-aware layout pass can pull line-height/letter-spacing/etc. rules
    // out of this instead of the parser inventing typographic APIs for
    // properties FB2's tag set has no element for). Empty if none present.
    std::string embeddedStylesheetCss;
};

// One entry per <section> found anywhere under any <body>. The scan keeps
// only fields needed to write the on-SD index or render a chapter later.
// Parent/end offsets are deliberately omitted: retaining them for hundreds
// of sections exhausts the ESP32-C3 heap before the index can be persisted.
struct Fb2SectionIndexEntry {
    uint32_t innerStartOffset = 0; // offset right after the opening <section ...> tag
    uint16_t level = 0;            // nesting depth, 0 = direct child of <body>
    int16_t bodyIndex = 0;         // which <body> this section lives under

    // IMPORTANT: do not put std::string here.
    //
    // A 700-section anthology previously kept two std::string objects in
    // EVERY section entry even when most IDs were empty. On ESP32-C3 the
    // object/capacity overhead alone consumed tens of KiB before actual title
    // bytes were counted. Offsets below point into Fb2ScanResult::stringPool,
    // so one bounded contiguous allocation holds all section text.
    uint32_t idPoolOffset = UINT32_MAX;
    uint16_t idLength = 0;
    uint32_t titlePoolOffset = UINT32_MAX;
    uint16_t titleLength = 0;

    uint32_t approxTextBytes = 0; // decoded-text size estimate, for progress math
    uint16_t imageRefCount = 0;   // direct inline images, capped at UINT16_MAX
    // Some Calibre FB2 exports flatten a chapter heading into the first
    // <p><strong>...</strong></p> instead of using the standard <title>.
    bool fallbackTitle = false;
};

// Optional seekable scratch storage. Firmware uses SD; host callers may keep
// the original in-memory mode. No section or ID is dropped under heap pressure.
class Fb2ScanStorage {
public:
    virtual ~Fb2ScanStorage() = default;
    virtual bool read(bool strings, uint32_t offset, void* data, size_t size) = 0;
    virtual bool write(bool strings, uint32_t offset, const void* data, size_t size) = 0;
};

class Fb2SectionList {
public:
    std::shared_ptr<Fb2ScanStorage> storage;
    size_t size() const { return storage ? count_ : memory_.size(); }
    bool empty() const { return size() == 0; }
    bool good() const { flush(); return good_; }
    bool healthy() const { return good_; }
    void push_back(const Fb2SectionIndexEntry& entry) {
        if (!storage) { memory_.push_back(entry); return; }
        // Copy first: callers may append the currently cached entry.
        const auto value = entry;
        flush();
        if (!good_ || count_ >= UINT32_MAX / sizeof(cached_)) { good_ = false; return; }
        cached_ = value; cachedIndex_ = count_++; dirty_ = true;
    }
    Fb2SectionIndexEntry& operator[](size_t i) {
        if (!storage) return memory_[i];
        load(i); dirty_ = true; return cached_;
    }
    const Fb2SectionIndexEntry& operator[](size_t i) const {
        if (!storage) return memory_[i];
        load(i); return cached_;
    }
    Fb2SectionIndexEntry& back() { return (*this)[size()-1]; }
    struct Iterator {
        const Fb2SectionList* list;
        size_t index;
        const Fb2SectionIndexEntry& operator*() const { return (*list)[index]; }
        Iterator& operator++() { ++index; return *this; }
        bool operator!=(const Iterator& other) const { return index != other.index; }
    };
    Iterator begin() const { return {this,0}; }
    Iterator end() const { return {this,size()}; }
private:
    void flush() const {
        if (storage && dirty_) {
            good_ = storage->write(false, static_cast<uint32_t>(cachedIndex_*sizeof(cached_)),
                                  &cached_, sizeof(cached_)) && good_;
            dirty_ = false;
        }
    }
    void load(size_t i) const {
        if (cachedIndex_ == i) return;
        flush(); cachedIndex_ = i;
        cached_ = {};
        if (i >= count_ || i > UINT32_MAX / sizeof(cached_)) { good_ = false; return; }
        good_ = storage->read(false, static_cast<uint32_t>(i*sizeof(cached_)),
                             &cached_, sizeof(cached_)) && good_;
    }
    std::deque<Fb2SectionIndexEntry> memory_;
    size_t count_ = 0;
    mutable Fb2SectionIndexEntry cached_{};
    mutable size_t cachedIndex_ = SIZE_MAX;
    mutable bool dirty_ = false;
    mutable bool good_ = true;
};

// One entry per top-level <body>. CrossPoint currently only renders the
// main flow; `name` lets a caller skip bodies like name="notes"/"comments"
// (endnotes) when building the primary spine, while still being able to
// look them up later for footnote rendering.
struct Fb2BodyIndexEntry {
    std::string name; // <body name="notes"> -> "notes"; empty for main body
};

struct Fb2BinaryIndexEntry {
    std::string id;             // matches href targets ("#id") elsewhere in the doc
    std::string contentType;    // e.g. "image/jpeg"
    uint32_t payloadStartOffset = 0; // offset of the first base64 byte
    uint32_t payloadEndOffset = 0;   // offset just past the last base64 byte
};

// Result of a full metadata/index scan (Fb2Parser::scan()).
struct Fb2ScanResult {
    // Only section IDs can be indexed directly from the section directory.
    bool hasNonSectionAnchors = false;
    Fb2Metadata metadata;
    std::vector<Fb2BodyIndexEntry> bodies;
    Fb2SectionList sections;   // flat, in document order
    std::deque<Fb2BinaryIndexEntry> binaries;

    // Shared pool for section IDs/titles. It is deliberately bounded: TOC
    // text is useful, but must never be allowed to consume the reader heap.
    // The parser reserves this once near the start of a seekable scan, so the
    // heap does not get fragmented by hundreds of tiny title allocations.
    std::string stringPool;

    // Lightweight first-open profiler counters. They are not persisted and
    // add no per-token allocation; the caller uses them to separate normal
    // document XML from large base64 <binary> payloads.
    uint32_t tokenCount = 0;
    uint32_t textTokenCount = 0;
    uint64_t textPayloadBytes = 0;
    uint64_t binaryTextBytes = 0;

    uint32_t storedStringBytes = 0;
    mutable bool storageGood = true;
    mutable std::string idScratch, titleScratch;
    bool good() const { return sections.good() && storageGood; }
    bool appendString(const char* data, size_t size, uint32_t& offset, uint16_t& length) {
        if (!sections.storage) return false;
        offset = UINT32_MAX;
        length = 0;
        if (size > UINT16_MAX || size > UINT32_MAX - storedStringBytes) return storageGood = false;
        offset = storedStringBytes;
        length = static_cast<uint16_t>(size);
        storageGood = sections.storage->write(true, offset, data, size) && storageGood;
        storedStringBytes += static_cast<uint32_t>(size);
        return storageGood;
    }
    std::string_view readStoredString(uint32_t offset, uint16_t length, std::string& scratch) const {
        if (offset == UINT32_MAX || length == 0) return {};
        if (offset > storedStringBytes || length > storedStringBytes - offset) {
            storageGood = false; return {};
        }
        scratch.resize(length);
        if (!sections.storage->read(true, offset, scratch.data(), length)) {
            storageGood = false; return {};
        }
        return scratch;
    }

    std::string_view sectionId(const Fb2SectionIndexEntry& section) const {
        if (sections.storage) return readStoredString(section.idPoolOffset, section.idLength, idScratch);
        if (section.idPoolOffset == UINT32_MAX || section.idLength == 0 ||
            section.idPoolOffset + section.idLength > stringPool.size()) {
            return {};
        }
        return std::string_view(stringPool.data() + section.idPoolOffset, section.idLength);
    }

    std::string_view sectionTitle(const Fb2SectionIndexEntry& section) const {
        if (sections.storage) return readStoredString(section.titlePoolOffset, section.titleLength, titleScratch);
        if (section.titlePoolOffset == UINT32_MAX || section.titleLength == 0 ||
            section.titlePoolOffset + section.titleLength > stringPool.size()) {
            return {};
        }
        return std::string_view(stringPool.data() + section.titlePoolOffset, section.titleLength);
    }
};

// ---------------------------------------------------------------------
// Content sink: the parser calls these while streaming a single section's
// body so the caller's existing text-layout code (whatever currently turns
// EPUB's parsed XHTML into TextBlock/Page structures) can build pages from
// FB2 the same way. This mirrors WordStyle from file-formats.md (BOLD,
// ITALIC, UNDERLINE, STRIKETHROUGH, SUP, SUB) plus the block-level shapes
// FB2 actually has (paragraph, subtitle, poem/stanza/verse, cite, epigraph,
// empty-line, table, image).
// ---------------------------------------------------------------------
enum class Fb2InlineStyle : uint8_t {
    Regular = 0,
    Bold = 1,
    Italic = 2,
    Underline = 4,
    Strikethrough = 8,
    Superscript = 16,
    Subscript = 32,
    // FB2 has no dedicated small-caps element. Producers express it via a
    // named <style name="..."> run (often mirroring a CSS class from an
    // embedded stylesheet, e.g. name="smallcaps"/"small-caps"/"sc"). The
    // parser recognizes the common spellings — see isSmallCapsStyleName()
    // in Fb2Parser.cpp — and folds them into this bit so callers don't have
    // to duplicate that heuristic.
    SmallCaps = 64,
};
inline Fb2InlineStyle operator|(Fb2InlineStyle a, Fb2InlineStyle b) {
    return static_cast<Fb2InlineStyle>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

// Real (not text-flattened) FB2 tables: <table>/<tr>/<td|th> with the
// attributes producers actually use — colspan/rowspan for merged cells,
// align/valign for cell content, and header vs. data cell distinction —
// so a table can be laid out as an actual grid instead of collapsing to
// plain paragraphs.
struct Fb2TableCellAttrs {
    uint16_t colspan = 1;
    uint16_t rowspan = 1;
    std::string align;    // "left" | "center" | "right" | "" (unset)
    std::string valign;   // "top" | "middle" | "bottom" | "" (unset)
    bool isHeader = false; // true for <th>, false for <td>
};

class Fb2ContentSink {
public:
    virtual ~Fb2ContentSink() = default;

    virtual void onParagraphBegin() {}
    virtual void onParagraphEnd() {}
    virtual void onSubtitle(const std::string& /*text*/) {}
    virtual void onSubtitleBegin() {}
    virtual void onSubtitleEnd() {}
    virtual void onTitleBegin(uint8_t /*level*/) {}
    virtual void onTitleEnd(uint8_t /*level*/) {}
    virtual void onTitleLineBreak() {}
    virtual void onEmptyLine() {}
    virtual void onHorizontalRule() {}

    virtual void onPoemBegin() {}
    virtual void onPoemEnd() {}
    virtual void onStanzaBegin() {}
    virtual void onStanzaEnd() {}
    virtual void onVerseLine(const std::string& /*text*/) {}
    // Streaming verse callbacks preserve inline FB2 markup (especially <emphasis>)
    // while keeping each <v> on its own visual line.
    virtual void onVerseBegin() {}
    virtual void onVerseEnd() {}

    virtual void onCiteBegin() {}
    virtual void onCiteEnd() {}
    virtual void onEpigraphBegin() {}
    virtual void onEpigraphEnd() {}
    // Attribution inside <cite> or <epigraph>, distinct from quoted text.
    virtual void onTextAuthor(const std::string& /*text*/) {}
    virtual void onTextAuthorBegin() {}
    virtual void onTextAuthorEnd() {}

    // Called with successive runs of text inside the current block-level
    // element, tagged with whatever inline styles are currently active.
    // May be called multiple times per paragraph (once per style run).
    virtual void onText(const std::string& /*text*/, Fb2InlineStyle /*style*/) {}

    // href is the binary id (no leading '#') this <image> points at; caller
    // decodes it via Fb2Parser::decodeBinary() using the index from scan().
    virtual void onImage(const std::string& /*binaryId*/) {}

    // <a l:href="#n1">...</a> - typically a footnote reference. targetId has
    // any leading '#' stripped already (FB2 links are always same-document,
    // there's no cross-file href in the format). Content between begin/end
    // still flows through onText() as usual - the caller decides how (or
    // whether) to render this as a clickable link.
    // ID of the current source element, emitted after its block begins.
    virtual void onAnchor(const std::string& /*id*/) {}

    virtual void onLinkBegin(const std::string& /*targetId*/) {}
    virtual void onLinkEnd() {}

    virtual void onTableBegin() {}
    virtual void onTableEnd() {}
    virtual void onTableRowBegin() {}
    // Streaming sinks preserve inline links/styles and avoid buffering whole cells.
    virtual bool streamsTableCells() const { return false; }
    virtual void onTableCellBegin(const Fb2TableCellAttrs& /*attrs*/) {}
    virtual void onTableCellEnd() {}
    virtual void onTableCell(const std::string& /*text*/, const Fb2TableCellAttrs& /*attrs*/) {}
    virtual void onTableRowEnd() {}
};
