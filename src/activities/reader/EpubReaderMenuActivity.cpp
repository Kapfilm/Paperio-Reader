#include "EpubReaderMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "SdCardFontGlobals.h"
#include "activities/SliderPickerActivity.h"
#include "activities/settings/EnumSelectionActivity.h"
#include "activities/settings/SettingsSubmenuActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ReaderFontSizeOptions.h"

// Keep in sync with the default in EpubReaderActivity.h — see the comment there for rationale.
#ifndef ENABLE_BENCHMARKS
#define ENABLE_BENCHMARKS 0
#endif

namespace {
// Three-state cycle helper for overrides represented as int8_t with -1 = default.
// Mirrors the helpers in QuickOverridesActivity.cpp; kept duplicated rather
// than extracted so each activity stays self-contained for 16 lines of code.
//   slot 0 -> -1 (default)
//   slot 1 -> 1  (on)
//   slot 2 -> 0  (off)
uint8_t threeStateSlotFromOverride(int8_t value) {
  if (value < 0) return 0;
  if (value > 0) return 1;
  return 2;
}

int8_t threeStateOverrideFromSlot(uint8_t slot) {
  if (slot == 0) return -1;
  if (slot == 1) return 1;
  return 0;
}

// Returns the localized name of the family currently used as the global default
// for the reader. When the user has selected an SD card font globally, the
// override menu's "Default" label should reflect that family by name even
// though the per-book override list itself is built-in only.
std::string defaultFontFamilyLabel(const SettingInfo& item) {
  if (SETTINGS.sdFontFamilyName[0] != '\0') {
    const auto& families = sdFontSystem.registry().getFamilies();
    const auto it = std::find_if(families.begin(), families.end(),
                                 [](const auto& family) { return family.name == SETTINGS.sdFontFamilyName; });
    if (it != families.end()) {
      return std::string(SETTINGS.sdFontFamilyName);
    }
  }
  // Built-in: enumValues[0] is STR_DEFAULT_VALUE, [1..] are built-in families
  // in CrossPointSettings::FONT_FAMILY order.
  const auto idx = static_cast<size_t>(SETTINGS.fontFamily + 1);
  if (idx < item.enumValues.size()) {
    return I18N.get(item.enumValues[idx]);
  }
  return {};
}
}  // namespace

EpubReaderMenuActivity::EpubReaderMenuActivity(
    GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title, const int currentPage,
    const int totalPages, const int bookProgressPercent, const uint8_t currentOrientation, const bool hasFootnotes,
    const int8_t initialEmbeddedStyleOverride, const int8_t initialImageRenderingOverride,
    const int8_t initialFontFamilyOverride, const std::string& initialSdFontFamilyOverride,
    const int8_t initialFontSizeOverride, const int16_t initialLineHeightPercentOverride,
    const uint8_t initialTextDarkness, const bool initialBionicReadingOverride, const int8_t initialGuideDotsOverride,
    const int8_t initialParagraphAlignmentOverride, const int8_t initialTextAntiAliasingOverride,
    const int8_t initialHyphenationOverride, const int8_t initialFontSizeNormalizationOverride,
    const int8_t initialInlineFootnotePreviewsOverride, const bool hasStarredPages, const bool isCurrentPageStarred,
    const bool hasPrintedPages, const bool hasClippings, const uint8_t initialFontPointSizeOverride)
    : MenuListActivity("EpubReaderMenu", renderer, mappedInput),
      currentPageStarred(isCurrentPageStarred),
      pendingOrientation(currentOrientation),
      pendingEmbeddedStyleOverride(initialEmbeddedStyleOverride),
      pendingImageRenderingOverride(initialImageRenderingOverride),
      pendingFontFamilyOverride(initialFontFamilyOverride),
      pendingSdFontFamilyOverride(initialSdFontFamilyOverride),
      pendingFontSizeOverride(initialFontSizeOverride),
      pendingFontPointSizeOverride(initialFontPointSizeOverride),
      pendingLineHeightPercentOverride(initialLineHeightPercentOverride),
      pendingTextDarkness(initialTextDarkness),
      pendingBionicReading(initialBionicReadingOverride),
      pendingGuideDotsOverride(initialGuideDotsOverride),
      pendingParagraphAlignmentOverride(initialParagraphAlignmentOverride),
      pendingTextAntiAliasingOverride(initialTextAntiAliasingOverride),
      pendingHyphenationOverride(initialHyphenationOverride),
      pendingFontSizeNormalizationOverride(initialFontSizeNormalizationOverride),
      pendingInlineFootnotePreviewsOverride(initialInlineFootnotePreviewsOverride),
      title(title),
      currentPage(currentPage),
      totalPages(totalPages),
      bookProgressPercent(bookProgressPercent) {
  buildMenuItems(hasFootnotes, hasStarredPages, hasPrintedPages, hasClippings);
}

