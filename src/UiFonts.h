#pragma once

class GfxRenderer;

// Small instructional text stays at the built-in 8pt size, independently of UI size/style.
inline constexpr int UI_HINT_FONT_ID = -1860378436;

void registerUiFonts(GfxRenderer& renderer);
// Call under RenderLock before rendering; returns true when font mappings changed.
bool applyUiTextSize(GfxRenderer& renderer, bool enlarged, bool bold = false);
