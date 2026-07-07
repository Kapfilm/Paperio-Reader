// Render-benchmark harness for EpubReaderActivity: drives a scripted forward/backward page-turn
// sweep and produces a diagnostic report shown via EpubRenderBenchmarkActivity. Split out of
// EpubReaderActivity.cpp because it has no coupling to the render-pass state machine that
// dominates that file — it only calls the activity's existing public-to-the-class navigation
// and stats surface (stepPageState, requestUpdateAndWait, lastRenderStats).
#include "EpubReaderActivity.h"

#if ENABLE_BENCHMARKS

#include <GfxRenderer.h>

#include "EpubRenderBenchmarkActivity.h"

namespace {
const char* orientationToString(const GfxRenderer::Orientation orientation) {
  switch (orientation) {
    case GfxRenderer::Portrait:
      return "Portrait";
    case GfxRenderer::LandscapeClockwise:
      return "Landscape CW";
    case GfxRenderer::PortraitInverted:
      return "Portrait Inverted";
    case GfxRenderer::LandscapeCounterClockwise:
      return "Landscape CCW";
  }
  return "Unknown";
}
}  // namespace

void EpubReaderActivity::runRenderBenchmark() {
  if (!epub) {
    return;
  }

  if (!section) {
    requestUpdateAndWait();
    if (!section) {
      return;
    }
  }

  const LastRenderStats startSnapshot = lastRenderStats;
  BenchmarkAggregate aggregate;
  auto recordRender = [&aggregate](const LastRenderStats& snapshot) {
    if (!snapshot.valid) {
      return;
    }

    aggregate.renderCount++;
    aggregate.imagePageCount += snapshot.hadImages ? 1 : 0;
    aggregate.cacheRebuildCount += snapshot.cacheRebuilt ? 1 : 0;
    if (snapshot.footnoteCount > aggregate.maxFootnotes) {
      aggregate.maxFootnotes = snapshot.footnoteCount;
    }

    aggregate.totalRequestRenderMs += snapshot.requestRenderMs;
    if (aggregate.renderCount == 1 || snapshot.requestRenderMs < aggregate.minRequestRenderMs) {
      aggregate.minRequestRenderMs = snapshot.requestRenderMs;
    }
    if (snapshot.requestRenderMs > aggregate.maxRequestRenderMs) {
      aggregate.maxRequestRenderMs = snapshot.requestRenderMs;
    }

    aggregate.totalRenderMs += snapshot.phases.totalMs;
    if (aggregate.renderCount == 1 || snapshot.phases.totalMs < aggregate.minRenderMs) {
      aggregate.minRenderMs = snapshot.phases.totalMs;
    }
    if (snapshot.phases.totalMs > aggregate.maxRenderMs) {
      aggregate.maxRenderMs = snapshot.phases.totalMs;
    }

    aggregate.totalSectionLoadMs += snapshot.sectionLoadMs;
    aggregate.totalPageLoadMs += snapshot.pageLoadMs;
    aggregate.totalPhases.prewarmMs += snapshot.phases.prewarmMs;
    aggregate.totalPhases.bwRenderMs += snapshot.phases.bwRenderMs;
    aggregate.totalPhases.displayMs += snapshot.phases.displayMs;
    aggregate.totalPhases.bwStoreMs += snapshot.phases.bwStoreMs;
    aggregate.totalPhases.grayLsbMs += snapshot.phases.grayLsbMs;
    aggregate.totalPhases.grayMsbMs += snapshot.phases.grayMsbMs;
    aggregate.totalPhases.grayDisplayMs += snapshot.phases.grayDisplayMs;
    aggregate.totalPhases.bwRestoreMs += snapshot.phases.bwRestoreMs;
    aggregate.totalPhases.totalMs += snapshot.phases.totalMs;

    aggregate.totalFontCacheHits += snapshot.fontCacheHits;
    aggregate.totalFontCacheMisses += snapshot.fontCacheMisses;
    aggregate.totalFontDecompressMs += snapshot.fontDecompressMs;
    aggregate.totalFontGetBitmapTimeUs += snapshot.fontGetBitmapTimeUs;
    aggregate.totalFontGetBitmapCalls += snapshot.fontGetBitmapCalls;

    if (aggregate.renderCount == 1 || snapshot.freeHeapAfter < aggregate.minFreeHeapAfter) {
      aggregate.minFreeHeapAfter = snapshot.freeHeapAfter;
    }
    if (snapshot.freeHeapAfter > aggregate.maxFreeHeapAfter) {
      aggregate.maxFreeHeapAfter = snapshot.freeHeapAfter;
    }
  };
  const unsigned long startTime = millis();
  int forwardTurns = 0;
  int backwardTurns = 0;

  for (int i = 0; i < 10; i++) {
    if (!stepPageState(true)) {
      break;
    }
    requestUpdateAndWait();
    recordRender(lastRenderStats);
    forwardTurns++;
  }

  const unsigned long forwardMs = millis() - startTime;
  const unsigned long backwardStart = millis();

  for (int i = 0; i < 10; i++) {
    if (!stepPageState(false)) {
      break;
    }
    requestUpdateAndWait();
    recordRender(lastRenderStats);
    backwardTurns++;
  }

  const unsigned long backwardMs = millis() - backwardStart;

  startActivityForResult(
      std::make_unique<EpubRenderBenchmarkActivity>(
          renderer, mappedInput,
          buildRenderBenchmarkReport(startSnapshot, aggregate, forwardTurns, forwardMs, backwardTurns, backwardMs)),
      [this](const ActivityResult&) { requestUpdate(); });
}

