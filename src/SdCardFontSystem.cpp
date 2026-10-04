#include "SdCardFontSystem.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <climits>
#include <cstdlib>
#include <cstring>

#include "CrossPointSettings.h"
#include "SdCardFontGlobals.h"
#include "util/ReaderFontSizeOptions.h"

// Free-function resolver used by CrossPointSettings::getReaderFontId().
// Resolved by the linker — no callback indirection stored in settings.
int resolveSdCardFontId(const char* familyName, uint8_t fontSizeEnum, uint8_t pointSize) {
  return sdFontSystem.resolveFontId(familyName, fontSizeEnum, pointSize);
}

// --- Font-family dynamic SettingInfo trampolines ---
//
// The font-family SettingInfo lives in a namespace-static SettingsList that is
// initialized at global-static phase, well before sdFontSystem.begin() runs.
// We therefore cannot bake the SD family list into the SettingInfo at
// construction; instead the SettingInfo holds these stateless trampolines that
// consult sdFontSystem at every call. enumLabels is enriched lazily by the
// consumers (SettingsActivity, CrossPointWebServer) before each iteration.

uint8_t fontFamilyDynamicGetter(const void* /*ctx*/) {
  if (SETTINGS.sdFontFamilyName[0] != '\0') {
    const auto& families = sdFontSystem.registry().getFamilies();
    for (size_t i = 0; i < families.size(); i++) {
      if (families[i].name == SETTINGS.sdFontFamilyName) {
        return static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + i);
      }
    }
    // SD family no longer present (card removed?); fall through to built-in.
  }
  return SETTINGS.fontFamily < CrossPointSettings::BUILTIN_FONT_COUNT ? SETTINGS.fontFamily
                                                                      : CrossPointSettings::NOTOSANS;
}

void fontFamilyDynamicSetter(void* /*ctx*/, uint8_t value) {
  if (value < CrossPointSettings::BUILTIN_FONT_COUNT) {
    SETTINGS.fontFamily = value;
    SETTINGS.sdFontFamilyName[0] = '\0';
    if (SETTINGS.readerFontPointSize) {
      SETTINGS.fontSize = reader_font_size::nearestNamedSize(SETTINGS.readerFontPointSize);
    }
    SETTINGS.readerFontPointSize = 0;
    return;
  }
  const auto& families = sdFontSystem.registry().getFamilies();
  uint8_t sdIdx = value - CrossPointSettings::BUILTIN_FONT_COUNT;
  if (sdIdx < families.size()) {
    strncpy(SETTINGS.sdFontFamilyName, families[sdIdx].name.c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
    SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
    if (SETTINGS.numericFontSizes) {
      const uint8_t target = SETTINGS.readerFontPointSize ? SETTINGS.readerFontPointSize
                                                         : reader_font_size::namedPointSize(SETTINGS.fontSize);
      const auto* best = families[sdIdx].pickClosestSize(target);
      SETTINGS.readerFontPointSize = best ? best->pointSize : 0;
      if (best) SETTINGS.fontSize = reader_font_size::nearestNamedSize(best->pointSize);
    }
  }
}

uint8_t txtFontFamilyDynamicGetter(const void* /*ctx*/) {
  if (SETTINGS.txtSdFontFamilyName[0] != '\0') {
    const auto& families = sdFontSystem.registry().getFamilies();
    for (size_t i = 0; i < families.size(); i++) {
      if (families[i].name == SETTINGS.txtSdFontFamilyName) {
        return static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + i);
      }
    }
  }
  return SETTINGS.txtFontFamily < CrossPointSettings::BUILTIN_FONT_COUNT ? SETTINGS.txtFontFamily
                                                                         : CrossPointSettings::NOTOSANS;
}

void txtFontFamilyDynamicSetter(void* /*ctx*/, uint8_t value) {
  if (value < CrossPointSettings::BUILTIN_FONT_COUNT) {
    SETTINGS.txtFontFamily = value;
    SETTINGS.txtSdFontFamilyName[0] = '\0';
    return;
  }
  const auto& families = sdFontSystem.registry().getFamilies();
  uint8_t sdIdx = value - CrossPointSettings::BUILTIN_FONT_COUNT;
  if (sdIdx < families.size()) {
    strncpy(SETTINGS.txtSdFontFamilyName, families[sdIdx].name.c_str(), sizeof(SETTINGS.txtSdFontFamilyName) - 1);
    SETTINGS.txtSdFontFamilyName[sizeof(SETTINGS.txtSdFontFamilyName) - 1] = '\0';
  }
}

uint8_t fontFamilyOptionCount() {
  return static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + sdFontSystem.registry().getFamilies().size());
}

std::string fontFamilyOptionLabel(uint8_t i) {
  if (i < CrossPointSettings::BUILTIN_FONT_COUNT) {
    static const StrId BUILTIN_LABELS[] = {StrId::STR_NOTO_SANS};
    return I18N.get(BUILTIN_LABELS[i]);
  }
  const auto& families = sdFontSystem.registry().getFamilies();
  uint8_t sdIdx = i - CrossPointSettings::BUILTIN_FONT_COUNT;
  return sdIdx < families.size() ? families[sdIdx].name : std::string();
}

static uint8_t targetPtSizeFromSettings() {
  return SETTINGS.readerFontPointSize ? SETTINGS.readerFontPointSize
                                     : reader_font_size::namedPointSize(SETTINGS.fontSize);
}