void EpubReaderMenuActivity::buildMenuItems(bool hasFootnotes, bool hasStarredPages, bool hasPrintedPages,
                                            bool hasClippings) {
  menuItems.reserve(26);

  // --- Navigation ---
  menuItems.push_back(SettingInfo::Separator(StrId::STR_READER_NAVIGATION));
  menuItems.push_back(SettingInfo::Action(StrId::STR_SELECT_CHAPTER, SettingAction::None));
  menuItems.push_back(SettingInfo::Action(StrId::STR_GO_TO_PERCENT, SettingAction::None));
  if (hasPrintedPages) {
    menuItems.push_back(SettingInfo::Action(StrId::STR_GO_TO_PRINTED_PAGE, SettingAction::None));
  }
  menuItems.push_back(SettingInfo::Action(StrId::STR_LOOKUP, SettingAction::None));
  menuItems.push_back(SettingInfo::Action(StrId::STR_SELECT_DICTIONARY, SettingAction::None));
  // Auto page turn belongs with the other ways to move through a book.
  menuItems.push_back(SettingInfo::Action(StrId::STR_AUTO_TURN_PAGES_PER_MIN, SettingAction::None));

  // Bookmarks, footnotes
  menuItems.push_back(SettingInfo::Separator(StrId::STR_READER_BOOKMARKS));

  menuItems.push_back(SettingInfo::Action(StrId::STR_STAR_PAGE, SettingAction::None));
  if (hasStarredPages) {
    menuItems.push_back(SettingInfo::Action(StrId::STR_STARRED_PAGES, SettingAction::None));
  }
  menuItems.push_back(
      SettingInfo::Action(StrId::STR_HIGHLIGHT_MARKER, SettingAction::None).withSubmenu(StrId::STR_CREATE_CLIPPING));
  menuItems.push_back(
      SettingInfo::Action(StrId::STR_HIGHLIGHT_UNDERLINE, SettingAction::None).withSubmenu(StrId::STR_CREATE_CLIPPING));
  menuItems.push_back(SettingInfo::Action(StrId::STR_HIGHLIGHT_BLACK_MARKER, SettingAction::None)
                          .withSubmenu(StrId::STR_CREATE_CLIPPING));
  if (hasClippings) {
    menuItems.push_back(SettingInfo::Action(StrId::STR_CLIPPINGS, SettingAction::None));
  }
  if (hasFootnotes) {
    menuItems.push_back(SettingInfo::Action(StrId::STR_FOOTNOTES, SettingAction::None));
  }

  // --- Appearance ---
  menuItems.push_back(SettingInfo::Separator(StrId::STR_READER_APPEARANCE));
  menuItems.push_back(SettingInfo::Action(StrId::STR_READING_PROFILES, SettingAction::None));

  auto* self = this;
  // Orientation: straightforward 0-3 cycle
  menuItems.push_back(SettingInfo::DynamicEnumCtx(
      StrId::STR_ORIENTATION,
      {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED, StrId::STR_LANDSCAPE_CCW}, self,
      [](const void* ctx) -> uint8_t { return static_cast<const EpubReaderMenuActivity*>(ctx)->pendingOrientation; },
      [](void* ctx, uint8_t v) { static_cast<EpubReaderMenuActivity*>(ctx)->pendingOrientation = v; }));

  // Embedded style: cycles default(-1) -> ON(1) -> OFF(0) via DynamicEnum indices 0/1/2
  menuItems.push_back(SettingInfo::DynamicEnumCtx(
                          StrId::STR_EMBEDDED_STYLE,
                          {StrId::STR_DEFAULT_VALUE, StrId::STR_STATE_ON, StrId::STR_STATE_OFF}, self,
                          [](const void* ctx) -> uint8_t {
                            const auto* s = static_cast<const EpubReaderMenuActivity*>(ctx);
                            if (s->pendingEmbeddedStyleOverride < 0) return 0;
                            if (s->pendingEmbeddedStyleOverride > 0) return 1;
                            return 2;
                          },
                          [](void* ctx, uint8_t v) {
                            auto* s = static_cast<EpubReaderMenuActivity*>(ctx);
                            if (v == 0)
                              s->pendingEmbeddedStyleOverride = -1;
                            else if (v == 1)
                              s->pendingEmbeddedStyleOverride = 1;
                            else
                              s->pendingEmbeddedStyleOverride = 0;
                          })
                          .withSubmenu(StrId::STR_READER_OVERRIDES));

  // Image rendering: cycles default(-1) -> display(0) -> placeholder(1) -> suppress(2)
  menuItems.push_back(SettingInfo::DynamicEnumCtx(
                          StrId::STR_IMAGES,
                          {StrId::STR_DEFAULT_VALUE, StrId::STR_IMAGES_DISPLAY, StrId::STR_IMAGES_PLACEHOLDER,
                           StrId::STR_IMAGES_SUPPRESS},
                          self,
                          [](const void* ctx) -> uint8_t {
                            const auto* s = static_cast<const EpubReaderMenuActivity*>(ctx);
                            return (s->pendingImageRenderingOverride < 0) ? 0 : (s->pendingImageRenderingOverride + 1);
                          },
                          [](void* ctx, uint8_t v) {
                            auto* s = static_cast<EpubReaderMenuActivity*>(ctx);
                            s->pendingImageRenderingOverride = (v == 0) ? -1 : static_cast<int8_t>(v - 1);
                          })
                          .withSubmenu(StrId::STR_READER_OVERRIDES));

  // Reader font family: default + built-ins + discovered SD families.
  {
    std::vector<StrId> values = {StrId::STR_DEFAULT_VALUE, StrId::STR_NOTO_SANS};
    const auto& families = sdFontSystem.registry().getFamilies();
    values.insert(values.end(), families.size(), StrId::STR_NONE_OPT);

    auto familySetting = SettingInfo::DynamicEnumCtx(
                             StrId::STR_FONT_FAMILY, values, self,
                             [](const void* ctx) -> uint8_t {
                               const auto* s = static_cast<const EpubReaderMenuActivity*>(ctx);
                               if (s->pendingFontFamilyOverride >= 0) {
                                 return static_cast<uint8_t>(s->pendingFontFamilyOverride + 1);
                               }
                               if (!s->pendingSdFontFamilyOverride.empty()) {
                                 const auto& fs = sdFontSystem.registry().getFamilies();
                                 for (size_t i = 0; i < fs.size(); i++) {
                                   if (fs[i].name == s->pendingSdFontFamilyOverride) {
                                     return static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + 1 + i);
                                   }
                                 }
                               }
                               return 0;
                             },
                             [](void* ctx, uint8_t v) {
                               auto* s = static_cast<EpubReaderMenuActivity*>(ctx);
                               if (v == 0) {
                                 s->pendingFontFamilyOverride = -1;
                                 s->pendingSdFontFamilyOverride.clear();
                                 return;
                               }

                               const uint8_t builtinCount = CrossPointSettings::BUILTIN_FONT_COUNT;
                               if (v <= builtinCount) {
                                 s->pendingFontFamilyOverride = static_cast<int8_t>(v - 1);
                                 s->pendingSdFontFamilyOverride.clear();
                                 return;
                               }

                               const size_t sdIdx = static_cast<size_t>(v - (builtinCount + 1));
                               const auto& fs = sdFontSystem.registry().getFamilies();
                               s->pendingFontFamilyOverride = -1;
                               if (sdIdx < fs.size()) {
                                 s->pendingSdFontFamilyOverride = fs[sdIdx].name;
                               } else {
                                 s->pendingSdFontFamilyOverride.clear();
                               }
                             })
                             .withSubmenu(StrId::STR_READER_OVERRIDES)
                             .withSelectorActivity();

    familySetting.enumLabels = {tr(STR_DEFAULT_VALUE), tr(STR_NOTO_SANS)};
    for (const auto& fam : families) {
      familySetting.enumLabels.push_back(fam.name);
    }
    menuItems.push_back(std::move(familySetting));
  }

  // Build the size picker when opened, after any pending family change.
  menuItems.push_back(
      SettingInfo::Action(StrId::STR_FONT_SIZE, SettingAction::None).withSubmenu(StrId::STR_READER_OVERRIDES));

  menuItems.push_back(
      SettingInfo::Action(StrId::STR_LINE_SPACING, SettingAction::None).withSubmenu(StrId::STR_READER_OVERRIDES));

  // Text darkness: straightforward 0-3 cycle
  menuItems.push_back(
      SettingInfo::DynamicEnumCtx(
          StrId::STR_TEXT_DARKNESS, {StrId::STR_NORMAL, StrId::STR_DARK, StrId::STR_EXTRA_DARK, StrId::STR_MAX_DARK},
          self,
          [](const void* ctx) -> uint8_t {
            return static_cast<const EpubReaderMenuActivity*>(ctx)->pendingTextDarkness;
          },
          [](void* ctx, uint8_t v) { static_cast<EpubReaderMenuActivity*>(ctx)->pendingTextDarkness = v; })
          .withSubmenu(StrId::STR_READER_OVERRIDES)
          .withSelectorActivity());

  menuItems.push_back(
      SettingInfo::DynamicEnumCtx(
          StrId::STR_BIONIC_READING, {StrId::STR_STATE_OFF, StrId::STR_STATE_ON}, self,
          [](const void* ctx) -> uint8_t {
            return static_cast<const EpubReaderMenuActivity*>(ctx)->pendingBionicReading ? 1 : 0;
          },
          [](void* ctx, uint8_t v) { static_cast<EpubReaderMenuActivity*>(ctx)->pendingBionicReading = (v != 0); })
          .withSubmenu(StrId::STR_READER_OVERRIDES));

  // Guide dots: default / on / off (mirrors QuickOverrides)
  menuItems.push_back(
      SettingInfo::DynamicEnumCtx(
          StrId::STR_GUIDE_DOTS, {StrId::STR_DEFAULT_VALUE, StrId::STR_STATE_ON, StrId::STR_STATE_OFF}, self,
          [](const void* ctx) -> uint8_t {
            return threeStateSlotFromOverride(
                static_cast<const EpubReaderMenuActivity*>(ctx)->pendingGuideDotsOverride);
          },
          [](void* ctx, uint8_t v) {
            static_cast<EpubReaderMenuActivity*>(ctx)->pendingGuideDotsOverride = threeStateOverrideFromSlot(v);
          })
          .withSubmenu(StrId::STR_READER_OVERRIDES));

  // Paragraph alignment: default(-1) + the 5 global options
  menuItems.push_back(SettingInfo::DynamicEnumCtx(
                          StrId::STR_PARA_ALIGNMENT,
                          {StrId::STR_DEFAULT_VALUE, StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER,
                           StrId::STR_ALIGN_RIGHT, StrId::STR_BOOK_S_STYLE},
                          self,
                          [](const void* ctx) -> uint8_t {
                            const auto* s = static_cast<const EpubReaderMenuActivity*>(ctx);
                            return (s->pendingParagraphAlignmentOverride < 0)
                                       ? 0
                                       : static_cast<uint8_t>(s->pendingParagraphAlignmentOverride + 1);
                          },
                          [](void* ctx, uint8_t v) {
                            auto* s = static_cast<EpubReaderMenuActivity*>(ctx);
                            s->pendingParagraphAlignmentOverride = (v == 0) ? -1 : static_cast<int8_t>(v - 1);
                          })
                          .withSubmenu(StrId::STR_READER_OVERRIDES)
                          .withSelectorActivity());

  // Text anti-aliasing: default / on / off (mirrors QuickOverrides)
  menuItems.push_back(
      SettingInfo::DynamicEnumCtx(
          StrId::STR_TEXT_AA, {StrId::STR_DEFAULT_VALUE, StrId::STR_STATE_ON, StrId::STR_STATE_OFF}, self,
          [](const void* ctx) -> uint8_t {
            return threeStateSlotFromOverride(
                static_cast<const EpubReaderMenuActivity*>(ctx)->pendingTextAntiAliasingOverride);
          },
          [](void* ctx, uint8_t v) {
            static_cast<EpubReaderMenuActivity*>(ctx)->pendingTextAntiAliasingOverride = threeStateOverrideFromSlot(v);
          })
          .withSubmenu(StrId::STR_READER_OVERRIDES));

  // Hyphenation: default / on / off (mirrors QuickOverrides)
  menuItems.push_back(
      SettingInfo::DynamicEnumCtx(
          StrId::STR_HYPHENATION, {StrId::STR_DEFAULT_VALUE, StrId::STR_STATE_ON, StrId::STR_STATE_OFF}, self,
          [](const void* ctx) -> uint8_t {
            return threeStateSlotFromOverride(
                static_cast<const EpubReaderMenuActivity*>(ctx)->pendingHyphenationOverride);
          },
          [](void* ctx, uint8_t v) {
            static_cast<EpubReaderMenuActivity*>(ctx)->pendingHyphenationOverride = threeStateOverrideFromSlot(v);
          })
          .withSubmenu(StrId::STR_READER_OVERRIDES));

  // Font size normalization: default / on / off (mirrors QuickOverrides)
  menuItems.push_back(SettingInfo::DynamicEnumCtx(
                          StrId::STR_FONT_SIZE_NORMALIZATION,
                          {StrId::STR_DEFAULT_VALUE, StrId::STR_STATE_ON, StrId::STR_STATE_OFF}, self,
                          [](const void* ctx) -> uint8_t {
                            return threeStateSlotFromOverride(
                                static_cast<const EpubReaderMenuActivity*>(ctx)->pendingFontSizeNormalizationOverride);
                          },
                          [](void* ctx, uint8_t v) {
                            static_cast<EpubReaderMenuActivity*>(ctx)->pendingFontSizeNormalizationOverride =
                                threeStateOverrideFromSlot(v);
                          })
                          .withSubmenu(StrId::STR_READER_OVERRIDES));

  menuItems.push_back(SettingInfo::DynamicEnumCtx(
                          StrId::STR_INLINE_FOOTNOTE_PREVIEWS,
                          {StrId::STR_DEFAULT_VALUE, StrId::STR_STATE_ON, StrId::STR_STATE_OFF}, self,
                          [](const void* ctx) -> uint8_t {
                            return threeStateSlotFromOverride(
                                static_cast<const EpubReaderMenuActivity*>(ctx)->pendingInlineFootnotePreviewsOverride);
                          },
                          [](void* ctx, uint8_t v) {
                            static_cast<EpubReaderMenuActivity*>(ctx)->pendingInlineFootnotePreviewsOverride =
                                threeStateOverrideFromSlot(v);
                          })
                          .withSubmenu(StrId::STR_READER_OVERRIDES));

  // --- Synchronisation (only if credentials are set) ---
  if (KOREADER_STORE.hasCredentials()) {
    menuItems.push_back(SettingInfo::Separator(StrId::STR_KOREADER_SYNC));
    menuItems.push_back(SettingInfo::Action(StrId::STR_PULL_PROGRESS_FROM_OTHER_DEVICES, SettingAction::None));
    menuItems.push_back(SettingInfo::Action(StrId::STR_PUSH_PROGRESS_FROM_THIS_DEVICE, SettingAction::None));
  }

  // --- Tools ---
  menuItems.push_back(SettingInfo::Separator(StrId::STR_READER_TOOLS));
  menuItems.push_back(
      SettingInfo::Action(StrId::STR_BOOK_INFO, SettingAction::None).withSubmenu(StrId::STR_READER_TOOLS));
  menuItems.push_back(SettingInfo::Action(StrId::STR_READING_STATS_FOR_THIS_BOOK, SettingAction::None)
                          .withSubmenu(StrId::STR_READER_TOOLS));
  menuItems.push_back(
      SettingInfo::Action(StrId::STR_MARK_AS_READ, SettingAction::None).withSubmenu(StrId::STR_READER_TOOLS));
  menuItems.push_back(
      SettingInfo::Action(StrId::STR_DELETE_CACHE, SettingAction::None).withSubmenu(StrId::STR_READER_TOOLS));
  menuItems.push_back(SettingInfo::Separator(StrId::STR_TOOL_UTILITIES).withSubmenu(StrId::STR_READER_TOOLS));
  menuItems.push_back(
      SettingInfo::Action(StrId::STR_SCREENSHOT_BUTTON, SettingAction::None).withSubmenu(StrId::STR_READER_TOOLS));
  menuItems.push_back(
      SettingInfo::Action(StrId::STR_DISPLAY_QR, SettingAction::None).withSubmenu(StrId::STR_READER_TOOLS));
  menuItems.push_back(SettingInfo::Action(StrId::STR_GO_HOME_BUTTON, SettingAction::None));