std::string EpubReaderActivity::buildRenderBenchmarkReport(const LastRenderStats& startSnapshot,
                                                           const BenchmarkAggregate& aggregate, const int forwardTurns,
                                                           const unsigned long forwardMs, const int backwardTurns,
                                                           const unsigned long backwardMs) const {
  const LastRenderStats& endSnapshot = lastRenderStats.valid ? lastRenderStats : startSnapshot;

  std::string report;
  report.reserve(768);

  auto appendLine = [&report](const std::string& line) {
    if (!report.empty()) {
      report += '\n';
    }
    report += line;
  };

  appendLine("Forward 10: " + std::to_string(forwardTurns) + " turns in " + std::to_string(forwardMs) + " ms");
  if (forwardTurns > 0) {
    appendLine("Forward avg: " + std::to_string(forwardMs / static_cast<unsigned long>(forwardTurns)) + " ms/turn");
  }
  appendLine("Backward 10: " + std::to_string(backwardTurns) + " turns in " + std::to_string(backwardMs) + " ms");
  if (backwardTurns > 0) {
    appendLine("Backward avg: " + std::to_string(backwardMs / static_cast<unsigned long>(backwardTurns)) + " ms/turn");
  }
  appendLine("Measured renders: " + std::to_string(aggregate.renderCount) + ", image pages " +
             std::to_string(aggregate.imagePageCount) + ", cache rebuilds " +
             std::to_string(aggregate.cacheRebuildCount));

  appendLine("Start: spine " + std::to_string(startSnapshot.spineIndex) + ", page " +
             std::to_string(startSnapshot.pageIndex + 1) + "/" + std::to_string(startSnapshot.pageCount));
  appendLine("End: spine " + std::to_string(endSnapshot.spineIndex) + ", page " +
             std::to_string(endSnapshot.pageIndex + 1) + "/" + std::to_string(endSnapshot.pageCount));
  appendLine("Orientation: " +
             std::string(orientationToString(static_cast<GfxRenderer::Orientation>(endSnapshot.orientation))));
  appendLine("Viewport: " + std::to_string(endSnapshot.viewportWidth) + "x" +
             std::to_string(endSnapshot.viewportHeight) + " px, margins T/R/B/L " +
             std::to_string(endSnapshot.marginTop) + "/" + std::to_string(endSnapshot.marginRight) + "/" +
             std::to_string(endSnapshot.marginBottom) + "/" + std::to_string(endSnapshot.marginLeft));
  appendLine("Font: " + std::to_string(endSnapshot.effectiveFontId) + ", embedded CSS " +
             std::string(endSnapshot.embeddedStyle ? "on" : "off") + ", images " +
             std::to_string(endSnapshot.imageRendering) + ", AA " +
             std::string(endSnapshot.textAntiAliasing ? "on" : "off"));
  appendLine("Last page: images " + std::string(endSnapshot.hadImages ? "yes" : "no") + ", footnotes " +
             std::to_string(endSnapshot.footnoteCount) + ", cache rebuilt " +
             std::string(endSnapshot.cacheRebuilt ? "yes" : "no"));
  if (aggregate.renderCount > 0) {
    appendLine("Render avg/min/max: request " +
               std::to_string(aggregate.totalRequestRenderMs / static_cast<unsigned long>(aggregate.renderCount)) +
               "/" + std::to_string(aggregate.minRequestRenderMs) + "/" + std::to_string(aggregate.maxRequestRenderMs) +
               " ms, core " +
               std::to_string(aggregate.totalRenderMs / static_cast<unsigned long>(aggregate.renderCount)) + "/" +
               std::to_string(aggregate.minRenderMs) + "/" + std::to_string(aggregate.maxRenderMs) + " ms");
    appendLine("Aggregate loads: section " + std::to_string(aggregate.totalSectionLoadMs) + " ms, page " +
               std::to_string(aggregate.totalPageLoadMs) + " ms, max footnotes " +
               std::to_string(aggregate.maxFootnotes));
    appendLine("Aggregate phases: prewarm " + std::to_string(aggregate.totalPhases.prewarmMs) + ", bw " +
               std::to_string(aggregate.totalPhases.bwRenderMs) + ", display " +
               std::to_string(aggregate.totalPhases.displayMs) + ", planes " +
               std::to_string(aggregate.totalPhases.grayLsbMs) + ", gray display " +
               std::to_string(aggregate.totalPhases.grayDisplayMs) + ", restore " +
               std::to_string(aggregate.totalPhases.bwRestoreMs));
    appendLine("Aggregate font: hits " + std::to_string(aggregate.totalFontCacheHits) + ", misses " +
               std::to_string(aggregate.totalFontCacheMisses) + ", decompress " +
               std::to_string(aggregate.totalFontDecompressMs) + " ms");
    appendLine("Aggregate glyph lookups: " + std::to_string(aggregate.totalFontGetBitmapCalls) + " calls, " +
               std::to_string(aggregate.totalFontGetBitmapTimeUs) + " us total");
    appendLine("Heap after render min/max: " + std::to_string(aggregate.minFreeHeapAfter) + "/" +
               std::to_string(aggregate.maxFreeHeapAfter));
  }
  appendLine("Last render: request " + std::to_string(endSnapshot.requestRenderMs) + " ms, section load " +
             std::to_string(endSnapshot.sectionLoadMs) + " ms, page load " + std::to_string(endSnapshot.pageLoadMs) +
             " ms, render total " + std::to_string(endSnapshot.phases.totalMs) + " ms");
  appendLine("Phases: prewarm " + std::to_string(endSnapshot.phases.prewarmMs) + ", bw " +
             std::to_string(endSnapshot.phases.bwRenderMs) + ", display " +
             std::to_string(endSnapshot.phases.displayMs) + ", planes " + std::to_string(endSnapshot.phases.grayLsbMs) +
             ", gray display " + std::to_string(endSnapshot.phases.grayDisplayMs) + ", restore " +
             std::to_string(endSnapshot.phases.bwRestoreMs));
  appendLine("Font cache: hits " + std::to_string(endSnapshot.fontCacheHits) + ", misses " +
             std::to_string(endSnapshot.fontCacheMisses) + ", decompress " +
             std::to_string(endSnapshot.fontDecompressMs) + " ms, groups " +
             std::to_string(endSnapshot.fontUniqueGroups));
  appendLine("Font buffers: page " + std::to_string(endSnapshot.fontPageBufferBytes) + ", glyph table " +
             std::to_string(endSnapshot.fontPageGlyphsBytes) + ", peak temp " +
             std::to_string(endSnapshot.fontPeakTempBytes));
  appendLine("Glyph lookups: " + std::to_string(endSnapshot.fontGetBitmapCalls) + " calls, " +
             std::to_string(endSnapshot.fontGetBitmapTimeUs) + " us total");
  appendLine("Heap: before " + std::to_string(endSnapshot.freeHeapBefore) + "/" +
             std::to_string(endSnapshot.largestFreeBlockBefore) + ", after " +
             std::to_string(endSnapshot.freeHeapAfter) + "/" + std::to_string(endSnapshot.largestFreeBlockAfter));

  return report;
}

#endif  // ENABLE_BENCHMARKS
