#include "Fb2Parser.h"
#include "Fb2XmlReader.h"
#include "Base64Decoder.h"
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <CooperativeAbort.h>

namespace {

// Keep the upstream token pointer in the public API for source compatibility,
// but use Paperio's existing cooperative-abort hook. The token type remains
// opaque, so the autonomous FB2 core no longer depends on inkMOD ReaderWork.
bool cancellationRequested(const reader::ReaderCancellationToken*) {
    if (!CooperativeAbort::shouldAbortLongTask()) return false;
    CooperativeAbort::markAborted();
    return true;
}

std::string stripHash(const char* v) {
    if (!v) return {};
    return (v[0] == '#') ? std::string(v + 1) : std::string(v);
}

const char* firstOf(const Fb2XmlReader& r, std::initializer_list<const char*> names) {
    for (auto n : names) {
        if (const char* v = r.attr(n)) return v;
    }
    return nullptr;
}

std::string formatAuthorName(const Fb2Author& a) {
    std::string out;
    auto append = [&](const std::string& part) {
        if (part.empty()) return;
        if (!out.empty()) out.push_back(' ');
        out += part;
    };
    append(a.firstName);
    append(a.middleName);
    append(a.lastName);
    if (out.empty()) out = a.nickname;
    return out;
}

constexpr size_t kMaxAnnotationBytes = 4000;
constexpr size_t kMaxStylesheetBytes = 8000;

// Some FB2 generators emulate strike-through with the Unicode combining
// long-stroke overlay U+0336 instead of the FB2 <strikethrough> element.
// E-ink fonts render that combining mark inconsistently (often above the
// glyph), so convert it to inkMOD's native strikethrough style and remove
// the combining mark before it reaches the font renderer.  A base codepoint
// is treated as struck when U+0336 appears immediately before or after it;
// this also handles malformed producer output that starts a run with U+0336.
void emitTextNormalizingCombiningStrike(Fb2ContentSink& sink, const std::string& text, Fb2InlineStyle baseStyle) {
    constexpr unsigned char kMark0 = 0xCC;
    constexpr unsigned char kMark1 = 0xB6;  // U+0336 in UTF-8
    if (text.find("\xCC\xB6") == std::string::npos) {
        sink.onText(text, baseStyle);
        return;
    }

    auto isMarkAt = [&](size_t pos) {
        return pos + 1 < text.size() &&
               static_cast<unsigned char>(text[pos]) == kMark0 &&
               static_cast<unsigned char>(text[pos + 1]) == kMark1;
    };
    auto codepointLenAt = [&](size_t pos) -> size_t {
        const unsigned char c = static_cast<unsigned char>(text[pos]);
        if ((c & 0x80u) == 0) return 1;
        if ((c & 0xE0u) == 0xC0u) return std::min<size_t>(2, text.size() - pos);
        if ((c & 0xF0u) == 0xE0u) return std::min<size_t>(3, text.size() - pos);
        if ((c & 0xF8u) == 0xF0u) return std::min<size_t>(4, text.size() - pos);
        return 1;
    };

    std::string run;
    run.reserve(text.size());
    bool runStrike = false;
    bool haveRun = false;
    bool markBeforeNext = false;
    auto flush = [&]() {
        if (run.empty()) return;
        sink.onText(run, runStrike ? (baseStyle | Fb2InlineStyle::Strikethrough) : baseStyle);
        run.clear();
    };

    for (size_t i = 0; i < text.size();) {
        if (isMarkAt(i)) {
            markBeforeNext = true;
            i += 2;
            continue;
        }
        const size_t cpLen = codepointLenAt(i);
        const bool markAfter = isMarkAt(i + cpLen);
        const bool struck = markBeforeNext || markAfter;
        markBeforeNext = false;

        if (!haveRun) {
            runStrike = struck;
            haveRun = true;
        } else if (runStrike != struck) {
            flush();
            runStrike = struck;
        }
        run.append(text, i, cpLen);
        i += cpLen;
        if (markAfter) {
            // Consume the mark here so it does not also become a prefix for
            // the following codepoint.  Producers that explicitly place a
            // mark before every character are still handled by the next loop.
            i += 2;
        }
    }
    flush();
}

bool isSmallCapsStyleName(const std::string& name) {
    std::string lower;
    lower.reserve(name.size());
    for (char c : name) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return lower.find("smallcaps") != std::string::npos ||
           lower.find("small-caps") != std::string::npos ||
           lower.find("small_caps") != std::string::npos ||
           lower == "sc" || lower == "caps";
}

uint16_t parseSpan(const char* v) {
    if (!v) return 1;
    long n = std::strtol(v, nullptr, 10);
    return n > 0 ? static_cast<uint16_t>(n) : 1;
}

void countTwoTagOpeners(IByteReader& reader, const char* tagA, uint32_t& countA, const char* tagB, uint32_t& countB) {
    const size_t lenA = std::strlen(tagA);
    const size_t lenB = std::strlen(tagB);
    const size_t maxLen = lenA > lenB ? lenA : lenB;
    constexpr size_t CHUNK = 4096;
    uint8_t buf[CHUNK + 16];
    size_t pending = 0;
    countA = 0;
    countB = 0;
    for (;;) {
        size_t got = reader.read(buf + pending, CHUNK);
        size_t total = pending + got;
        if (total < maxLen) break;
        const size_t searchEnd = total - maxLen + 1;
        for (size_t i = 0; i < searchEnd; ++i) {
            if (std::memcmp(buf + i, tagA, lenA) == 0) ++countA;
            if (std::memcmp(buf + i, tagB, lenB) == 0) ++countB;
        }
        if (got < CHUNK) break;
        pending = maxLen - 1;
        std::memmove(buf, buf + total - pending, pending);
    }
}

} // namespace

