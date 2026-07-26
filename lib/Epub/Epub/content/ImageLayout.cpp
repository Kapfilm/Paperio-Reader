#include "ImageLayout.h"

namespace compiled {

// Resolve an image's on-page display size from its intrinsic dims + CSS width/height, clamped to
// the container/viewport while preserving aspect ratio. See ImageLayout.h for the branch rules.
ImageDisplaySize computeImageDisplaySize(const int intrinsicW, const int intrinsicH, const CssStyle& imgStyle,
                                         const int viewportWidth, const int viewportHeight, const int containerWidth,
                                         const float emSize) {
  // viewportWidth is part of the conceptual contract but the horizontal clamp uses
  // containerWidth (viewportWidth minus the block inset), so it is not read directly.
  (void)viewportWidth;
  int displayWidth = 0;
  int displayHeight = 0;
  const bool hasCssHeight = imgStyle.hasImageHeight();
  const bool hasCssWidth = imgStyle.hasImageWidth();

  if (hasCssHeight && hasCssWidth && intrinsicW > 0 && intrinsicH > 0) {
    // Both CSS height and width set: resolve both, then clamp to container preserving ratio.
    displayHeight = static_cast<int>(imgStyle.imageHeight.toPixels(emSize, static_cast<float>(viewportHeight)) + 0.5f);
    displayWidth = static_cast<int>(imgStyle.imageWidth.toPixels(emSize, static_cast<float>(containerWidth)) + 0.5f);
    if (displayHeight < 1) displayHeight = 1;
    if (displayWidth < 1) displayWidth = 1;
    if (displayWidth > containerWidth || displayHeight > viewportHeight) {
      float scaleX = (displayWidth > containerWidth) ? static_cast<float>(containerWidth) / displayWidth : 1.0f;
      float scaleY = (displayHeight > viewportHeight) ? static_cast<float>(viewportHeight) / displayHeight : 1.0f;
      float scale = (scaleX < scaleY) ? scaleX : scaleY;
      displayWidth = static_cast<int>(displayWidth * scale + 0.5f);
      displayHeight = static_cast<int>(displayHeight * scale + 0.5f);
      if (displayWidth < 1) displayWidth = 1;
      if (displayHeight < 1) displayHeight = 1;
    }
  } else if (hasCssHeight && !hasCssWidth && intrinsicW > 0 && intrinsicH > 0) {
    // Use CSS height (resolve % against viewport height) and derive width from aspect ratio.
    displayHeight = static_cast<int>(imgStyle.imageHeight.toPixels(emSize, static_cast<float>(viewportHeight)) + 0.5f);
    if (displayHeight < 1) displayHeight = 1;
    displayWidth = static_cast<int>(displayHeight * (static_cast<float>(intrinsicW) / intrinsicH) + 0.5f);
    if (displayHeight > viewportHeight) {
      displayHeight = viewportHeight;
      displayWidth = static_cast<int>(displayHeight * (static_cast<float>(intrinsicW) / intrinsicH) + 0.5f);
      if (displayWidth < 1) displayWidth = 1;
    }
    if (displayWidth > containerWidth) {
      displayWidth = containerWidth;
      displayHeight = static_cast<int>(displayWidth * (static_cast<float>(intrinsicH) / intrinsicW) + 0.5f);
      if (displayHeight < 1) displayHeight = 1;
    }
    if (displayWidth < 1) displayWidth = 1;
  } else if (hasCssWidth && !hasCssHeight && intrinsicW > 0 && intrinsicH > 0) {
    // Use CSS width (resolve % against container width) and derive height from aspect ratio.
    displayWidth = static_cast<int>(imgStyle.imageWidth.toPixels(emSize, static_cast<float>(containerWidth)) + 0.5f);
    if (displayWidth > containerWidth) displayWidth = containerWidth;
    if (displayWidth < 1) displayWidth = 1;
    displayHeight = static_cast<int>(displayWidth * (static_cast<float>(intrinsicH) / intrinsicW) + 0.5f);
    if (displayHeight > viewportHeight) {
      displayHeight = viewportHeight;
      displayWidth = static_cast<int>(displayHeight * (static_cast<float>(intrinsicW) / intrinsicH) + 0.5f);
      if (displayWidth < 1) displayWidth = 1;
    }
    if (displayHeight < 1) displayHeight = 1;
  } else {
    // Scale to fit current container while maintaining aspect ratio; never upscale.
    int maxWidth = containerWidth;
    int maxHeight = viewportHeight;
    float scaleX = (intrinsicW > maxWidth) ? static_cast<float>(maxWidth) / intrinsicW : 1.0f;
    float scaleY = (intrinsicH > maxHeight) ? static_cast<float>(maxHeight) / intrinsicH : 1.0f;
    float scale = (scaleX < scaleY) ? scaleX : scaleY;
    if (scale > 1.0f) scale = 1.0f;
    displayWidth = static_cast<int>(intrinsicW * scale);
    displayHeight = static_cast<int>(intrinsicH * scale);
  }

  return {displayWidth, displayHeight};
}

}  // namespace compiled