#if ENABLE_BENCHMARKS
  menuItems.push_back(SettingInfo::Separator(StrId::STR_NONE_OPT).withSubmenu(StrId::STR_READER_TOOLS));
  menuItems.push_back(
      SettingInfo::Action(StrId::STR_RENDER_BENCHMARK, SettingAction::None).withSubmenu(StrId::STR_READER_TOOLS));
#endif  // ENABLE_BENCHMARKS
}

EpubReaderMenuActivity::MenuAction EpubReaderMenuActivity::actionForNameId(StrId nameId) {
  switch (nameId) {
    case StrId::STR_READING_PROFILES:
      return MenuAction::READING_PROFILES;
    case StrId::STR_SELECT_CHAPTER:
      return MenuAction::SELECT_CHAPTER;
    case StrId::STR_GO_TO_PERCENT:
      return MenuAction::GO_TO_PERCENT;
    case StrId::STR_GO_TO_PRINTED_PAGE:
      return MenuAction::GO_TO_PRINTED_PAGE;
    case StrId::STR_LOOKUP:
      return MenuAction::DICTIONARY;
    case StrId::STR_SELECT_DICTIONARY:
      return MenuAction::SELECT_DICTIONARY;
    case StrId::STR_STARRED_PAGES:
      return MenuAction::STARRED_PAGES;
    case StrId::STR_STAR_PAGE:
      return MenuAction::STAR_PAGE;
    case StrId::STR_HIGHLIGHT_MARKER:
      return MenuAction::CREATE_CLIPPING_MARKER;
    case StrId::STR_HIGHLIGHT_UNDERLINE:
      return MenuAction::CREATE_CLIPPING_UNDERLINE;
    case StrId::STR_HIGHLIGHT_BLACK_MARKER:
      return MenuAction::CREATE_CLIPPING_BLACK_MARKER;
    case StrId::STR_CLIPPINGS:
      return MenuAction::VIEW_CLIPPINGS;
    case StrId::STR_FOOTNOTES:
      return MenuAction::FOOTNOTES;
    case StrId::STR_AUTO_TURN_PAGES_PER_MIN:
      return MenuAction::AUTO_PAGE_TURN;
    case StrId::STR_EMBEDDED_STYLE:
      return MenuAction::EMBEDDED_STYLE;
    case StrId::STR_IMAGES:
      return MenuAction::IMAGE_RENDERING;
    case StrId::STR_TEXT_DARKNESS:
      return MenuAction::TEXT_DARKNESS;
    case StrId::STR_ORIENTATION:
      return MenuAction::ROTATE_SCREEN;
    case StrId::STR_PULL_PROGRESS_FROM_OTHER_DEVICES:
      return MenuAction::PULL_REMOTE;
    case StrId::STR_PUSH_PROGRESS_FROM_THIS_DEVICE:
      return MenuAction::PUSH_LOCAL;
    case StrId::STR_SCREENSHOT_BUTTON:
      return MenuAction::SCREENSHOT;
    case StrId::STR_DISPLAY_QR:
      return MenuAction::DISPLAY_QR;
    case StrId::STR_MARK_AS_READ:
      return MenuAction::MARK_AS_READ;
    case StrId::STR_DELETE_CACHE:
      return MenuAction::DELETE_CACHE;
    case StrId::STR_RENDER_BENCHMARK:
      return MenuAction::RENDER_BENCHMARK;
    case StrId::STR_GO_HOME_BUTTON:
      return MenuAction::GO_HOME;
    case StrId::STR_READING_STATS_FOR_THIS_BOOK:
      return MenuAction::READING_STATS_FOR_BOOK;
    case StrId::STR_BOOK_INFO:
      return MenuAction::BOOK_INFO;
    default:
      return MenuAction::NONE;
  }
}