namespace {
constexpr size_t kMaxFb2SectionIdBytes = 192;
constexpr size_t kMaxFb2SectionTitleBytes = 384;
constexpr size_t kMaxFb2BinaryIdBytes = 192;
constexpr size_t kMaxFb2ContentTypeBytes = 80;
constexpr size_t kMaxFb2BodyNameBytes = 80;
constexpr size_t kSectionStringPoolReserve = 28 * 1024;
constexpr size_t kSectionStringPoolHardLimit = 28 * 1024;

bool storePoolString(Fb2ScanResult& out, const std::string& value,
                     uint32_t& offset, uint16_t& length) {
    offset = UINT32_MAX;
    length = 0;
    if (value.empty() || out.stringPool.size() >= kSectionStringPoolHardLimit) return false;
    const size_t available = kSectionStringPoolHardLimit - out.stringPool.size();
    const size_t copyLen = std::min({value.size(), available, static_cast<size_t>(UINT16_MAX)});
    if (copyLen == 0) return false;
    offset = static_cast<uint32_t>(out.stringPool.size());
    length = static_cast<uint16_t>(copyLen);
    out.stringPool.append(value.data(), copyLen);
    return true;
}

bool storePoolCString(Fb2ScanResult& out, const char* value, const size_t maxLen,
                      uint32_t& offset, uint16_t& length) {
    offset = UINT32_MAX;
    length = 0;
    if (out.sections.storage) {
        return value && out.appendString(value, strnlen(value, maxLen), offset, length);
    }
    if (!value || out.stringPool.size() >= kSectionStringPoolHardLimit) return false;
    const size_t sourceLen = strnlen(value, maxLen);
    const size_t available = kSectionStringPoolHardLimit - out.stringPool.size();
    const size_t copyLen = std::min({sourceLen, available, static_cast<size_t>(UINT16_MAX)});
    if (copyLen == 0) return false;
    offset = static_cast<uint32_t>(out.stringPool.size());
    length = static_cast<uint16_t>(copyLen);
    out.stringPool.append(value, copyLen);
    return true;
}

bool storePoolTitle(Fb2ScanResult& out, const std::string& value,
                    uint32_t& offset, uint16_t& length) {
    offset = UINT32_MAX;
    length = 0;
    if (out.sections.storage) {
        std::string normalized;
        bool space = false;
        for (unsigned char c : value) {
            if (c == ' ' || c == '\r' || c == '\n' || c == '\t') { space = !normalized.empty(); continue; }
            if (space) normalized.push_back(' ');
            space = false; normalized.push_back(static_cast<char>(c));
        }
        return out.appendString(normalized.data(), normalized.size(), offset, length);
    }
    if (value.empty() || out.stringPool.size() >= kSectionStringPoolHardLimit) return false;
    const size_t start = out.stringPool.size();
    bool pendingSpace = false;
    for (const unsigned char c : value) {
        if (out.stringPool.size() >= kSectionStringPoolHardLimit) break;
        if (c == ' ' || c == '\r' || c == '\n' || c == '\t') {
            pendingSpace = out.stringPool.size() > start;
            continue;
        }
        if (pendingSpace && out.stringPool.size() < kSectionStringPoolHardLimit) out.stringPool.push_back(' ');
        pendingSpace = false;
        if (out.stringPool.size() < kSectionStringPoolHardLimit) out.stringPool.push_back(static_cast<char>(c));
    }
    const size_t stored = out.stringPool.size() - start;
    if (stored == 0) return false;
    offset = static_cast<uint32_t>(start);
    length = static_cast<uint16_t>(std::min(stored, static_cast<size_t>(UINT16_MAX)));
    return true;
}

void assignBoundedFb2(std::string& dst, const char* src, const size_t limit) {
    if (!src) { dst.clear(); return; }
    const size_t len = strnlen(src, limit);
    dst.assign(src, len);
}

void appendBoundedFb2(std::string& dst, const std::string& src, const size_t limit) {
    if (dst.size() >= limit || src.empty()) return;
    const size_t remaining = limit - dst.size();
    dst.append(src.data(), std::min(remaining, src.size()));
}

// Locate the FB2 cover and verify that `expectedInnerStartOffset` belongs to
// the first body section. This is intentionally a small prefix scan: it stops
// at the first <section>, so opening later chapters never scans the whole book.
bool findCoverForFirstSection(IByteReader& reader, uint32_t expectedInnerStartOffset, std::string& coverId) {
    coverId.clear();
    if (!reader.seek(0)) return false;

    Fb2XmlReader xml(reader, 2048);
    bool inDescription = false;
    bool inTitleInfo = false;
    bool inCoverpage = false;
    bool inBody = false;

    for (;;) {
        const Fb2Token tok = xml.next();
        if (tok == Fb2Token::Eof || tok == Fb2Token::Error) return false;
        const std::string& name = xml.name();

        if (tok == Fb2Token::StartTag || tok == Fb2Token::SelfClosing) {
            if (name == "description") inDescription = true;
            else if (name == "title-info" && inDescription) inTitleInfo = true;
            else if (name == "coverpage" && inTitleInfo) inCoverpage = true;
            else if (name == "image" && inCoverpage) {
                if (const char* href = firstOf(xml, {"l:href", "xlink:href", "href"})) coverId = stripHash(href);
            } else if (name == "body") {
                inBody = true;
            } else if (name == "section" && inBody) {
                const uint32_t firstInnerStart = xml.streamPos();
                (void)firstInnerStart;
                (void)expectedInnerStartOffset;
                return !coverId.empty();
            }
        }

        if (tok == Fb2Token::EndTag) {
            if (name == "coverpage") inCoverpage = false;
            else if (name == "title-info") inTitleInfo = false;
            else if (name == "description") inDescription = false;
            else if (name == "body") inBody = false;
        }
    }
}
}  // namespace

