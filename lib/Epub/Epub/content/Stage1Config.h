#pragma once
// Stage-1 compiled-content build gate (Phase 3 step 2c of
// docs/compiled-book-pipeline-plan.md; design in docs/stage1-extraction-design.md).
//
// EPUB_STAGE1 == 0 (default): the shipping fused parse→layout path. The BlockSink
//   extraction (LayoutSink) still ships — it is a pure refactor, unflagged.
// EPUB_STAGE1 == 1: also compile the ContentSink / ContentCompiler that drive the
//   shared walk core to produce content.bin, and the host content_stage1_dump tool.
//
// Mirrors the pattern the retired EPUB_BUILD_ARENA flag used: an overridable
// #ifndef guard so a test target (or -D on the build) can flip it without editing
// source. Only the NEW Stage-1 producer sits behind it; goldens for the fused path
// stay byte-identical whether it is on or off.
#ifndef EPUB_STAGE1
#define EPUB_STAGE1 0
#endif
