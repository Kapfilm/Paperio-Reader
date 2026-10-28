#pragma once

class GfxRenderer;

void registerUiFonts(GfxRenderer& renderer);
// Call under RenderLock before rendering; returns true when font mappings changed.
bool applyUiTextSize(GfxRenderer& renderer, bool enlarged);