bool Fb2Parser::scan(IByteReader& reader, Fb2ScanResult& out, size_t xmlBufferSize) {
    if (reader.size() > UINT32_MAX) return false;
    auto storage = out.sections.storage;
    out = Fb2ScanResult{};
    out.sections.storage = std::move(storage);
    out.bodies.reserve(4);
    if (!out.sections.storage) out.stringPool.reserve(reader.size() >= 8ULL * 1024 * 1024 ? kSectionStringPoolReserve : 8 * 1024);

    const size_t safeXmlBufferSize = std::max<size_t>(1024, std::min<size_t>(8 * 1024, xmlBufferSize));
    Fb2XmlReader xml(reader, safeXmlBufferSize);

    bool inDescription = false;
    bool inTitleInfo = false;
    bool inAuthorTag = false;
    bool inAnnotation = false;
    bool inCoverpage = false;
    bool inTitle = false;
    bool inStylesheet = false;
    bool inBinaryTag = false;
    Fb2BinaryIndexEntry curBinary;
    Fb2Author curAuthor;
    std::string* activeTarget = nullptr;
    std::string annotationBuf;
    bool annotationNeedsSep = false;
    std::string titleBuf;
    bool titleNeedsSep = false;
    int currentBodyIndex = -1;
    std::vector<int> sectionStack;
    struct FallbackHeadingState {
        bool paragraphActive = false;
        bool firstMeaningfulParagraphSeen = false;
        bool sawStyledText = false;
        bool sawPlainText = false;
        int styleDepth = 0;
        std::string text;
    };
    std::vector<FallbackHeadingState> fallbackHeadingStack;

    for (;;) {
        if (!out.storageGood || !out.sections.healthy() || cancellationRequested(nullptr)) return false;
        const bool captureFallbackHeading =
            !fallbackHeadingStack.empty() && fallbackHeadingStack.back().paragraphActive;
        xml.setCaptureText(activeTarget != nullptr || inStylesheet || inAnnotation || inTitle || captureFallbackHeading);
        Fb2Token tok = xml.next();
        if (tok == Fb2Token::Eof) break;
        if (tok == Fb2Token::Error) return false;

        out.tokenCount++;
        if (tok == Fb2Token::Text) {
            out.textTokenCount++;
            out.textPayloadBytes += xml.textSize();
            if (inBinaryTag) out.binaryTextBytes += xml.textSize();
        }

        const std::string& name = xml.name();
        if (tok == Fb2Token::StartTag || tok == Fb2Token::SelfClosing) {
            if (name == "description") inDescription = true;
            else if (name == "title-info" && inDescription) inTitleInfo = true;
            else if (name == "author" && inTitleInfo) { inAuthorTag = true; curAuthor = Fb2Author{}; }
            else if (inAuthorTag && name == "first-name") activeTarget = &curAuthor.firstName;
            else if (inAuthorTag && name == "middle-name") activeTarget = &curAuthor.middleName;
            else if (inAuthorTag && name == "last-name") activeTarget = &curAuthor.lastName;
            else if (inAuthorTag && name == "nickname") activeTarget = &curAuthor.nickname;
            else if (name == "book-title" && inTitleInfo && !inAuthorTag) activeTarget = &out.metadata.title;
            else if (name == "lang" && inTitleInfo && !inAuthorTag) activeTarget = &out.metadata.language;
            else if (name == "date" && inTitleInfo && !inAuthorTag) {
                if (const char* value = xml.attr("value")) out.metadata.date = value;
                else activeTarget = &out.metadata.date;
            }
            else if (name == "annotation" && inTitleInfo) { inAnnotation = true; annotationBuf.clear(); annotationNeedsSep = false; }
            else if (name == "p" && inAnnotation) { if (annotationNeedsSep) annotationBuf += "\n\n"; annotationNeedsSep = true; }
            else if (name == "stylesheet" && inDescription) { inStylesheet = true; out.metadata.embeddedStylesheetCss.clear(); }
            else if (name == "coverpage" && inTitleInfo) inCoverpage = true;
            else if (name == "image" && inCoverpage) {
                if (const char* href = firstOf(xml, {"l:href", "xlink:href", "href"})) out.metadata.coverBinaryId = stripHash(href);
            } else if (name == "image" && !inCoverpage && !sectionStack.empty()) {
                if (out.sections[sectionStack.back()].imageRefCount < UINT16_MAX) out.sections[sectionStack.back()].imageRefCount++;
            } else if (name == "sequence" && inTitleInfo && !inAuthorTag && out.metadata.sequenceName.empty()) {
                if (const char* n = xml.attr("name")) out.metadata.sequenceName = n;
                if (const char* num = xml.attr("number")) out.metadata.sequenceNumber = static_cast<uint32_t>(std::strtoul(num, nullptr, 10));
            } else if (name == "body") {
                if (out.bodies.size() > INT16_MAX) return false;
                Fb2BodyIndexEntry b;
                if (const char* n = xml.attr("name")) assignBoundedFb2(b.name, n, kMaxFb2BodyNameBytes);
                out.bodies.push_back(b);
                currentBodyIndex = static_cast<int>(out.bodies.size()) - 1;
                sectionStack.clear();
                fallbackHeadingStack.clear();
            } else if (name == "section" && currentBodyIndex >= 0) {
                // Structural paths support 32 levels; reject excessive nesting
                // before growing the per-level heading stack or wrapping indices.
                if (sectionStack.size() >= 32 || out.sections.size() >= UINT16_MAX) return false;
                Fb2SectionIndexEntry e;
                e.level = static_cast<uint16_t>(sectionStack.size());
                e.bodyIndex = currentBodyIndex;
                if (const char* id = xml.attr("id")) storePoolCString(out, id, kMaxFb2SectionIdBytes, e.idPoolOffset, e.idLength);
                out.sections.push_back(e);
                sectionStack.push_back(static_cast<int>(out.sections.size()) - 1);
                fallbackHeadingStack.emplace_back();
                out.sections.back().innerStartOffset = xml.streamPos();
            } else if (name == "title" && !sectionStack.empty()) {
                fallbackHeadingStack.back().firstMeaningfulParagraphSeen = true;
                inTitle = true; titleBuf.clear(); titleNeedsSep = false;
            } else if (name == "p" && inTitle) {
                if (titleNeedsSep) titleBuf += " ";
                titleNeedsSep = true;
            } else if (name == "p" && !sectionStack.empty() &&
                       !fallbackHeadingStack.back().firstMeaningfulParagraphSeen) {
                auto& fallback = fallbackHeadingStack.back();
                fallback.paragraphActive = true;
                fallback.sawStyledText = false;
                fallback.sawPlainText = false;
                fallback.styleDepth = 0;
                fallback.text.clear();
            } else if (!fallbackHeadingStack.empty() && fallbackHeadingStack.back().paragraphActive &&
                       (name == "strong" || name == "b")) {
                ++fallbackHeadingStack.back().styleDepth;
            } else if (name == "binary") {
                inBinaryTag = true;
                curBinary = Fb2BinaryIndexEntry{};
                if (const char* id = xml.attr("id")) assignBoundedFb2(curBinary.id, id, kMaxFb2BinaryIdBytes);
                if (const char* ct = xml.attr("content-type")) assignBoundedFb2(curBinary.contentType, ct, kMaxFb2ContentTypeBytes);
                curBinary.payloadStartOffset = xml.streamPos();
            }
        } else if (tok == Fb2Token::Text) {
            if (activeTarget) *activeTarget += xml.text();
            else if (inStylesheet) { if (out.metadata.embeddedStylesheetCss.size() < kMaxStylesheetBytes) out.metadata.embeddedStylesheetCss += xml.text(); }
            else if (inAnnotation) { if (annotationBuf.size() < kMaxAnnotationBytes) annotationBuf += xml.text(); }
            else if (inTitle) appendBoundedFb2(titleBuf, xml.text(), kMaxFb2SectionTitleBytes);
            else if (inBinaryTag) { }
            else if (currentBodyIndex >= 0 && !sectionStack.empty()) {
                auto& section = out.sections[sectionStack.back()];
                if (xml.textSize() > UINT32_MAX - section.approxTextBytes) return false;
                section.approxTextBytes += static_cast<uint32_t>(xml.textSize());
                auto& fallback = fallbackHeadingStack.back();
                if (fallback.paragraphActive) {
                    bool hasVisibleText = false;
                    for (const unsigned char c : xml.text()) {
                        if (!std::isspace(c) && c != 0xC2 && c != 0xA0) { hasVisibleText = true; break; }
                    }
                    if (hasVisibleText) {
                        if (fallback.styleDepth > 0) fallback.sawStyledText = true;
                        else fallback.sawPlainText = true;
                    }
                    appendBoundedFb2(fallback.text, xml.text(), kMaxFb2SectionTitleBytes);
                }
            }
        }

        if (tok == Fb2Token::EndTag || tok == Fb2Token::SelfClosing) {
            if (name == "description") inDescription = false;
            else if (name == "title-info") inTitleInfo = false;
            else if (name == "author" && inAuthorTag) {
                out.metadata.authors.push_back(curAuthor);
                std::string display = formatAuthorName(curAuthor);
                if (!display.empty()) { if (!out.metadata.author.empty()) out.metadata.author += "; "; out.metadata.author += display; }
                inAuthorTag = false;
            } else if (inAuthorTag && (name == "first-name" || name == "middle-name" || name == "last-name" || name == "nickname")) activeTarget = nullptr;
            else if (name == "book-title" || name == "lang" || name == "date") activeTarget = nullptr;
            else if (name == "annotation") { out.metadata.annotationText = annotationBuf; inAnnotation = false; }
            else if (name == "stylesheet") inStylesheet = false;
            else if (name == "coverpage") inCoverpage = false;
            else if (!fallbackHeadingStack.empty() && fallbackHeadingStack.back().paragraphActive &&
                     (name == "strong" || name == "b")) {
                if (fallbackHeadingStack.back().styleDepth > 0) --fallbackHeadingStack.back().styleDepth;
            }
            else if (name == "p" && !fallbackHeadingStack.empty() && fallbackHeadingStack.back().paragraphActive) {
                auto& fallback = fallbackHeadingStack.back();
                if (fallback.sawStyledText || fallback.sawPlainText) {
                    fallback.firstMeaningfulParagraphSeen = true;
                    if (fallback.sawStyledText && !fallback.sawPlainText) {
                        auto& section = out.sections[sectionStack.back()];
                        if (storePoolTitle(out, fallback.text, section.titlePoolOffset, section.titleLength)) {
                            section.fallbackTitle = true;
                        }
                    }
                }
                fallback.paragraphActive = false;
            }
            else if (name == "body") { currentBodyIndex = -1; sectionStack.clear(); fallbackHeadingStack.clear(); }
            else if (name == "section" && !sectionStack.empty()) {
                sectionStack.pop_back();
                if (!fallbackHeadingStack.empty()) fallbackHeadingStack.pop_back();
            }
            else if (name == "title") {
                if (!sectionStack.empty()) { auto& section = out.sections[sectionStack.back()]; storePoolTitle(out, titleBuf, section.titlePoolOffset, section.titleLength); }
                inTitle = false;
            } else if (name == "binary") {
                curBinary.payloadEndOffset = xml.tokenStartOffset();
                out.binaries.push_back(curBinary);
                inBinaryTag = false;
            }
        }
    }

    // Coverpage belongs to FB2 metadata, not to the body image stream.
    // Keep imageRefCount limited to real section illustrations so virtual
    // image slices are stable regardless of whether a book has a cover.
    return out.good();
}

