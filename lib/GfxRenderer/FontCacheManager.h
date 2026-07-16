#pragma once

#include <EpdFontFamily.h>

#include <cstdint>
#include <map>
#include <string>

class FontDecompressor;
class SdCardFont;

class FontCacheManager {
 public:
  FontCacheManager(const std::map<int, EpdFontFamily>& fontMap, const std::map<int, SdCardFont*>& sdCardFonts);

  void setFontDecompressor(FontDecompressor* d);

  void clearCache();
  // Prewarm one font's glyphs for the given text. Appends into existing page slots
  // WITHOUT clearing them (multi-font pages call this once per font); clear once via
  // clearCache() before a batch — endScanAndPrewarm() does exactly that.
  void prewarmCache(int fontId, const char* utf8Text, uint8_t styleMask = 0x0F);
  void logStats(const char* label = "render");
  void resetStats();

  // Scan-mode API: called by GfxRenderer::drawText() during scan pass
  bool isScanning() const;
  void recordText(const char* text, int fontId, EpdFontFamily::Style style);

  // The FontDecompressor pointer, needed by GfxRenderer::getGlyphBitmap()
  FontDecompressor* getDecompressor() const { return fontDecompressor_; }

  // RAII scope for two-pass prewarm pattern
  class PrewarmScope {
   public:
    explicit PrewarmScope(FontCacheManager& manager);
    ~PrewarmScope();
    void endScanAndPrewarm();
    PrewarmScope(PrewarmScope&& other) noexcept;
    PrewarmScope& operator=(PrewarmScope&&) = delete;
    PrewarmScope(const PrewarmScope&) = delete;
    PrewarmScope& operator=(const PrewarmScope&) = delete;

   private:
    FontCacheManager* manager_;
    bool active_ = true;
  };
  PrewarmScope createPrewarmScope();

 private:
  const std::map<int, EpdFontFamily>& fontMap_;
  const std::map<int, SdCardFont*>& sdCardFonts_;
  FontDecompressor* fontDecompressor_ = nullptr;

  enum class ScanMode : uint8_t { None, Scanning };
  ScanMode scanMode_ = ScanMode::None;
  // Prewarm scan accumulates text PER fontId, not just the first one seen. A page that mixes
  // fonts — e.g. a chapter opener with an h1/h2 heading in a taller font plus body text — must
  // prewarm every font it uses, or the render thrashes the glyph cache (seconds of per-glyph
  // flash decode) on whichever font wasn't warmed. Typically 1-2 entries.
  //
  // Within a font, text is kept PER BASE STYLE (R/B/I/BI) so each style slot is warmed with
  // only the glyphs actually drawn in that style. Sharing one text buffer across styles warmed
  // the WHOLE page into every style that appeared — one italic word cost a full ~4-5 KB italic
  // slot, and a bionic page held 4 full slots (~20-26 KB of page buffers instead of ~5-8 KB).
  struct ScanEntry {
    std::string textByStyle[4];
  };
  std::map<int, ScanEntry> scanByFont_;
};