EpubReaderMenuActivity::MenuAction EpubReaderMenuActivity::actionForSettingAction(SettingAction action) {
  switch (action) {
    case SettingAction::None:
    case SettingAction::Submenu:
      return MenuAction::NONE;
    default:
      return MenuAction::NONE;
  }
}

void EpubReaderMenuActivity::finishWithAction(MenuAction action) {
  MenuResult payload{static_cast<int>(action),
                     -1,
                     pendingOrientation,
                     selectedPageTurnOption,
                     pendingEmbeddedStyleOverride,
                     pendingImageRenderingOverride,
                     pendingFontFamilyOverride,
                     pendingSdFontFamilyOverride,
                     pendingFontSizeOverride,
                     pendingTextDarkness,
                     static_cast<uint8_t>(pendingBionicReading),
                     pendingParagraphAlignmentOverride,
                     pendingTextAntiAliasingOverride,
                     pendingHyphenationOverride};
  // Appended after the file-browser fields, so set by name rather than position.
  payload.fontSizeNormalizationOverride = pendingFontSizeNormalizationOverride;
  payload.guideDotsOverride = pendingGuideDotsOverride;
  payload.inlineFootnotePreviewsOverride = pendingInlineFootnotePreviewsOverride;
  payload.lineHeightPercentOverride = pendingLineHeightPercentOverride;
  payload.fontPointSizeOverride = pendingFontPointSizeOverride;
  setResult(std::move(payload));
  finish();
}

