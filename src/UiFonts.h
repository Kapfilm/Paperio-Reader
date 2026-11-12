#pragma once

#include <cstdint>

class GfxRenderer;

// Persisted values. Keep 1 mapped to the existing enlarged setting.
namespace UiTextSize {
inline constexpr uint8_t Normal = 0;
inline constexpr uint8_t Enlarged = 1;
inline constexpr uint8_t Medium = 2;
}  // namespace UiTextSize

// Small instructional text stays at the built-in 8pt size, independently of UI size/style.
inline constexpr int UI_HINT_FONT_ID = -1860378436;
inline constexpr int COMPACT_BUTTON_FONT_ID = -1860378435;

void registerUiFonts(GfxRenderer& renderer);
// Call under RenderLock before rendering; returns true when font mappings changed.
bool applyUiTextSize(GfxRenderer& renderer, uint8_t size, bool bold = false);