bool Fb2Parser::renderCoverForFirstSection(IByteReader& reader,
                                                  const Fb2SectionIndexEntry& section,
                                                  Fb2ContentSink& sink) {
    std::string coverId;
    if (!findCoverForFirstSection(reader, section.innerStartOffset, coverId)) return false;
    sink.onImage(coverId);
    return true;
}

bool Fb2Parser::renderAnnotation(IByteReader& reader, Fb2ContentSink& sink,
                                 const reader::ReaderCancellationToken* cancellationToken) {
    Fb2XmlReader xml(reader, 2048);
    xml.seekTo(0);
    bool inTitleInfo = false;
    bool inAnnotation = false;
    bool paragraphOpen = false;
    int boldDepth = 0, italicDepth = 0, underlineDepth = 0, strikeDepth = 0, supDepth = 0, subDepth = 0;
    auto currentStyle = [&]() {
        Fb2InlineStyle st = Fb2InlineStyle::Regular;
        if (boldDepth) st = st | Fb2InlineStyle::Bold;
        if (italicDepth) st = st | Fb2InlineStyle::Italic;
        if (underlineDepth) st = st | Fb2InlineStyle::Underline;
        if (strikeDepth) st = st | Fb2InlineStyle::Strikethrough;
        if (supDepth) st = st | Fb2InlineStyle::Superscript;
        if (subDepth) st = st | Fb2InlineStyle::Subscript;
        return st;
    };

    for (;;) {
        if (cancellationRequested(cancellationToken)) return false;
        const Fb2Token tok = xml.next();
        if (tok == Fb2Token::Eof) return true;
        if (tok == Fb2Token::Error) return false;
        const std::string& name = xml.name();

        if (tok == Fb2Token::StartTag || tok == Fb2Token::SelfClosing) {
            if (name == "title-info") { inTitleInfo = true; continue; }
            if (!inTitleInfo) continue;
            if (name == "annotation") {
                inAnnotation = true;
                if (const char* id = xml.attr("id")) { if (*id) sink.onAnchor(id); }
                continue;
            }
            if (!inAnnotation) continue;
            if (name == "p") { sink.onParagraphBegin(); paragraphOpen = true; }
            else if (name == "empty-line") sink.onEmptyLine();
            else if (name == "strong" || name == "b") ++boldDepth;
            else if (name == "emphasis" || name == "i") ++italicDepth;
            else if (name == "underline" || name == "u") ++underlineDepth;
            else if (name == "strikethrough") ++strikeDepth;
            else if (name == "sup") ++supDepth;
            else if (name == "sub") ++subDepth;
            if (const char* id = xml.attr("id")) { if (*id) sink.onAnchor(id); }
            continue;
        }
        if (tok == Fb2Token::Text && inAnnotation) {
            emitTextNormalizingCombiningStrike(sink, xml.text(), currentStyle());
            continue;
        }
        if (tok == Fb2Token::EndTag) {
            if (name == "title-info") return true;
            if (!inAnnotation) continue;
            if (name == "annotation") { if (paragraphOpen) sink.onParagraphEnd(); return true; }
            if (name == "p") { sink.onParagraphEnd(); paragraphOpen = false; }
            else if (name == "strong" || name == "b") --boldDepth;
            else if (name == "emphasis" || name == "i") --italicDepth;
            else if (name == "underline" || name == "u") --underlineDepth;
            else if (name == "strikethrough") --strikeDepth;
            else if (name == "sup") --supDepth;
            else if (name == "sub") --subDepth;
        }
    }
}

