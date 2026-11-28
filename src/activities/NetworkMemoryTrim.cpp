#include "NetworkMemoryTrim.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <Logging.h>

#include "SilentRestart.h"
#include "RenderLock.h"
#include <optional>

void trimMemoryForNetworkSession(const GfxRenderer& renderer, const char* logTag) {
  // Some activities already hold the non-recursive render mutex; others (OPDS,
  // OTA, weather) enter from loop(). Protect the entire cache/buffer transaction.
  std::optional<RenderLock> lock;
  if (!RenderLock::isHeldByCurrentTask()) lock.emplace(RenderLock::ExclusiveActivityAccess{});

  if (auto* cache = renderer.getFontCacheManager()) {
    cache->clearCache();
  }
  if (renderer.hasSecondaryBuffer()) {
    // Seed the controller baseline while frameBufferActive is still valid.
    if (!renderer.isX3()) renderer.syncRedRamFromFrameBuffer();
    if (renderer.releaseSecondaryBuffer()) {
      LOG_DBG(logTag, "Released secondary framebuffer before network session (~52 KB contiguous)");
      renderer.setSingleBufferFastDiff(true);
    }
  }
}

void restoreDisplayAfterNetworkSession(const GfxRenderer& renderer) {
  // Some activities already hold the non-recursive render mutex; others (OPDS,
  // OTA, weather) enter from loop(). Protect the entire cache/buffer transaction.
  std::optional<RenderLock> lock;
  if (!RenderLock::isHeldByCurrentTask()) lock.emplace(RenderLock::ExclusiveActivityAccess{});

  if (renderer.hasSecondaryBuffer()) return;
  if (auto* cache = renderer.getFontCacheManager()) cache->clearCache();
  if (renderer.reallocSecondaryBuffer()) {
    // The display driver uses its retained baseline for the first refresh.
    renderer.setSingleBufferFastDiff(false);
  } else {
    silentRestart();
  }
}