void EpubReaderMenuActivity::onActionSelected(int index) {
  const auto& item = menuItems[index];

  if (item.nameId == StrId::STR_FONT_SIZE) {
    openFontSizeOverridePicker();
    return;
  }
  if (item.nameId == StrId::STR_LINE_SPACING) {
    openLineHeightOverridePicker();
    return;
  }

  // Auto page turn cycles locally (not a DynamicEnum because labels are raw strings)
  if (item.nameId == StrId::STR_AUTO_TURN_PAGES_PER_MIN) {
    selectedPageTurnOption = (selectedPageTurnOption + 1) % 5;
    requestUpdate();
    return;
  }

  // All other ACTION items finish with a result
  finishWithAction(actionForNameId(item.nameId));
}

void EpubReaderMenuActivity::onSettingToggled(int /*index*/) {
  // DynamicEnum items update pending state via their setters — no persistence needed.
}

void EpubReaderMenuActivity::onBackPressed() {
  ActivityResult result;
  result.isCancelled = true;
  MenuResult payload{-1,
                     -1,
                     pendingOrientation,
                     selectedPageTurnOption,
                     pendingEmbeddedStyleOverride,
                     pendingImageRenderingOverride,
                     pendingFontFamilyOverride,
                     pendingSdFontFamilyOverride,
                     pendingFontSizeOverride,
                     pendingTextDarkness,
                     static_cast<uint8_t>(pendingBionicReading),
                     pendingParagraphAlignmentOverride,
                     pendingTextAntiAliasingOverride,
                     pendingHyphenationOverride};
  // Appended after the file-browser fields, so set by name rather than position.
  payload.fontSizeNormalizationOverride = pendingFontSizeNormalizationOverride;
  payload.guideDotsOverride = pendingGuideDotsOverride;
  payload.inlineFootnotePreviewsOverride = pendingInlineFootnotePreviewsOverride;
  payload.lineHeightPercentOverride = pendingLineHeightPercentOverride;
  payload.fontPointSizeOverride = pendingFontPointSizeOverride;
  result.data = std::move(payload);
  setResult(std::move(result));
  finish();
}

std::string EpubReaderMenuActivity::getItemValueString(int index) const {
  const auto& item = menuItems[index];
  if (item.nameId == StrId::STR_FONT_SIZE) return fontSizeValueLabel();

  // Auto page turn: custom labels
  if (item.nameId == StrId::STR_AUTO_TURN_PAGES_PER_MIN) {
    if (selectedPageTurnOption == 0) return std::string(tr(STR_STATE_OFF));
    return std::string(pageTurnLabels[selectedPageTurnOption]);
  }

  // Star page: reflect current page's star state
  if (item.nameId == StrId::STR_STAR_PAGE) {
    return currentPageStarred ? std::string(tr(STR_STATE_ON)) : std::string(tr(STR_STATE_OFF));
  }

  if (item.nameId == StrId::STR_SELECT_DICTIONARY) {
    return SETTINGS.dictionaryName[0] == '\0' ? std::string(tr(STR_DICT_AUTOMATIC))
                                              : std::string(SETTINGS.dictionaryName);
  }

  if (item.nameId == StrId::STR_LINE_SPACING) {
    if (pendingLineHeightPercentOverride < 0) {
      return std::to_string(SETTINGS.lineHeightPercent) + "%";
    }
    return std::to_string(pendingLineHeightPercentOverride) + "%";
  }

  if (item.type == SettingType::ACTION) {
    if (item.action == SettingAction::Submenu) {
      return MenuListActivity::getItemValueString(index);
    }
    return {};
  }

  if (item.type == SettingType::ENUM) {
    if (item.nameId == StrId::STR_EMBEDDED_STYLE && pendingEmbeddedStyleOverride < 0) {
      return (SETTINGS.embeddedStyle != 0) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    }
    if (item.nameId == StrId::STR_IMAGES && pendingImageRenderingOverride < 0) {
      const auto defaultIndex = static_cast<size_t>(SETTINGS.imageRendering + 1);
      if (defaultIndex < item.enumValues.size()) {
        return I18N.get(item.enumValues[defaultIndex]);
      }
    }
    if (item.nameId == StrId::STR_FONT_FAMILY && pendingFontFamilyOverride < 0 && pendingSdFontFamilyOverride.empty()) {
      const auto label = defaultFontFamilyLabel(item);
      if (!label.empty()) {
        return label;
      }
    }
    if (item.nameId == StrId::STR_PARA_ALIGNMENT && pendingParagraphAlignmentOverride < 0) {
      const auto defaultIndex = static_cast<size_t>(SETTINGS.paragraphAlignment + 1);
      if (defaultIndex < item.enumValues.size()) {
        return I18N.get(item.enumValues[defaultIndex]);
      }
    }
    if (item.nameId == StrId::STR_TEXT_AA && pendingTextAntiAliasingOverride < 0) {
      return (SETTINGS.textAntiAliasing != 0) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    }
    if (item.nameId == StrId::STR_HYPHENATION && pendingHyphenationOverride < 0) {
      return (SETTINGS.hyphenationEnabled != 0) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    }
    if (item.nameId == StrId::STR_GUIDE_DOTS && pendingGuideDotsOverride < 0) {
      return (SETTINGS.guideDots != 0) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    }
    if (item.nameId == StrId::STR_INLINE_FOOTNOTE_PREVIEWS && pendingInlineFootnotePreviewsOverride < 0) {
      return (SETTINGS.inlineFootnotePreviews != 0) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    }
    if (item.nameId == StrId::STR_FONT_SIZE_NORMALIZATION && pendingFontSizeNormalizationOverride < 0) {
      return (SETTINGS.fontSizeNormalization != 0) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    }
  }

  // DynamicEnum items use the standard display
  return MenuListActivity::getItemValueString(index);
}