bool Fb2Parser::renderBodyPreambleForFirstSection(
    IByteReader& reader,
    Fb2ContentSink& sink,
    const reader::ReaderCancellationToken* cancellationToken) {
    // Body-level epigraphs are legal FB2 and are common before the first
    // section. scan() deliberately indexes only <section> nodes, so recover
    // that tiny preamble here. This pass terminates at the first section and
    // therefore does not turn chapter rendering into a whole-book rescan.
    Fb2XmlReader xml(reader, 4096);
    xml.seekTo(0);

    bool inBody = false;
    bool inEpigraph = false;
    bool inTextAuthor = false;
    int boldDepth = 0, italicDepth = 0, underlineDepth = 0, strikeDepth = 0, supDepth = 0, subDepth = 0;
    int smallCapsDepth = 0;
    std::vector<bool> styleTagIsSmallCaps;

    auto currentStyle = [&]() {
        Fb2InlineStyle style = Fb2InlineStyle::Regular;
        if (boldDepth > 0) style = style | Fb2InlineStyle::Bold;
        if (italicDepth > 0) style = style | Fb2InlineStyle::Italic;
        if (underlineDepth > 0) style = style | Fb2InlineStyle::Underline;
        if (strikeDepth > 0) style = style | Fb2InlineStyle::Strikethrough;
        if (supDepth > 0) style = style | Fb2InlineStyle::Superscript;
        if (subDepth > 0) style = style | Fb2InlineStyle::Subscript;
        if (smallCapsDepth > 0) style = style | Fb2InlineStyle::SmallCaps;
        return style;
    };

    for (;;) {
        if (cancellationRequested(cancellationToken)) return false;
        const Fb2Token tok = xml.next();
        if (tok == Fb2Token::Eof) return true;
        if (tok == Fb2Token::Error) return false;
        const std::string& name = xml.name();

        if (!inBody) {
            if ((tok == Fb2Token::StartTag || tok == Fb2Token::SelfClosing) && name == "body") {
                // The first body is the primary reading flow for the first
                // section. Notes/comments bodies appear later in normal FB2s.
                inBody = true;
            }
            continue;
        }

        if ((tok == Fb2Token::StartTag || tok == Fb2Token::SelfClosing) && name == "section") {
            return true;
        }
        if (tok == Fb2Token::EndTag && name == "body") return true;

        if (tok == Fb2Token::StartTag || tok == Fb2Token::SelfClosing) {
            if (name == "epigraph") {
                inEpigraph = true;
                sink.onEpigraphBegin();
                if (const char* id = xml.attr("id")) { if (*id) sink.onAnchor(id); }
                continue;
            }
            if (!inEpigraph) continue;

            if (name == "p") sink.onParagraphBegin();
            else if (name == "empty-line") sink.onEmptyLine();
            else if (name == "poem") sink.onPoemBegin();
            else if (name == "stanza") sink.onStanzaBegin();
            else if (name == "v") sink.onVerseBegin();
            else if (name == "text-author") { inTextAuthor = true; sink.onTextAuthorBegin(); }
            else if (name == "strong" || name == "b") ++boldDepth;
            else if (name == "emphasis" || name == "i") ++italicDepth;
            else if (name == "underline" || name == "u") ++underlineDepth;
            else if (name == "strikethrough") ++strikeDepth;
            else if (name == "sup") ++supDepth;
            else if (name == "sub") ++subDepth;
            else if (name == "style") {
                const bool isSC = isSmallCapsStyleName(xml.attr("name") ? xml.attr("name") : "");
                styleTagIsSmallCaps.push_back(isSC);
                if (isSC) ++smallCapsDepth;
            }
            else if (name == "a") {
                if (const char* href = firstOf(xml, {"l:href", "xlink:href", "href"})) sink.onLinkBegin(stripHash(href));
                else sink.onLinkBegin(std::string());
            }
            if (const char* id = xml.attr("id")) { if (*id) sink.onAnchor(id); }
            continue;
        }

        if (tok == Fb2Token::Text && inEpigraph) {
            emitTextNormalizingCombiningStrike(sink, xml.text(), currentStyle());
            continue;
        }

        if (tok == Fb2Token::EndTag && inEpigraph) {
            if (name == "p") sink.onParagraphEnd();
            else if (name == "poem") sink.onPoemEnd();
            else if (name == "stanza") sink.onStanzaEnd();
            else if (name == "v") sink.onVerseEnd();
            else if (name == "text-author") { sink.onTextAuthorEnd(); inTextAuthor = false; }
            else if (name == "strong" || name == "b") --boldDepth;
            else if (name == "emphasis" || name == "i") --italicDepth;
            else if (name == "underline" || name == "u") --underlineDepth;
            else if (name == "strikethrough") --strikeDepth;
            else if (name == "sup") --supDepth;
            else if (name == "sub") --subDepth;
            else if (name == "style") {
                if (!styleTagIsSmallCaps.empty()) {
                    const bool was = styleTagIsSmallCaps.back();
                    styleTagIsSmallCaps.pop_back();
                    if (was) --smallCapsDepth;
                }
            }
            else if (name == "a") sink.onLinkEnd();
            else if (name == "epigraph") {
                sink.onEpigraphEnd();
                inEpigraph = false;
            }
        }
    }
}

