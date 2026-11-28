#pragma once
#include <cstddef>
#include <cstdint>

namespace reader_build {
// X4 needs its resident host frame for overlays and next-page pre-renders.
// Speculative indexing may use spare heap, but must not take that frame away.
inline bool allowBackgroundBorrow(bool x4) { return !x4; }
inline unsigned backgroundSliceBudget(bool x4) { return x4 ? 8 : 40; }

// While resident, the renderer seeds from the displayed buffer, never the
// write buffer (which may already contain the next page's pre-render).
template<class Renderer>
uint8_t* borrowBackgroundBuffer(Renderer& renderer, size_t* size) {
  if (!renderer.hasSecondaryBuffer()) return nullptr;
  if (!renderer.isX3()) renderer.syncRedRamFromFrameBuffer();
  return renderer.borrowSecondaryBuffer(size);
}
// Every successful reader borrow starts with an established panel baseline.
// Hiding the indexing popup must not downgrade page turns to HALF.
template<class Renderer>
void resumeBorrowedBuild(Renderer& renderer) {
  renderer.setSingleBufferFastDiff(true);
}
}  // namespace reader_build
