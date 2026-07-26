#pragma once
// Per-block (and per-spine-aux) serialization primitives for the WBC1 content.bin format, shared by
// the whole-book writer/reader (CompiledContent.cpp) and the STREAMING writer/reader
// (ContentBinWriter / BlockStreamReader). Factoring these out keeps the on-disk block encoding in
// ONE place so the streaming path and the whole-book path can never drift.
//
// A "block record" here is exactly what writeContentBin emits per Block: type + styleId + flags +
// charOffset + the type-specific body + the shared footnote/xpath tail. No spine framing — the
// caller owns spine headers, anchor/label sections, style pool, index, and chapters.

#include <HalStorage.h>  // FsFile

#include "CompiledContent.h"

namespace compiled {

// Serialize one Block record (all fields; type-dispatched body + shared tail). Returns false on I/O
// error. Mirrors readBlock exactly.
bool writeBlock(FsFile& out, const Block& b);

// Read one Block record written by writeBlock. Returns false on I/O error or an unknown block type
// (which would desync the stream). Mirrors writeBlock exactly.
bool readBlock(FsFile& in, Block& b);

// Serialize a spine's anchor table (count + entries). Mirrors readAnchors.
bool writeAnchors(FsFile& out, const std::vector<Anchor>& anchors);
bool readAnchors(FsFile& in, std::vector<Anchor>& anchors);

// Serialize a spine's page-break-label table (count + entries). Mirrors readLabels.
bool writeLabels(FsFile& out, const std::vector<PageBreakLabel>& labels);
bool readLabels(FsFile& in, std::vector<PageBreakLabel>& labels);

// Serialize the deduped block-style pool (count + entries). Book-level; written once.
bool writeStylePool(FsFile& out, const std::vector<CssStyle>& pool);
bool readStylePool(FsFile& in, std::vector<CssStyle>& pool);

// Serialize the book-level chapter table (count + entries).
bool writeChapters(FsFile& out, const std::vector<Chapter>& chapters);
bool readChapters(FsFile& in, std::vector<Chapter>& chapters);

// Pack the 24 explicit-set CSS flags into one u32 (used for style equality/dedup, and by the
// style serializer). Exposed so styleEquals stays in lockstep with the serialized form.
uint32_t packDefined(const CssPropertyFlags& d);

}  // namespace compiled