void SdCardFontSystem::begin(GfxRenderer& renderer) {
  (void)renderer;
  registry_.discover();

  // Startup is discovery-only. SD font payloads are loaded lazily on reader
  // entry via ensureLoaded/ensureLoadedForPath and unloaded on reader exit.
  LOG_DBG("SDFS", "SD font system ready (discovery-only, %d families)", registry_.getFamilyCount());
}

void SdCardFontSystem::ensureLoaded(GfxRenderer& renderer) {
  const char* wantedFamily = SETTINGS.sdFontFamilyName;
  const std::string& currentFamily = manager_.currentFamilyName();
  const uint8_t targetPt = targetPtSizeFromSettings();

  if (wantedFamily[0] == '\0') {
    if (!currentFamily.empty()) {
      manager_.unloadAll(renderer);
    }
    return;
  }

  // Reload if family changed OR if the user-selected size changed and the
  // family has a closer file than what's currently loaded.
  bool familyMatches = (currentFamily == wantedFamily);
  if (familyMatches) {
    const auto* family = registry_.findFamily(wantedFamily);
    if (!family) {
      LOG_DBG("SDFS", "SD font family disappeared: %s (clearing)", wantedFamily);
      manager_.unloadAll(renderer);
      SETTINGS.sdFontFamilyName[0] = '\0';
      return;
    }
    const auto* best = family->pickClosestSize(targetPt);
    const uint8_t bestPt = best ? best->pointSize : 0;
    if (bestPt == manager_.currentPointSize() && !manager_.isPreviewLoad()) {
      return;  // already loaded with the right size through the normal cache path
    }
    LOG_DBG("SDFS", "Reloading %s: size %u -> %u (target %u)", wantedFamily, manager_.currentPointSize(), bestPt,
            targetPt);
  }

  if (!currentFamily.empty()) {
    manager_.unloadAll(renderer);
  }

  const auto* family = registry_.findFamily(wantedFamily);
  if (family) {
    if (manager_.loadFamily(*family, renderer, targetPt)) {
      LOG_DBG("SDFS", "Loaded SD font family: %s", wantedFamily);
    } else {
      LOG_ERR("SDFS", "Failed to load SD font family: %s (clearing)", wantedFamily);
      SETTINGS.sdFontFamilyName[0] = '\0';
    }
  } else {
    LOG_DBG("SDFS", "SD font family not found: %s (clearing)", wantedFamily);
    SETTINGS.sdFontFamilyName[0] = '\0';
  }
}

void SdCardFontSystem::ensureLoaded(GfxRenderer& renderer, const char* wantedFamily, uint8_t fontSizeEnum,
                                    const std::function<void()>& onColdLoad, uint8_t pointSize) {
  const std::string& currentFamily = manager_.currentFamilyName();
  const uint8_t targetPt = pointSize ? pointSize : reader_font_size::namedPointSize(fontSizeEnum);

  if (!wantedFamily || wantedFamily[0] == '\0') {
    if (!currentFamily.empty()) manager_.unloadAll(renderer);
    return;
  }

  bool familyMatches = (currentFamily == wantedFamily);
  if (familyMatches) {
    const auto* family = registry_.findFamily(wantedFamily);
    if (!family) {
      manager_.unloadAll(renderer);
      return;
    }
    const auto* best = family->pickClosestSize(targetPt);
    if (best && best->pointSize == manager_.currentPointSize() && !manager_.isPreviewLoad()) return;
  }

  if (!currentFamily.empty()) manager_.unloadAll(renderer);

  const auto* family = registry_.findFamily(wantedFamily);
  if (family) {
    if (!manager_.loadFamily(*family, renderer, targetPt, onColdLoad)) {
      LOG_ERR("SDFS", "Failed to load SD font family: %s", wantedFamily);
    }
  }
}

void SdCardFontSystem::ensureLoadedForPreview(GfxRenderer& renderer, const char* wantedFamily,
                                              uint8_t fontSizeEnum, uint8_t pointSize) {
  const uint8_t targetPt = pointSize ? pointSize : reader_font_size::namedPointSize(fontSizeEnum);
  if (!wantedFamily || wantedFamily[0] == '\0') {
    if (!manager_.currentFamilyName().empty()) manager_.unloadAll(renderer);
    return;
  }

  const auto* family = registry_.findFamily(wantedFamily);
  if (!family) return;
  const auto* best = family->pickClosestSize(targetPt);
  if (manager_.currentFamilyName() == wantedFamily && best && best->pointSize == manager_.currentPointSize()) return;

  if (!manager_.currentFamilyName().empty()) manager_.unloadAll(renderer);
  if (!manager_.loadFamilyPreview(*family, renderer, targetPt)) {
    LOG_ERR("SDFS", "Failed to load preview font family: %s", wantedFamily);
  }
}

int SdCardFontSystem::resolveFontId(const char* familyName, uint8_t fontSizeEnum, uint8_t pointSize) const {
  // The manager loads exactly one size for the active SD family. Resolve only
  // if the requested family matches the loaded family and the requested size
  // matches the loaded size. otherwise return 0 so callers can fall back.
  if (!familyName || familyName[0] == '\0') return 0;
  if (manager_.currentFamilyName() != familyName) return 0;
  const auto* family = registry_.findFamily(familyName);
  const auto* best = family ? family->pickClosestSize(pointSize ? pointSize : reader_font_size::namedPointSize(fontSizeEnum)) : nullptr;
  if (!best || manager_.currentPointSize() != best->pointSize) return 0;
  return manager_.getFontId(familyName);
}
