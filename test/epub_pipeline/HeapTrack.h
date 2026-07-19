#pragma once
// Whole-process heap tracking for the epub_pipeline_dump benchmark mode.
// Linked ONLY into the CLI tool — it overrides malloc/free process-wide
// (same header-word technique as test/epub_benchmark), which is unwanted
// inside the gtest binary.
#include <cstddef>

// Zero the live/peak counters and start tracking.
void heapTrackBegin();
// Stop tracking and return the peak live bytes observed since begin().
size_t heapTrackEnd();