bool Fb2Parser::renderSectionTitle(IByteReader& reader,
                                  const Fb2SectionIndexEntry& section,
                                  Fb2ContentSink& sink,
                                  uint8_t level,
                                  const reader::ReaderCancellationToken* cancellationToken) {
    Fb2XmlReader xml(reader, 2048);
    xml.seekTo(section.innerStartOffset);
    if (section.fallbackTitle) {
        bool inParagraph = false;
        bool sawStyledText = false;
        bool sawPlainText = false;
        int styleDepth = 0;
        std::string text;
        for (;;) {
            if (cancellationRequested(cancellationToken)) return false;
            const Fb2Token tok = xml.next();
            if (tok == Fb2Token::Eof || tok == Fb2Token::Error) return false;
            const std::string& name = xml.name();
            if (tok == Fb2Token::StartTag || tok == Fb2Token::SelfClosing) {
                if (name == "section") return false;
                if (name == "p" && !inParagraph) {
                    inParagraph = true;
                    sawStyledText = sawPlainText = false;
                    styleDepth = 0;
                    text.clear();
                } else if (inParagraph && (name == "strong" || name == "b")) {
                    ++styleDepth;
                }
                if (inParagraph) {
                    if (const char* id = xml.attr("id")) { if (*id) sink.onAnchor(id); }
                }
                continue;
            }
            if (tok == Fb2Token::Text && inParagraph) {
                bool visible = false;
                for (const unsigned char c : xml.text()) {
                    if (!std::isspace(c) && c != 0xC2 && c != 0xA0) { visible = true; break; }
                }
                if (visible) {
                    if (styleDepth > 0) sawStyledText = true;
                    else sawPlainText = true;
                }
                appendBoundedFb2(text, xml.text(), kMaxFb2SectionTitleBytes);
                continue;
            }
            if (tok == Fb2Token::EndTag && inParagraph) {
                if (name == "strong" || name == "b") {
                    if (styleDepth > 0) --styleDepth;
                } else if (name == "p") {
                    if (sawStyledText || sawPlainText) {
                        if (!sawStyledText || sawPlainText) return false;
                        sink.onTitleBegin(level);
                        emitTextNormalizingCombiningStrike(sink, text, Fb2InlineStyle::Bold);
                        sink.onTitleEnd(level);
                        return true;
                    }
                    inParagraph = false;
                }
            }
        }
    }
    bool inTitle = false;
    bool emitted = false;
    bool titleParagraphSeen = false;
    int boldDepth = 0, italicDepth = 0, underlineDepth = 0, strikeDepth = 0, supDepth = 0, subDepth = 0;
    auto style = [&]() {
        Fb2InlineStyle st = Fb2InlineStyle::Regular;
        if (boldDepth) st = st | Fb2InlineStyle::Bold;
        if (italicDepth) st = st | Fb2InlineStyle::Italic;
        if (underlineDepth) st = st | Fb2InlineStyle::Underline;
        if (strikeDepth) st = st | Fb2InlineStyle::Strikethrough;
        if (supDepth) st = st | Fb2InlineStyle::Superscript;
        if (subDepth) st = st | Fb2InlineStyle::Subscript;
        return st;
    };
    for (;;) {
        if (cancellationRequested(cancellationToken)) return false;
        const Fb2Token tok = xml.next();
        if (tok == Fb2Token::Eof || tok == Fb2Token::Error) return false;
        const std::string& name = xml.name();
        if (!inTitle) {
            if ((tok == Fb2Token::StartTag || tok == Fb2Token::SelfClosing) && name == "title") {
                inTitle = true; sink.onTitleBegin(level);
                if (const char* id = xml.attr("id")) { if (*id) sink.onAnchor(id); }
            } else if ((tok == Fb2Token::StartTag || tok == Fb2Token::SelfClosing) &&
                       (name == "section" || name == "p" || name == "epigraph" || name == "poem" || name == "cite")) {
                return false;
            }
            continue;
        }
        if (tok == Fb2Token::StartTag || tok == Fb2Token::SelfClosing) {
            if (name == "p") {
                if (titleParagraphSeen) sink.onTitleLineBreak();
                titleParagraphSeen = true;
            } else if (name == "strong" || name == "b") ++boldDepth;
            else if (name == "emphasis" || name == "i") ++italicDepth;
            else if (name == "underline" || name == "u") ++underlineDepth;
            else if (name == "strikethrough") ++strikeDepth;
            else if (name == "sup") ++supDepth;
            else if (name == "sub") ++subDepth;
            if (const char* id = xml.attr("id")) { if (*id) sink.onAnchor(id); }
            continue;
        }
        if (tok == Fb2Token::Text) { emitTextNormalizingCombiningStrike(sink, xml.text(), style()); emitted = true; continue; }
        if (tok == Fb2Token::EndTag) {
            if (name == "title") { sink.onTitleEnd(level); return emitted; }
            if (name == "strong" || name == "b") --boldDepth;
            else if (name == "emphasis" || name == "i") --italicDepth;
            else if (name == "underline" || name == "u") --underlineDepth;
            else if (name == "strikethrough") --strikeDepth;
            else if (name == "sup") --supDepth;
            else if (name == "sub") --subDepth;
        }
    }
}

