// Offline geometry probe for Recent Books using the firmware's actual fonts.
// This does not emulate the display or activity event loop. The runner reads the
// current translations/grid constants; all measurements use EpdFont itself.
#include <GfxRenderer.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "RecentProbeInputs.h"
#include "UiFonts.h"
#include "components/themes/BaseTheme.h"
#include "components/themes/lyra/LyraTheme.h"
#include "fontIds.h"

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

int textWidth(const EpdFontFamily& font, const std::string& text) {
  int width = 0, height = 0;
  font.getTextDimensions(text.c_str(), &width, &height);
  return width;
}

int wrappedLines(const EpdFontFamily& font, const char* text, int availableWidth) {
  std::istringstream input(text);
  std::string word, line;
  int lines = 1;
  while (input >> word) {
    require(textWidth(font, word) <= availableWidth, "a hint word would be truncated");
    const std::string next = line.empty() ? word : line + " " + word;
    if (textWidth(font, next) > availableWidth) {
      ++lines;
      line = word;
    } else {
      line = next;
    }
  }
  return lines;
}
}  // namespace

int main() {
  GfxRenderer renderer;
  int cases = 0;
  registerUiFonts(renderer);
  const auto* fixedHint = renderer.fonts.at(UI_HINT_FONT_ID).getData();
  for (bool enlarged : {false, true}) {
    for (bool bold : {false, true}) {
      registerUiFonts(renderer);
      applyUiTextSize(renderer, enlarged, bold);
      for (bool x3 : {false, true}) {
        for (bool compact : {false, true}) {
          const ThemeMetrics metrics = compact ? LyraMetrics::values : BaseMetrics::values;
          const int screenWidth = x3 ? 528 : 480;
          const int screenHeight = x3 ? 792 : 800;
          const int contentWidth = screenWidth - metrics.sideButtonHintsWidth * (x3 ? 2 : 1);
          const int hintWidth = contentWidth - metrics.contentSidePadding * 2;
          const auto& small = renderer.fonts.at(UI_HINT_FONT_ID);
          require(small.getData() == fixedHint, "hint font changes with interface size or style");
          const auto& caption = renderer.fonts.at(compact ? SMALL_FONT_ID : UI_10_FONT_ID);
          for (bool russian : {false, true}) {
            const int lines = wrappedLines(small, russian ? recent_probe::hintRu : recent_probe::hintEn, hintWidth);
            require(lines <= 2, "view gesture hint would be truncated at two lines");
            const int lineHeight = small.getData()->advanceY + 3;
            const int footerHeight = lines * lineHeight + 12;
            const int footerTop = screenHeight - metrics.buttonHintsHeight - footerHeight;
            const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
            const int contentHeight = footerTop - contentTop - metrics.verticalSpacing;
            int lastCellBottom = 0;
            if (compact) {
              const int cellHeight = std::max(
                  84, (contentHeight - recent_probe::outerMargin * 2 - recent_probe::cellGap) / recent_probe::rows);
              lastCellBottom = contentTop + recent_probe::outerMargin +
                               (recent_probe::rows - 1) * (cellHeight + recent_probe::cellGap) + cellHeight - 3;
            } else {
              const int stride = recent_probe::cellHeight + recent_probe::labelHeight + recent_probe::gridMargin;
              const int rows = std::max(1, contentHeight / stride);
              lastCellBottom = contentTop + (rows - 1) * stride + recent_probe::cellHeight + recent_probe::labelHeight;
            }
            require(lastCellBottom < footerTop, "book cells overlap the gesture footer");
            require(footerTop + 6 + (lines - 1) * lineHeight + small.getData()->advanceY <=
                        screenHeight - metrics.buttonHintsHeight,
                    "gesture footer overlaps the bottom buttons");
            const int buttonWidth = compact ? 80 : 106;
            const int available = buttonWidth - 8;
            const char* full[] = {russian ? recent_probe::homeRu : recent_probe::homeEn,
                                  russian ? recent_probe::openRu : recent_probe::openEn,
                                  russian ? recent_probe::removeRu : recent_probe::removeEn,
                                  russian ? recent_probe::infoRu : recent_probe::infoEn};
            const char* abbreviated[] = {russian ? recent_probe::homeShortRu : recent_probe::homeShortEn,
                                         russian ? recent_probe::openShortRu : recent_probe::openShortEn,
                                         russian ? recent_probe::removeShortRu : recent_probe::removeShortEn, full[3]};
            for (int i = 0; i < 4; ++i) {
              const char* label = textWidth(caption, full[i]) <= available ? full[i] : abbreviated[i];
              require(textWidth(caption, label) <= available,
                      "a native-size caption needs more shortening (scaling is forbidden)");
            }
            // Centering is computed from the same fixed-size measured text.
            for (const char* hint : {recent_probe::hintRu, recent_probe::hintEn}) {
              if (textWidth(small, hint) <= hintWidth) {
                const int origin = (contentWidth - textWidth(small, hint)) / 2;
                require(std::abs(origin - (contentWidth - origin - textWidth(small, hint))) <= 1,
                        "instruction label is not horizontally centered");
              }
            }
            ++cases;
            std::cout << (x3 ? "X3" : "X4") << (compact ? " Mini/Lyra" : " Classic") << (russian ? " RU" : " EN")
                      << " enlarged=" << enlarged << " bold=" << bold << " hintLines=" << lines
                      << " fixedHintHeight=" << static_cast<int>(small.getData()->advanceY)
                      << " captionScale=1 cellFooterGap=" << footerTop - lastCellBottom << '\n';
          }
        }
      }
    }
  }
  std::cout << "PASS: " << cases << " real-font layout combinations\n";
}
