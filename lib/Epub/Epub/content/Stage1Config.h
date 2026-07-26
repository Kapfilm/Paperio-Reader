#pragma once
// Stage-1 compiled-content build gate (docs/compiled-book-pipeline-plan.md Phase 3).
//
// The walk → LayoutSink path ships unconditionally (the device build drives an internal
// LayoutSink). EPUB_STAGE1 gates the content.bin PERSISTENCE side: the ContentSink and the host
// content_stage1_dump tool. Today only the host test targets set it (-DEPUB_STAGE1=1); wiring the
// content.bin write/read into the device Section build is a pending step (see
// docs/parser-stage1-content-bin-device-wiring-design-2026-07-26.md), which is expected to reuse
// this flag. Overridable #ifndef guard so a target (or -D on the build) can flip it.
#ifndef EPUB_STAGE1
#define EPUB_STAGE1 0
#endif
