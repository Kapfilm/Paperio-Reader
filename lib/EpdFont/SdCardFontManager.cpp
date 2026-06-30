#include "SdCardFontManager.h"

#include <EpdFontFamily.h>
#include <FlashFontPartition.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <SdCardFont.h>
#include <SdCardFontRegistry.h>

#include <algorithm>
#include <cstdlib>

SdCardFontManager::~SdCardFontManager() {
  if (renderer_) {
    unloadAll(*renderer_);
  } else {
    for (auto& lf : loaded_) {
      delete lf.font;
    }
    loaded_.clear();
  }
}

// FNV-1a continuation: seeds with contentHash, then hashes family name + point size.
int SdCardFontManager::computeFontId(uint32_t contentHash, const char* familyName, uint8_t pointSize) {
  static constexpr uint32_t FNV_PRIME = 16777619u;
  uint32_t hash = contentHash;
  while (*familyName) {
    hash ^= static_cast<uint8_t>(*familyName++);
    hash *= FNV_PRIME;
  }
  hash ^= pointSize;
  hash *= FNV_PRIME;
  int id = static_cast<int>(hash);
  return id != 0 ? id : 1;
}

// Try to write the whole family (all sizes) to the flash partition.
// If the total size exceeds the available space, falls back to writing only
// the single requested file. Returns true if at least the requested size
// was written successfully.
static bool writeFamily(const SdCardFontFamilyInfo& family, uint8_t requestedPointSize) {
  // Collect available sizes and sort ascending so we write small→large.
  std::vector<uint8_t> sizes = family.availableSizes();
  std::sort(sizes.begin(), sizes.end());

  // Check whether the whole family fits.
  // FlashFontPartition::beginWrite erases the full partition, so we compute
  // the total first without writing.
  {
    // Rough total: sum all file sizes + HEADER_BYTES overhead.
    size_t total = FlashFontPartition::HEADER_BYTES;
    bool allFit = true;
    // We can't read partition size here directly, but 3.47 MB is the known
    // value. Use a conservative 3.3 MB cap to leave room for alignment padding.
    static constexpr size_t PARTITION_CAP = 3300 * 1024;
    for (uint8_t sz : sizes) {
      const SdCardFontFileInfo* fi = family.findFile(sz);
      if (!fi) continue;
      HalFile f;
      if (!Storage.openFileForRead("FFP", fi->path.c_str(), f) || !f) continue;
      total += (f.fileSize() + 3) & ~static_cast<size_t>(3);  // 4-byte align
      f.close();
      if (total > PARTITION_CAP || static_cast<uint8_t>(sizes.size()) > FlashFontPartition::MAX_ENTRIES) {
        allFit = false;
        break;
      }
    }

    if (allFit && sizes.size() > 1) {
      // Write the whole family.
      if (!FlashFontPartition::beginWrite(family.name.c_str())) return false;
      bool requestedWritten = false;
      for (uint8_t sz : sizes) {
        const SdCardFontFileInfo* fi = family.findFile(sz);
        if (!fi) continue;
        if (FlashFontPartition::appendFile(fi->path.c_str(), family.name.c_str(), sz)) {
          if (sz == requestedPointSize) requestedWritten = true;
        } else {
          LOG_ERR("FFP", "appendFile failed for %s@%u; aborting family write", family.name.c_str(), sz);
          // Fall through to single-file fallback below.
          FlashFontPartition::finaliseWrite();  // attempt to commit what we have
          return requestedWritten;
        }
      }
      if (!FlashFontPartition::finaliseWrite()) return false;
      LOG_INF("SDMGR", "Cached full family %s (%zu sizes) to flash", family.name.c_str(), sizes.size());
      return requestedWritten;
    }
  }

  // Single-file fallback: only write the requested size.
  const SdCardFontFileInfo* fi = family.findFile(requestedPointSize);
  if (!fi) return false;
  if (!FlashFontPartition::beginWrite(family.name.c_str())) return false;
  if (!FlashFontPartition::appendFile(fi->path.c_str(), family.name.c_str(), requestedPointSize)) {
    FlashFontPartition::finaliseWrite();
    return false;
  }
  if (!FlashFontPartition::finaliseWrite()) return false;
  LOG_INF("SDMGR", "Cached single file %s@%u to flash", family.name.c_str(), requestedPointSize);
  return true;
}