void EpubReaderMenuActivity::openSubmenu(const SettingInfo& submenuEntry) {
  auto it = std::find_if(submenuData.begin(), submenuData.end(),
                         [&submenuEntry](const SettingInfo::SubmenuData& d) { return d.id == submenuEntry.nameId; });
  if (it == submenuData.end()) return;

  auto itemValueStringOverride = [this](const SettingInfo& item) -> std::string {
    if (item.nameId == StrId::STR_FONT_SIZE) return fontSizeValueLabel();
    if (item.nameId == StrId::STR_LINE_SPACING) {
      if (pendingLineHeightPercentOverride < 0) {
        return std::to_string(SETTINGS.lineHeightPercent) + "%";
      }
      return std::to_string(pendingLineHeightPercentOverride) + "%";
    }
    if (item.type == SettingType::ACTION && item.action != SettingAction::Submenu) {
      return {};
    }
    if (item.nameId == StrId::STR_EMBEDDED_STYLE && pendingEmbeddedStyleOverride < 0) {
      return (SETTINGS.embeddedStyle != 0) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    }
    if (item.nameId == StrId::STR_IMAGES && pendingImageRenderingOverride < 0) {
      const auto valueIndex = static_cast<size_t>(SETTINGS.imageRendering + 1);
      if (valueIndex < item.enumValues.size()) {
        return I18N.get(item.enumValues[valueIndex]);
      }
    }
    if (item.nameId == StrId::STR_FONT_FAMILY && pendingFontFamilyOverride < 0 && pendingSdFontFamilyOverride.empty()) {
      const auto label = defaultFontFamilyLabel(item);
      if (!label.empty()) {
        return label;
      }
    }
    if (item.nameId == StrId::STR_PARA_ALIGNMENT && pendingParagraphAlignmentOverride < 0) {
      const auto valueIndex = static_cast<size_t>(SETTINGS.paragraphAlignment + 1);
      if (valueIndex < item.enumValues.size()) {
        return I18N.get(item.enumValues[valueIndex]);
      }
    }
    if (item.nameId == StrId::STR_TEXT_AA && pendingTextAntiAliasingOverride < 0) {
      return (SETTINGS.textAntiAliasing != 0) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    }
    if (item.nameId == StrId::STR_HYPHENATION && pendingHyphenationOverride < 0) {
      return (SETTINGS.hyphenationEnabled != 0) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    }
    if (item.nameId == StrId::STR_GUIDE_DOTS && pendingGuideDotsOverride < 0) {
      return (SETTINGS.guideDots != 0) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    }
    if (item.nameId == StrId::STR_INLINE_FOOTNOTE_PREVIEWS && pendingInlineFootnotePreviewsOverride < 0) {
      return (SETTINGS.inlineFootnotePreviews != 0) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    }
    if (item.nameId == StrId::STR_FONT_SIZE_NORMALIZATION && pendingFontSizeNormalizationOverride < 0) {
      return (SETTINGS.fontSizeNormalization != 0) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    }
    return item.getDisplayValue();
  };

  startActivityForResult(std::make_unique<SettingsSubmenuActivity>(renderer, mappedInput, submenuEntry.nameId,
                                                                   it->items, std::move(itemValueStringOverride),
                                                                   /*persistSettingsOnChange=*/false,
                                                                   /*overlayPanel=*/true),
                         [this](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             const auto* menuResult = std::get_if<MenuResult>(&result.data);
                             if (menuResult) {
                               if (menuResult->action != -1) {
                                 const auto action =
                                     actionForSettingAction(static_cast<SettingAction>(menuResult->action));
                                 if (action != MenuAction::NONE) {
                                   finishWithAction(action);
                                   return;
                                 }
                               }
                               if (menuResult->nameId != -1) {
                                 if (static_cast<StrId>(menuResult->nameId) == StrId::STR_FONT_SIZE) {
                                   openFontSizeOverridePicker();
                                   return;
                                 }
                                 if (static_cast<StrId>(menuResult->nameId) == StrId::STR_LINE_SPACING) {
                                   openLineHeightOverridePicker();
                                   return;
                                 }
                                 const auto action = actionForNameId(static_cast<StrId>(menuResult->nameId));
                                 if (action != MenuAction::NONE) {
                                   finishWithAction(action);
                                   return;
                                 }
                               }
                             }
                           }
                           requestUpdate();
                         });
}

std::vector<uint8_t> EpubReaderMenuActivity::availableFontSizes() const {
  if (SETTINGS.numericFontSizes && pendingFontFamilyOverride < 0) {
    const char* familyName = pendingSdFontFamilyOverride.empty() ? SETTINGS.sdFontFamilyName
                                                                : pendingSdFontFamilyOverride.c_str();
    if (const auto* family = sdFontSystem.registry().findFamily(familyName)) {
      auto sizes = family->availableSizes();
      if (!sizes.empty()) return sizes;
    }
  }
  return {12, 14, 16, 18};
}

std::string EpubReaderMenuActivity::fontSizeValueLabel() const {
  const bool inherited = pendingFontSizeOverride < 0 && !pendingFontPointSizeOverride;
  std::string label;
  if (SETTINGS.numericFontSizes) {
    const auto sizes = availableFontSizes();
    const uint8_t point = inherited ? SETTINGS.readerFontPointSize : pendingFontPointSizeOverride;
    const int8_t named = inherited ? static_cast<int8_t>(SETTINGS.fontSize) : pendingFontSizeOverride;
    const auto slot = reader_font_size::overrideSizeSlot(named, point, true, sizes);
    label = std::to_string(sizes[slot ? slot - 1 : 0]) + " pt";
  } else {
    static constexpr StrId names[] = {StrId::STR_SMALL, StrId::STR_MEDIUM, StrId::STR_LARGE, StrId::STR_X_LARGE};
    const uint8_t named = inherited ? SETTINGS.fontSize : pendingFontPointSizeOverride
        ? reader_font_size::nearestNamedSize(pendingFontPointSizeOverride) : static_cast<uint8_t>(pendingFontSizeOverride);
    label = I18N.get(names[named < 4 ? named : 1]);
  }
  return label;
}