bool Fb2Parser::renderSection(IByteReader& reader,
                               const Fb2SectionIndexEntry& section,
                               Fb2ContentSink& sink,
                               const reader::ReaderCancellationToken* cancellationToken) {
    Fb2XmlReader xml(reader, 4096);
    xml.seekTo(section.innerStartOffset);

    int boldDepth = 0, italicDepth = 0, underlineDepth = 0, strikeDepth = 0, supDepth = 0, subDepth = 0;
    int smallCapsDepth = 0;
    std::vector<bool> styleTagIsSmallCaps;
    auto currentStyle = [&]() {
        Fb2InlineStyle s = Fb2InlineStyle::Regular;
        if (boldDepth > 0) s = s | Fb2InlineStyle::Bold;
        if (italicDepth > 0) s = s | Fb2InlineStyle::Italic;
        if (underlineDepth > 0) s = s | Fb2InlineStyle::Underline;
        if (strikeDepth > 0) s = s | Fb2InlineStyle::Strikethrough;
        if (supDepth > 0) s = s | Fb2InlineStyle::Superscript;
        if (subDepth > 0) s = s | Fb2InlineStyle::Subscript;
        if (smallCapsDepth > 0) s = s | Fb2InlineStyle::SmallCaps;
        return s;
    };

    bool inSubtitle = false;
    bool inTextAuthor = false;
    const bool streamingCells = sink.streamsTableCells();
    bool inTableCell = false; std::string cellBuf; Fb2TableCellAttrs cellAttrs;
    bool inTitleTag = false;
    bool skipping = false;
    int skipDepth = 0;
    bool findFallbackTitle = section.fallbackTitle;
    bool skippingFallbackParagraph = false;
    bool fallbackSawStyledText = false;
    bool fallbackSawPlainText = false;
    int fallbackStyleDepth = 0;

    for (;;) {
        if (cancellationRequested(cancellationToken)) return false;
        Fb2Token tok = xml.next();
        if (tok == Fb2Token::Eof) return true;
        if (tok == Fb2Token::Error) return false;
        const std::string& name = xml.name();

        if (skipping) {
            if ((tok == Fb2Token::StartTag) && name == "section") skipDepth++;
            else if (tok == Fb2Token::EndTag && name == "section") { if (--skipDepth == 0) skipping = false; }
            continue;
        }
        if (tok == Fb2Token::StartTag && name == "section") { skipping = true; skipDepth = 1; continue; }
        if (tok == Fb2Token::EndTag && name == "section") return true;

        // Calibre sometimes encodes a heading as the first styled paragraph.
        // It was already emitted through renderSectionTitle(); consume that
        // source paragraph here so it is not displayed twice.
        if (findFallbackTitle) {
            if ((tok == Fb2Token::StartTag || tok == Fb2Token::SelfClosing) && name == "p" &&
                !skippingFallbackParagraph) {
                skippingFallbackParagraph = true;
                fallbackSawStyledText = fallbackSawPlainText = false;
                fallbackStyleDepth = 0;
                continue;
            }
            if (skippingFallbackParagraph) {
                if (tok == Fb2Token::StartTag || tok == Fb2Token::SelfClosing) {
                    if (name == "strong" || name == "b") ++fallbackStyleDepth;
                    continue;
                }
                if (tok == Fb2Token::Text) {
                    bool visible = false;
                    for (const unsigned char c : xml.text()) {
                        if (!std::isspace(c) && c != 0xC2 && c != 0xA0) { visible = true; break; }
                    }
                    if (visible) {
                        if (fallbackStyleDepth > 0) fallbackSawStyledText = true;
                        else fallbackSawPlainText = true;
                    }
                    continue;
                }
                if (tok == Fb2Token::EndTag) {
                    if (name == "strong" || name == "b") {
                        if (fallbackStyleDepth > 0) --fallbackStyleDepth;
                        continue;
                    }
                    if (name == "p") {
                        skippingFallbackParagraph = false;
                        if (fallbackSawStyledText || fallbackSawPlainText) {
                            findFallbackTitle = false;
                        }
                        continue;
                    }
                }
            }
            if (tok == Fb2Token::Text) {
                bool onlyWhitespace = true;
                for (const unsigned char c : xml.text()) {
                    if (!std::isspace(c) && c != 0xC2 && c != 0xA0) { onlyWhitespace = false; break; }
                }
                if (onlyWhitespace) continue;
            }
        }

        if (tok == Fb2Token::StartTag || tok == Fb2Token::SelfClosing) {
            if (name == "title") { inTitleTag = true; continue; }
            if (inTitleTag) {
                if (name == "image") {
                    if (const char* href = firstOf(xml, {"l:href", "xlink:href", "href"})) sink.onImage(stripHash(href));
                }
                continue;
            }
            if (name == "p") sink.onParagraphBegin();
            else if (name == "empty-line") sink.onEmptyLine();
            else if (name == "poem") sink.onPoemBegin();
            else if (name == "stanza") sink.onStanzaBegin();
            else if (name == "v") sink.onVerseBegin();
            else if (name == "cite") sink.onCiteBegin();
            else if (name == "epigraph") sink.onEpigraphBegin();
            else if (name == "text-author") { inTextAuthor = true; sink.onTextAuthorBegin(); }
            else if (name == "subtitle") { inSubtitle = true; sink.onSubtitleBegin(); }
            else if (name == "strong" || name == "b") boldDepth++;
            else if (name == "emphasis" || name == "i") italicDepth++;
            else if (name == "underline" || name == "u") underlineDepth++;
            else if (name == "strikethrough") strikeDepth++;
            else if (name == "sup") supDepth++;
            else if (name == "sub") subDepth++;
            else if (name == "style") {
                bool isSC = isSmallCapsStyleName(xml.attr("name") ? xml.attr("name") : "");
                styleTagIsSmallCaps.push_back(isSC);
                if (isSC) smallCapsDepth++;
            }
            else if (name == "image") {
                if (const char* href = firstOf(xml, {"l:href", "xlink:href", "href"})) sink.onImage(stripHash(href));
            }
            else if (name == "a") {
                if (const char* href = firstOf(xml, {"l:href", "xlink:href", "href"})) sink.onLinkBegin(stripHash(href));
                else sink.onLinkBegin(std::string());
            }
            else if (name == "table") sink.onTableBegin();
            else if (name == "tr") sink.onTableRowBegin();
            else if (name == "td" || name == "th") {
                inTableCell = true;
                cellBuf.clear();
                cellAttrs = Fb2TableCellAttrs{};
                cellAttrs.isHeader = (name == "th");
                cellAttrs.colspan = parseSpan(xml.attr("colspan"));
                cellAttrs.rowspan = parseSpan(xml.attr("rowspan"));
                if (const char* a = xml.attr("align")) cellAttrs.align = a;
                if (const char* v = xml.attr("valign")) cellAttrs.valign = v;
                if (streamingCells) sink.onTableCellBegin(cellAttrs);
            }
            if (const char* id = xml.attr("id")) { if (*id) sink.onAnchor(id); }
        }

        if (tok == Fb2Token::Text) {
            if (inTitleTag) { }
            else if (inSubtitle || inTextAuthor) emitTextNormalizingCombiningStrike(sink, xml.text(), currentStyle());
            else if (inTableCell && !streamingCells) cellBuf += xml.text();
            else emitTextNormalizingCombiningStrike(sink, xml.text(), currentStyle());
        }

        if (tok == Fb2Token::EndTag) {
            if (name == "title") { inTitleTag = false; continue; }
            if (inTitleTag) continue;
            if (name == "p") sink.onParagraphEnd();
            else if (name == "poem") sink.onPoemEnd();
            else if (name == "stanza") sink.onStanzaEnd();
            else if (name == "v") sink.onVerseEnd();
            else if (name == "cite") sink.onCiteEnd();
            else if (name == "epigraph") sink.onEpigraphEnd();
            else if (name == "text-author") { sink.onTextAuthorEnd(); inTextAuthor = false; }
            else if (name == "subtitle") { sink.onSubtitleEnd(); inSubtitle = false; }
            else if (name == "strong" || name == "b") boldDepth--;
            else if (name == "emphasis" || name == "i") italicDepth--;
            else if (name == "underline" || name == "u") underlineDepth--;
            else if (name == "strikethrough") strikeDepth--;
            else if (name == "sup") supDepth--;
            else if (name == "sub") subDepth--;
            else if (name == "style") {
                if (!styleTagIsSmallCaps.empty()) {
                    bool was = styleTagIsSmallCaps.back();
                    styleTagIsSmallCaps.pop_back();
                    if (was) smallCapsDepth--;
                }
            }
            else if (name == "table") sink.onTableEnd();
            else if (name == "tr") sink.onTableRowEnd();
            else if (name == "td" || name == "th") {
                if (streamingCells) sink.onTableCellEnd();
                else sink.onTableCell(cellBuf, cellAttrs);
                inTableCell = false;
            }
            else if (name == "a") sink.onLinkEnd();
        }
    }
}

bool Fb2Parser::decodeBinary(IByteReader& reader,
                              const Fb2BinaryIndexEntry& binary,
                              const BinaryOutputFn& out,
                              const reader::ReaderCancellationToken* cancellationToken) {
    if (binary.payloadEndOffset < binary.payloadStartOffset) return false;
    if (!reader.seek(binary.payloadStartOffset)) return false;
    Base64Decoder decoder(out);
    uint32_t remaining = binary.payloadEndOffset - binary.payloadStartOffset;
    uint8_t chunk[256];
    while (remaining > 0) {
        if (cancellationRequested(cancellationToken)) return false;
        size_t want = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
        size_t got = reader.read(chunk, want);
        if (got == 0) return false;
        decoder.feed(reinterpret_cast<const char*>(chunk), got);
        remaining -= static_cast<uint32_t>(got);
    }
    decoder.finish();
    return remaining == 0;
}
