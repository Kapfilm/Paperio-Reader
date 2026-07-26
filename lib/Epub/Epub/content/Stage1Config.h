#pragma once
// Stage-1 compiled-content build gate (docs/compiled-book-pipeline-plan.md Phase 3).
//
// The walk → LayoutSink path ships unconditionally (the device build drives an internal
// LayoutSink). EPUB_STAGE1 gates the content.bin PERSISTENCE side: the ContentSink, the host
// content_stage1_dump tool, and the device Section content.bin write/read wiring in
// EpubReaderActivity (buildSectionFromContentBin read-back + one-time compileBookToContentBin; see
// docs/parser-stage1-content-bin-device-wiring-design-2026-07-26.md). Host test targets set it
// (-DEPUB_STAGE1=1); the device default env leaves it off, so the parse path is bit-for-bit
// unchanged. Overridable #ifndef guard so a target (or -D on the build) can flip it.
#ifndef EPUB_STAGE1
#define EPUB_STAGE1 0
#endif