void EpubReaderMenuActivity::openFontSizeOverridePicker() {
  fontSizePoints = availableFontSizes();
  fontSizePickerSetting = std::make_unique<SettingInfo>(SettingInfo::DynamicEnumCtx(
      StrId::STR_FONT_SIZE,
      {StrId::STR_DEFAULT_VALUE, StrId::STR_SMALL, StrId::STR_MEDIUM, StrId::STR_LARGE, StrId::STR_X_LARGE}, this,
      [](const void* ctx) -> uint8_t {
        const auto* self = static_cast<const EpubReaderMenuActivity*>(ctx);
        return reader_font_size::overrideSizeSlot(self->pendingFontSizeOverride, self->pendingFontPointSizeOverride,
                                                  SETTINGS.numericFontSizes, self->fontSizePoints);
      },
      [](void* ctx, uint8_t slot) {
        auto* self = static_cast<EpubReaderMenuActivity*>(ctx);
        const auto choice = reader_font_size::choiceForSlot(slot, SETTINGS.numericFontSizes, self->fontSizePoints);
        self->pendingFontSizeOverride = choice.named;
        self->pendingFontPointSizeOverride = choice.point;
      }));
  if (SETTINGS.numericFontSizes) {
    fontSizePickerSetting->enumLabels.emplace_back(tr(STR_DEFAULT_VALUE));
    for (uint8_t point : fontSizePoints) fontSizePickerSetting->enumLabels.push_back(std::to_string(point) + " pt");
  }
  // Stored on the parent: EnumSelectionActivity holds a reference throughout its lifetime.
  startActivityForResult(std::make_unique<EnumSelectionActivity>(renderer, mappedInput, *fontSizePickerSetting,
                                                                  nullptr, 0, /*overlayPanel=*/true),
                         [this](const ActivityResult&) { requestUpdate(); });
}

void EpubReaderMenuActivity::openLineHeightOverridePicker() {
  constexpr int kDefaultSentinel = CrossPointSettings::MIN_LINE_HEIGHT_PERCENT - 1;
  const int initialValue = pendingLineHeightPercentOverride < 0 ? kDefaultSentinel : pendingLineHeightPercentOverride;
  SliderPickerActivity::Config cfg{
      .titleId = StrId::STR_LINE_SPACING,
      .hintId = StrId::STR_SLIDER_STEP_HINT,
      .minValue = kDefaultSentinel,
      .maxValue = CrossPointSettings::MAX_LINE_HEIGHT_PERCENT,
      .initialValue = initialValue,
      .suffix = "%",
      .zeroLabel = std::string(tr(STR_DEFAULT_VALUE)) + " (" + std::to_string(SETTINGS.lineHeightPercent) + "%)",
      .showButtonStepHints = true,
      .firstNumericValue = CrossPointSettings::MIN_LINE_HEIGHT_PERCENT,
  };
  startActivityForResult(std::make_unique<SliderPickerActivity>(renderer, mappedInput, std::move(cfg),
                                                                 /*overlayPanel=*/true),
                         [this](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             const auto* selected = std::get_if<PercentResult>(&result.data);
                             if (selected) {
                               pendingLineHeightPercentOverride =
                                   selected->percent < CrossPointSettings::MIN_LINE_HEIGHT_PERCENT
                                       ? -1
                                       : static_cast<int16_t>(selected->percent);
                             }
                           }
                           requestUpdate();
                         });
}

void EpubReaderMenuActivity::toggleCurrentItem() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(menuItems.size())) return;
  const auto& item = menuItems[selectedIndex];
  if (item.isSeparator) return;

  if (item.type == SettingType::ACTION) {
    if (item.action == SettingAction::Submenu) {
      openSubmenu(item);
      return;
    }
    onActionSelected(selectedIndex);
    return;
  }

  if (item.usesSelectorActivity) {
    startActivityForResult(std::make_unique<EnumSelectionActivity>(renderer, mappedInput, menuItems[selectedIndex],
                                                                    nullptr, 0, /*overlayPanel=*/true),
                           [this](const ActivityResult&) { requestUpdate(); });
    return;
  }

  menuItems[selectedIndex].toggleValue();
  onSettingToggled(selectedIndex);
  requestUpdate();
}

const char* EpubReaderMenuActivity::tabLabel(const Tab value) const {
  static constexpr StrId ids[] = {StrId::STR_READER_TAB_BOOK, StrId::STR_READER_TAB_TEXT,
                                  StrId::STR_READER_TAB_OPTIONS};
  return I18N.get(ids[static_cast<size_t>(value)]);
}

void EpubReaderMenuActivity::prepareTabs() {
  Tab destination = Tab::Navigation;
  for (auto& item : menuItems) {
    if (item.isSeparator) {
      if (item.nameId == StrId::STR_READER_NAVIGATION) {
        destination = Tab::Navigation;
        continue;
      }
      if (item.nameId == StrId::STR_READER_BOOKMARKS) {
        destination = Tab::Navigation;
        tabItems[static_cast<size_t>(destination)].push_back(std::move(item));
        continue;
      }
      if (item.nameId == StrId::STR_READER_APPEARANCE) {
        destination = Tab::Settings;
        continue;
      }
      if (item.nameId == StrId::STR_KOREADER_SYNC) {
        destination = Tab::Tools;
        tabItems[static_cast<size_t>(destination)].push_back(std::move(item));
        continue;
      }
      if (item.nameId == StrId::STR_READER_TOOLS) {
        destination = Tab::Tools;
        continue;
      }
    }
    tabItems[static_cast<size_t>(destination)].push_back(std::move(item));
  }
  menuItems.clear();

  // The Tools tab itself is already the requested entry point: expose its
  // contents directly instead of showing another identically named submenu.
  auto& tools = tabItems[static_cast<size_t>(Tab::Tools)];
  for (auto& item : tools) {
    if (item.submenu == StrId::STR_READER_TOOLS) item.submenu = StrId::STR_NONE_OPT;
  }

  // Keep the familiar "Per-book overrides" section, but expose its contents
  // directly on the Appearance page instead of requiring another menu entry.
  auto& appearance = tabItems[static_cast<size_t>(Tab::Settings)];
  std::vector<SettingInfo> expandedAppearance;
  expandedAppearance.reserve(appearance.size() + 1);
  bool overrideHeadingInserted = false;
  for (auto& item : appearance) {
    if (item.submenu == StrId::STR_READER_OVERRIDES) {
      if (!overrideHeadingInserted) {
        expandedAppearance.push_back(SettingInfo::Separator(StrId::STR_READER_OVERRIDES));
        overrideHeadingInserted = true;
      }
      item.submenu = StrId::STR_NONE_OPT;
    }
    expandedAppearance.push_back(std::move(item));
  }
  appearance = std::move(expandedAppearance);

  for (auto& items : tabItems) {
    SettingInfo::prepareSubmenus(items, submenuData);
    SettingInfo::insertSubcategorySeparators(items);
  }
  submenusPrepared = true;
}