bool SdCardFontManager::loadFamily(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, uint8_t targetPtSize,
                                   const std::function<void()>& onColdLoad) {
  if (!renderer_) renderer_ = &renderer;
  if (!loadedFamilyName_.empty()) unloadAll(renderer);

  const SdCardFontFileInfo* selected = family.pickClosestSize(targetPtSize);
  if (!selected) {
    LOG_ERR("SDMGR", "Family %s has no files to load", family.name.c_str());
    return false;
  }

  // Ensure all sizes are cached in the flash partition.
  if (FlashFontPartition::isMapped()) FlashFontPartition::unmap();
  const bool alreadyCached = FlashFontPartition::hasEntry(family.name.c_str(), selected->pointSize);
  if (!alreadyCached) {
    if (onColdLoad) onColdLoad();
    if (!writeFamily(family, selected->pointSize)) {
      LOG_ERR("SDMGR", "Flash write failed for %s", family.name.c_str());
      // Fall through to SD-only load of the primary size below.
    }
  } else {
    LOG_DBG("SDMGR", "Flash cache hit for %s@%u", family.name.c_str(), selected->pointSize);
  }

  // Try to map all available sizes at once from the flash partition.
  // Each entry gets its own SdCardFont aliasing the single mmap region.
  bool primaryLoaded = false;
  {
    static constexpr int kMaxSizes = 8;
    FlashFontPartition::MappedEntry mapped[kMaxSizes];
    const int nMapped = FlashFontPartition::mmapAll(family.name.c_str(), mapped, kMaxSizes);

    if (nMapped > 0) {
      for (int i = 0; i < nMapped; i++) {
        // Find the SD path for this size (needed by loadFromMmap for logging).
        const SdCardFontFileInfo* fi = family.findFile(mapped[i].pointSize);
        const char* sdPath = fi ? fi->path.c_str() : selected->path.c_str();

        auto* font = new (std::nothrow) SdCardFont();
        if (!font) {
          LOG_ERR("SDMGR", "OOM for %s@%u", family.name.c_str(), mapped[i].pointSize);
          continue;
        }
        if (!font->loadFromMmap(mapped[i].ptr, mapped[i].size, sdPath)) {
          LOG_ERR("SDMGR", "loadFromMmap failed for %s@%u", family.name.c_str(), mapped[i].pointSize);
          delete font;
          continue;
        }

        const int fontId = computeFontId(font->contentHash(), family.name.c_str(), mapped[i].pointSize);
        if (renderer.getFontMap().count(fontId) != 0) {
          LOG_ERR("SDMGR", "Font ID collision for %s@%u, skipping", family.name.c_str(), mapped[i].pointSize);
          delete font;
          continue;
        }

        renderer.registerSdCardFont(fontId, font);
        loaded_.push_back({font, fontId, mapped[i].pointSize});
        EpdFontFamily fontFamily(font->getEpdFont(0), font->getEpdFont(1), font->getEpdFont(2), font->getEpdFont(3));
        renderer.insertFont(fontId, fontFamily);
        LOG_INF("SDMGR", "Loaded %s@%u from flash (id=%d)", family.name.c_str(), mapped[i].pointSize, fontId);

        if (mapped[i].pointSize == selected->pointSize) primaryLoaded = true;
      }
    }
  }

  // Fall back to SD load of just the primary size if flash path failed.
  if (!primaryLoaded) {
    if (FlashFontPartition::isMapped()) FlashFontPartition::unmap();
    auto* font = new (std::nothrow) SdCardFont();
    if (!font) {
      LOG_ERR("SDMGR", "OOM allocating SdCardFont for %s", selected->path.c_str());
      return false;
    }
    if (!font->load(selected->path.c_str())) {
      LOG_ERR("SDMGR", "Failed to load %s from SD", selected->path.c_str());
      delete font;
      return false;
    }
    const int fontId = computeFontId(font->contentHash(), family.name.c_str(), selected->pointSize);
    renderer.registerSdCardFont(fontId, font);
    loaded_.push_back({font, fontId, selected->pointSize});
    EpdFontFamily fontFamily(font->getEpdFont(0), font->getEpdFont(1), font->getEpdFont(2), font->getEpdFont(3));
    renderer.insertFont(fontId, fontFamily);
    LOG_INF("SDMGR", "Loaded %s@%u from SD (flash fallback)", family.name.c_str(), selected->pointSize);
  }

  loadedFamilyName_ = family.name;
  loadedPointSize_ = selected->pointSize;
  return true;
}

int SdCardFontManager::getFontIdForPt(const std::string& familyName, uint8_t pt) const {
  if (familyName != loadedFamilyName_) return 0;
  for (const auto& lf : loaded_) {
    if (lf.size == pt) return lf.fontId;
  }
  return 0;
}

std::vector<uint8_t> SdCardFontManager::loadedSizes() const {
  std::vector<uint8_t> sizes;
  sizes.reserve(loaded_.size());
  for (const auto& lf : loaded_) sizes.push_back(lf.size);
  std::sort(sizes.begin(), sizes.end());
  return sizes;
}

void SdCardFontManager::unloadAll(GfxRenderer& renderer) {
  if (!renderer_) renderer_ = &renderer;
  renderer.clearSdCardFonts();
  for (auto& lf : loaded_) {
    renderer.removeFont(lf.fontId);
    delete lf.font;
  }
  loaded_.clear();
  loadedFamilyName_.clear();
  loadedPointSize_ = 0;
  if (FlashFontPartition::isMapped()) FlashFontPartition::unmap();
}

int SdCardFontManager::getFontId(const std::string& familyName) const {
  if (familyName != loadedFamilyName_ || loaded_.empty()) return 0;
  // Return the font ID for the primary (body) size — the closest to the user's target.
  return getFontIdForPt(familyName, loadedPointSize_);
}
