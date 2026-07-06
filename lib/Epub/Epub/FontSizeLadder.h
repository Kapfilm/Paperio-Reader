#pragma once

#include <cstdint>

// FontSizeLadder — the body font's sibling sizes, used to snap an effective block
// font size to a REAL pre-rendered font instead of nearest-neighbor glyph scaling
// (concept from CidVonHighwind/microreader's BitmapFontSet).
//
// The app layer builds the ladder (it knows the font family and which built-in
// sizes exist) and passes it down as pure data so the Epub lib stays
// settings-agnostic — the same contract the old HeadingFonts struct had.
// Each rung is a registered fontId plus its size as a percent of the body font
// (the body font itself is the 100% rung). SD-card fonts ship a single loaded
// size, so their ladder is empty and everything resolves to the scale fallback.
//
// Deterministic from the body fontId by construction, so it is deliberately NOT
// part of the section-cache property hash (fontId already is).
struct FontSizeLadder {
  static constexpr int kMaxRungs = 5;  // built-in families ship 10/12/14/16/18 pt

  struct Rung {
    int32_t fontId = 0;    // 32-bit font-id hash (fontIds.h); never truncate
    uint16_t sizePct = 0;  // rung size as percent of the body font (body = 100)
  };

  Rung rungs[kMaxRungs] = {};
  int8_t count = 0;

  void addRung(const int32_t fontId, const uint16_t sizePct) {
    if (count >= kMaxRungs || fontId == 0 || sizePct == 0) return;
    rungs[count].fontId = fontId;
    rungs[count].sizePct = sizePct;
    ++count;
  }

  // Residual dead zone: a leftover scale this close to 1.0 is snapped to exactly 1.0,
  // so the block renders NATIVE glyphs of the chosen rung instead of resampling every
  // glyph for a size difference below the ~5% typographic just-noticeable threshold.
  // Kept at 3% so deliberate fine gradients (Alice's 99/98/97… mouse tale) only lose
  // their first, imperceptible steps. Applied to the final residual regardless of rung
  // (including the body font and the empty-ladder SD path).
  static constexpr float kResidualDeadZone = 0.03f;

  // Result of snapping a desired size to the ladder. fontId == 0 means "use the
  // body font"; residual is the remaining scale applied on top of the chosen font
  // so the exact desired size is reached (1.0 when the rung matches exactly).
  struct Resolved {
    int32_t fontId = 0;
    float residual = 1.0f;
  };

  // Snap desiredPct (percent of the body size) to the nearest rung. An empty
  // ladder, or a nearest rung of 100%, keeps the body font with the full scale
  // as residual — exactly the legacy scale-only behavior.
  Resolved resolve(const float desiredPct) const {
    Resolved r = resolveExact(desiredPct);
    if (r.residual > 1.0f - kResidualDeadZone && r.residual < 1.0f + kResidualDeadZone) {
      r.residual = 1.0f;
    }
    return r;
  }

 private:
  Resolved resolveExact(const float desiredPct) const {
    Resolved r;
    r.residual = desiredPct / 100.0f;
    if (count == 0 || desiredPct <= 0.0f) return r;

    int best = -1;
    float bestDiff = 0.0f;
    for (int i = 0; i < count; ++i) {
      const float diff =
          (rungs[i].sizePct > desiredPct) ? (rungs[i].sizePct - desiredPct) : (desiredPct - rungs[i].sizePct);
      if (best < 0 || diff < bestDiff) {
        best = i;
        bestDiff = diff;
      }
    }
    if (best < 0 || rungs[best].sizePct == 100) return r;  // body font is the closest — pure scale

    r.fontId = rungs[best].fontId;
    r.residual = desiredPct / static_cast<float>(rungs[best].sizePct);
    return r;
  }
};