void EpubReaderMenuActivity::showTab(const Tab nextTab) {
  tab = nextTab;
  menuItems = tabItems[static_cast<size_t>(tab)];
  initMenuList();
  selectedIndex = -1;
  requestUpdate();
}

void EpubReaderMenuActivity::switchTab() {
  const auto next = (static_cast<size_t>(tab) + 1) % static_cast<size_t>(Tab::Count);
  showTab(static_cast<Tab>(next));
}

void EpubReaderMenuActivity::renderTabBar(const Rect& rect) const {
  constexpr int count = static_cast<int>(Tab::Count);
  const int sidePadding = UITheme::getInstance().getMetrics().contentSidePadding;
  const int tabsX = rect.x + sidePadding;
  const int tabsWidth = std::max(count, rect.width - sidePadding * 2);
  const int cellWidth = tabsWidth / count;
  const int fontId = UI_10_FONT_ID;
  const int textHeight = renderer.getTextHeight(fontId);

  for (int i = 0; i < count; i++) {
    const int x = tabsX + i * cellWidth;
    const int width = (i == count - 1) ? tabsX + tabsWidth - x : cellWidth;
    const bool active = i == static_cast<int>(tab);
    const bool focused = active && selectedIndex == -1;
    if (focused) {
      renderer.fillRoundedRect(x + 3, rect.y + 1, width - 6, rect.height - 4, 6, Color::Black);
    } else if (active) {
      renderer.fillRoundedRect(x + 3, rect.y + 1, width - 6, rect.height - 4, 6, Color::LightGray);
      renderer.drawLine(x + 3, rect.y + rect.height - 3, x + width - 4, rect.y + rect.height - 3, 2, true);
    }

    const char* label = tabLabel(static_cast<Tab>(i));
    const std::string fitted = renderer.truncatedText(fontId, label, width - 8);
    const int textWidth = renderer.getTextWidth(fontId, fitted.c_str());
    const int textX = x + std::max(0, (width - textWidth) / 2);
    const int textY = rect.y + std::max(0, (rect.height - textHeight) / 2);
    renderer.drawText(fontId, textX, textY, fitted.c_str(), !focused);
  }
  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width - 1, rect.y + rect.height - 1);
}

void EpubReaderMenuActivity::onEnter() {
  Activity::onEnter();
  if (!submenusPrepared) prepareTabs();
  showTab(Tab::Navigation);
}

void EpubReaderMenuActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBackPressed();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedIndex < 0) {
      switchTab();
    } else {
      toggleCurrentItem();
    }
    return;
  }

  buttonNavigator.onNextPress([this] {
    if (selectedIndex < 0) {
      selectedIndex = buttonNavigator.nextIndex(-1);
    } else {
      const int next = buttonNavigator.nextIndex(selectedIndex);
      selectedIndex = next <= selectedIndex ? -1 : next;
    }
    requestUpdate();
  });
  buttonNavigator.onPreviousPress([this] {
    if (selectedIndex < 0) {
      selectedIndex = buttonNavigator.previousIndex(0);
    } else {
      const int previous = buttonNavigator.previousIndex(selectedIndex);
      selectedIndex = previous >= selectedIndex ? -1 : previous;
    }
    requestUpdate();
  });
}

void EpubReaderMenuActivity::render(RenderLock&&) {
  // Overlay the menu on the displayed reader page. Only the lower three
  // quarters are cleared, so the top quarter remains readable as context.
  renderer.syncWriteBufferFromDisplayed();
  const int panelTop = renderer.getScreenHeight() / 4;
  renderer.fillRoundedRect(0, panelTop, renderer.getScreenWidth(), renderer.getScreenHeight() - panelTop, 12,
                           true, true, false, false, Color::White);
  renderer.drawRoundedRect(0, panelTop, renderer.getScreenWidth(), renderer.getScreenHeight() - panelTop, 2, 12,
                           true, true, false, false, true);

  Rect contentRect = UITheme::getContentRect(renderer, true, false);
  const int contentBottom = contentRect.y + contentRect.height;
  contentRect.y = std::max(contentRect.y, panelTop + 2);
  contentRect.height = std::max(0, contentBottom - contentRect.y);

  // Title
  const std::string truncTitle =
      renderer.truncatedText(UI_12_FONT_ID, title.c_str(), contentRect.width - 40, EpdFontFamily::BOLD);
  const int titleX =
      contentRect.x +
      (contentRect.width - renderer.getTextWidth(UI_12_FONT_ID, truncTitle.c_str(), EpdFontFamily::BOLD)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentRect.y, truncTitle.c_str(), true, EpdFontFamily::BOLD);

  // Progress summary
  std::string progressLine;
  if (totalPages > 0) {
    progressLine = std::string(tr(STR_CHAPTER_PREFIX)) + std::to_string(currentPage) + "/" +
                   std::to_string(totalPages) + std::string(tr(STR_PAGES_SEPARATOR));
  }
  progressLine += std::string(tr(STR_BOOK_PREFIX)) + std::to_string(bookProgressPercent) + "%";
  renderer.drawCenteredText(UI_10_FONT_ID, 48 + contentRect.y, progressLine.c_str());

  const auto& metrics = UITheme::getInstance().getMetrics();
  // Match the main Settings header: one 3 px separator immediately before the tabs.
  // Give the chapter/progress summary a little more breathing room before
  // the separator and tabs.
  const int tabTop = 86 + contentRect.y;
  renderer.drawLine(contentRect.x, tabTop - 3, contentRect.x + contentRect.width - 1, tabTop - 3, 3, true);
  renderTabBar(Rect{contentRect.x, tabTop, contentRect.width, metrics.tabBarHeight});

  // Menu Items
  const int startY = tabTop + metrics.tabBarHeight + metrics.verticalSpacing;
  const int listHeight = contentRect.height - (startY - contentRect.y);
  drawMenuList(Rect{contentRect.x, startY, contentRect.width, listHeight});

  // Footer / Hints
  const char* confirm = tr(STR_SELECT);
  if (selectedIndex == -1) {
    confirm = tabLabel(static_cast<Tab>((static_cast<size_t>(tab) + 1) % static_cast<size_t>(Tab::Count)));
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirm, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayWindow(0, panelTop, renderer.getScreenWidth(), renderer.getScreenHeight() - panelTop);
}
