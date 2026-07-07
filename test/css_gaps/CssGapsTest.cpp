#include <cstdio>
#include <string>

#include "../../lib/Epub/Epub/css/CssParser.h"

// Minimal test harness matching the style of CssParserTest.cpp
static int testsPassed = 0;
static int testsFailed = 0;

#define ASSERT_EQ(a, b)                                                                                         \
  do {                                                                                                          \
    if ((a) != (b)) {                                                                                           \
      fprintf(stderr, "  FAIL: %s:%d  %s == %s  (got %d, expected %d)\n", __FILE__, __LINE__, #a, #b, (int)(a), \
              (int)(b));                                                                                        \
      testsFailed++;                                                                                            \
      return;                                                                                                   \
    }                                                                                                           \
  } while (0)

#define ASSERT_TRUE(cond)                                                         \
  do {                                                                            \
    if (!(cond)) {                                                                \
      fprintf(stderr, "  FAIL: %s:%d  %s is false\n", __FILE__, __LINE__, #cond); \
      testsFailed++;                                                              \
      return;                                                                     \
    }                                                                             \
  } while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

static bool assertFloatNear(float got, float expected, float tol, const char* expr, const char* expStr,
                            const char* file, int line) {
  if (got < expected - tol || got > expected + tol) {
    fprintf(stderr, "  FAIL: %s:%d  %s ≈ %s  (got %.4f, expected %.4f ±%.4f)\n", file, line, expr, expStr, got,
            expected, tol);
    testsFailed++;
    return false;
  }
  return true;
}
#define ASSERT_FLOAT_NEAR(a, b, tol)                                                                \
  do {                                                                                              \
    if (!assertFloatNear((float)(a), (float)(b), (float)(tol), #a, #b, __FILE__, __LINE__)) return; \
  } while (0)

#define PASS()     \
  do {             \
    testsPassed++; \
  } while (0)

// ============================================================================
// Gap 3: list-style-type / list-style: none
// ============================================================================

void testListStyleTypeNone() {
  printf("testListStyleTypeNone...\n");
  const CssStyle style = CssParser::parseInlineStyle("list-style-type: none");
  ASSERT_TRUE(style.hasListStyleNone());
  ASSERT_TRUE(style.listStyleNone);
  PASS();
}

void testListStyleShorthandNone() {
  printf("testListStyleShorthandNone...\n");
  const CssStyle style = CssParser::parseInlineStyle("list-style: none");
  ASSERT_TRUE(style.hasListStyleNone());
  ASSERT_TRUE(style.listStyleNone);
  PASS();
}

void testListStyleTypeDisc_notNone() {
  printf("testListStyleTypeDisc_notNone...\n");
  const CssStyle style = CssParser::parseInlineStyle("list-style-type: disc");
  // disc is the default — either not set or explicitly false
  ASSERT_FALSE(style.hasListStyleNone() && style.listStyleNone);
  PASS();
}

// ============================================================================
// Gap 4: page-break-before / page-break-after
// ============================================================================

void testPageBreakBeforeAlways() {
  printf("testPageBreakBeforeAlways...\n");
  const CssStyle style = CssParser::parseInlineStyle("page-break-before: always");
  ASSERT_TRUE(style.hasPageBreakBefore());
  ASSERT_TRUE(style.pageBreakBefore);
  PASS();
}

void testPageBreakAfterAlways() {
  printf("testPageBreakAfterAlways...\n");
  const CssStyle style = CssParser::parseInlineStyle("page-break-after: always");
  ASSERT_TRUE(style.hasPageBreakAfter());
  ASSERT_TRUE(style.pageBreakAfter);
  PASS();
}

void testBreakBeforePage_css3() {
  printf("testBreakBeforePage_css3...\n");
  const CssStyle style = CssParser::parseInlineStyle("break-before: page");
  ASSERT_TRUE(style.hasPageBreakBefore());
  ASSERT_TRUE(style.pageBreakBefore);
  PASS();
}

void testBreakAfterPage_css3() {
  printf("testBreakAfterPage_css3...\n");
  const CssStyle style = CssParser::parseInlineStyle("break-after: page");
  ASSERT_TRUE(style.hasPageBreakAfter());
  ASSERT_TRUE(style.pageBreakAfter);
  PASS();
}

void testPageBreakBeforeAuto_notSet() {
  printf("testPageBreakBeforeAuto_notSet...\n");
  const CssStyle style = CssParser::parseInlineStyle("page-break-before: auto");
  // auto should not set the break flag
  ASSERT_FALSE(style.hasPageBreakBefore() && style.pageBreakBefore);
  PASS();
}

// ============================================================================
// Gap 2: line-height
// ============================================================================

void testLineHeightUnitless() {
  printf("testLineHeightUnitless...\n");
  const CssStyle style = CssParser::parseInlineStyle("line-height: 1.5");
  ASSERT_TRUE(style.hasLineHeight());
  // 1.5 normalised around 1.5 base => 100%. Allow ±5%.
  ASSERT_FLOAT_NEAR(style.lineHeightMultiplier, 1.0f, 0.05f);
  PASS();
}

void testLineHeightPercent() {
  printf("testLineHeightPercent...\n");
  const CssStyle style = CssParser::parseInlineStyle("line-height: 150%");
  ASSERT_TRUE(style.hasLineHeight());
  ASSERT_FLOAT_NEAR(style.lineHeightMultiplier, 1.0f, 0.05f);
  PASS();
}

void testLineHeightEm() {
  printf("testLineHeightEm...\n");
  const CssStyle style = CssParser::parseInlineStyle("line-height: 1.5em");
  ASSERT_TRUE(style.hasLineHeight());
  ASSERT_FLOAT_NEAR(style.lineHeightMultiplier, 1.0f, 0.05f);
  PASS();
}

void testLineHeightSmallerValue() {
  printf("testLineHeightSmallerValue...\n");
  // line-height: 1.0 (compact) should give a multiplier < 1.0
  const CssStyle style = CssParser::parseInlineStyle("line-height: 1.0");
  ASSERT_TRUE(style.hasLineHeight());
  ASSERT_TRUE(style.lineHeightMultiplier < 1.0f);
  PASS();
}

void testLineHeightLargerValue() {
  printf("testLineHeightLargerValue...\n");
  // line-height: 2.0 (spacious) should give a multiplier > 1.0
  const CssStyle style = CssParser::parseInlineStyle("line-height: 2.0");
  ASSERT_TRUE(style.hasLineHeight());
  ASSERT_TRUE(style.lineHeightMultiplier > 1.0f);
  PASS();
}

void testLineHeightClampMin() {
  printf("testLineHeightClampMin...\n");
  // Very small value must be clamped to minimum (0.7)
  const CssStyle style = CssParser::parseInlineStyle("line-height: 0.1");
  ASSERT_TRUE(style.hasLineHeight());
  ASSERT_TRUE(style.lineHeightMultiplier >= 0.7f);
  PASS();
}

void testLineHeightClampMax() {
  printf("testLineHeightClampMax...\n");
  // Very large value must be clamped to maximum (2.0)
  const CssStyle style = CssParser::parseInlineStyle("line-height: 10.0");
  ASSERT_TRUE(style.hasLineHeight());
  ASSERT_TRUE(style.lineHeightMultiplier <= 2.0f);
  PASS();
}

void testLineHeightNormal_notSet() {
  printf("testLineHeightNormal_notSet...\n");
  // 'normal' keyword should not set a line-height override
  const CssStyle style = CssParser::parseInlineStyle("line-height: normal");
  ASSERT_FALSE(style.hasLineHeight());
  PASS();
}

// ============================================================================
// Gap 1: font-size (heading scaling)
// ============================================================================

void testFontSizePercent160() {
  printf("testFontSizePercent160...\n");
  const CssStyle style = CssParser::parseInlineStyle("font-size: 160%");
  ASSERT_TRUE(style.hasFontSizeMultiplier());
  ASSERT_FLOAT_NEAR(style.fontSizeMultiplier, 1.6f, 0.05f);
  PASS();
}

void testFontSizeEm() {
  printf("testFontSizeEm...\n");
  const CssStyle style = CssParser::parseInlineStyle("font-size: 1.4em");
  ASSERT_TRUE(style.hasFontSizeMultiplier());
  ASSERT_FLOAT_NEAR(style.fontSizeMultiplier, 1.4f, 0.05f);
  PASS();
}

void testFontSizeSmaller() {
  printf("testFontSizeSmaller...\n");
  const CssStyle style = CssParser::parseInlineStyle("font-size: 80%");
  ASSERT_TRUE(style.hasFontSizeMultiplier());
  ASSERT_FLOAT_NEAR(style.fontSizeMultiplier, 0.8f, 0.05f);
  PASS();
}

// ============================================================================
// font-variant: small-caps
// ============================================================================

void testFontVariantSmallCaps() {
  printf("testFontVariantSmallCaps...\n");
  const CssStyle style = CssParser::parseInlineStyle("font-variant: small-caps");
  ASSERT_TRUE(style.hasSmallCaps());
  ASSERT_TRUE(style.smallCaps);
  PASS();
}

void testFontVariantCapsLonghand() {
  printf("testFontVariantCapsLonghand...\n");
  const CssStyle style = CssParser::parseInlineStyle("font-variant-caps: small-caps");
  ASSERT_TRUE(style.hasSmallCaps());
  ASSERT_TRUE(style.smallCaps);
  PASS();
}

void testFontVariantNormalCancels() {
  printf("testFontVariantNormalCancels...\n");
  const CssStyle style = CssParser::parseInlineStyle("font-variant: normal");
  // "normal" is an explicit value so it can cancel inherited small-caps.
  ASSERT_TRUE(style.hasSmallCaps());
  ASSERT_FALSE(style.smallCaps);
  PASS();
}

void testFontVariantUnknown_notSet() {
  printf("testFontVariantUnknown_notSet...\n");
  const CssStyle style = CssParser::parseInlineStyle("font-variant: oldstyle-nums");
  // Unrecognised value: leave the property undefined so inheritance is unaffected.
  ASSERT_FALSE(style.hasSmallCaps());
  PASS();
}

void testFontVariantCaseInsensitive() {
  printf("testFontVariantCaseInsensitive...\n");
  const CssStyle style = CssParser::parseInlineStyle("FONT-VARIANT : SMALL-CAPS ;");
  ASSERT_TRUE(style.hasSmallCaps());
  ASSERT_TRUE(style.smallCaps);
  PASS();
}

// ============================================================================
// CSS ID selector support (#id, tag#id)
// ============================================================================

// Helper: load CSS rules from a string into a caller-supplied CssParser.
// CssParser is non-copyable/movable, so we populate in place.
static void loadCssFromString(CssParser& parser, const char* css) {
  HalFile f = HalFile::fromString(css);
  parser.loadFromStream(f);
}

void testIdSelectorBasic() {
  printf("testIdSelectorBasic...\n");
  CssParser parser("");
  loadCssFromString(parser, "#hero { font-weight: bold; }");
  const CssStyle style = parser.resolveStyle("p", "", "hero");
  ASSERT_TRUE(style.hasFontWeight());
  ASSERT_EQ(style.fontWeight, CssFontWeight::Bold);
  PASS();
}

void testIdSelectorNotMatchedOnOtherElement() {
  printf("testIdSelectorNotMatchedOnOtherElement...\n");
  CssParser parser("");
  loadCssFromString(parser, "#hero { font-weight: bold; }");
  // No id attr — should not pick up the #hero rule
  const CssStyle style = parser.resolveStyle("p", "", "");
  ASSERT_FALSE(style.hasFontWeight());
  PASS();
}

void testTagIdSelector() {
  printf("testTagIdSelector...\n");
  // tag#id is more specific than #id — both applied, tag#id wins on conflict
  CssParser parser("");
  loadCssFromString(parser, "#intro { text-align: left; } p#intro { text-align: center; }");
  const CssStyle style = parser.resolveStyle("p", "", "intro");
  ASSERT_TRUE(style.hasTextAlign());
  ASSERT_EQ(style.textAlign, CssTextAlign::Center);
  PASS();
}

void testIdCascadeOverClass() {
  printf("testIdCascadeOverClass...\n");
  // #id must override .class on the same property
  CssParser parser("");
  loadCssFromString(parser, ".note { text-align: left; } #special { text-align: right; }");
  const CssStyle style = parser.resolveStyle("p", "note", "special");
  ASSERT_TRUE(style.hasTextAlign());
  ASSERT_EQ(style.textAlign, CssTextAlign::Right);
  PASS();
}

void testIdCascadeOverTag() {
  printf("testIdCascadeOverTag...\n");
  CssParser parser("");
  loadCssFromString(parser, "p { text-align: left; } #override { text-align: center; }");
  const CssStyle style = parser.resolveStyle("p", "", "override");
  ASSERT_TRUE(style.hasTextAlign());
  ASSERT_EQ(style.textAlign, CssTextAlign::Center);
  PASS();
}

void testIdSelectorCaseNormalized() {
  printf("testIdSelectorCaseNormalized...\n");
  // CSS id selectors are case-sensitive by spec, but we normalize to lowercase
  // consistently (same as class selectors) to avoid common EPUB authoring issues.
  CssParser parser("");
  loadCssFromString(parser, "#MyID { font-style: italic; }");
  const CssStyle style = parser.resolveStyle("span", "", "myid");
  ASSERT_TRUE(style.hasFontStyle());
  ASSERT_EQ(style.fontStyle, CssFontStyle::Italic);
  PASS();
}

void testIdSelectorNotAffectUnrelatedElement() {
  printf("testIdSelectorNotAffectUnrelatedElement...\n");
  CssParser parser("");
  loadCssFromString(parser, "#toc { font-weight: bold; }");
  // Element without the matching id should not get the rule
  const CssStyle style = parser.resolveStyle("div", "", "chapter");
  ASSERT_FALSE(style.hasFontWeight());
  PASS();
}

void testIdSelectorGrouped() {
  printf("testIdSelectorGrouped...\n");
  // Grouped selector: #a, #b { } should store two rules
  CssParser parser("");
  loadCssFromString(parser, "#alpha, #beta { font-style: italic; }");
  const CssStyle styleA = parser.resolveStyle("p", "", "alpha");
  const CssStyle styleB = parser.resolveStyle("p", "", "beta");
  ASSERT_TRUE(styleA.hasFontStyle());
  ASSERT_EQ(styleA.fontStyle, CssFontStyle::Italic);
  ASSERT_TRUE(styleB.hasFontStyle());
  ASSERT_EQ(styleB.fontStyle, CssFontStyle::Italic);
  PASS();
}

// ============================================================================
// StackBuffer overflow handling (selector / declaration truncation)
//
// The streaming parser buffers each selector group and each declaration in a
// fixed 1024-byte StackBuffer. Before the overflow flag was added, content past
// the cap was silently dropped: a truncated selector could be parsed as a bogus
// rule, and a truncated declaration could be parsed as garbage. These tests pin
// the recovery behaviour — oversized tokens are skipped, and valid rules before
// and after them still parse.
// ============================================================================

void testOverflowedSelectorGroupDropsUsablePrefix() {
  printf("testOverflowedSelectorGroupDropsUsablePrefix...\n");
  CssParser parser("");
  // A comma group whose first member (#keep) is short and usable, followed by a
  // giant filler selector that overflows the 1024-byte StackBuffer. The whole rule
  // must be dropped: without the overflow guard the truncated buffer still begins
  // with "#keep, ..." and #keep would be stored as a bogus rule.
  std::string css = "#keep, #";
  css.append(2000, 'x');
  css += " { font-weight: bold; }";
  loadCssFromString(parser, css.c_str());

  const CssStyle style = parser.resolveStyle("p", "", "keep");
  ASSERT_FALSE(style.hasFontWeight());  // giant rule dropped in full — #keep not stored
  PASS();
}

void testParserRecoversAfterOverflowedSelector() {
  printf("testParserRecoversAfterOverflowedSelector...\n");
  CssParser parser("");
  // The dropped giant rule must not derail parsing: valid rules before and after
  // it still resolve.
  std::string css = "p { text-align: center; } #";
  css.append(2000, 'x');
  css += " { font-weight: bold; } #ok { font-style: italic; }";
  loadCssFromString(parser, css.c_str());

  const CssStyle before = parser.resolveStyle("p", "", "");
  ASSERT_TRUE(before.hasTextAlign());
  ASSERT_EQ(before.textAlign, CssTextAlign::Center);

  const CssStyle after = parser.resolveStyle("span", "", "ok");
  ASSERT_TRUE(after.hasFontStyle());
  ASSERT_EQ(after.fontStyle, CssFontStyle::Italic);
  PASS();
}

void testOverflowedDeclarationNotParsedAsProperty() {
  printf("testOverflowedDeclarationNotParsedAsProperty...\n");
  CssParser parser("");
  // A "text-align:" declaration padded past the buffer. Without the overflow guard
  // the truncated "text-align:xxxx..." is parsed, interpretAlignment() falls back to
  // Left, and hasTextAlign() becomes true. With the guard the declaration is dropped.
  std::string css = "p { text-align:";
  css.append(2000, 'x');
  css += "; }";
  loadCssFromString(parser, css.c_str());

  const CssStyle style = parser.resolveStyle("p", "", "");
  ASSERT_FALSE(style.hasTextAlign());  // truncated declaration dropped, not parsed as Left
  PASS();
}

void testValidDeclarationSurvivesAfterOverflowedOne() {
  printf("testValidDeclarationSurvivesAfterOverflowedOne...\n");
  CssParser parser("");
  // The oversized declaration is dropped when ';' flushes it; the following valid
  // declaration in the same block must still apply.
  std::string css = "p { color:";
  css.append(2000, 'z');  // overflow the declaration StackBuffer
  css += "; text-align: right; }";
  loadCssFromString(parser, css.c_str());

  const CssStyle style = parser.resolveStyle("p", "", "");
  ASSERT_TRUE(style.hasTextAlign());
  ASSERT_EQ(style.textAlign, CssTextAlign::Right);
  PASS();
}

// ============================================================================
// main
// ============================================================================

int main() {
  printf("=== CSS Gaps Tests ===\n\n");

  printf("--- Gap 3: list-style-type: none ---\n");
  testListStyleTypeNone();
  testListStyleShorthandNone();
  testListStyleTypeDisc_notNone();

  printf("\n--- Gap 4: page-break ---\n");
  testPageBreakBeforeAlways();
  testPageBreakAfterAlways();
  testBreakBeforePage_css3();
  testBreakAfterPage_css3();
  testPageBreakBeforeAuto_notSet();

  printf("\n--- Gap 2: line-height ---\n");
  testLineHeightUnitless();
  testLineHeightPercent();
  testLineHeightEm();
  testLineHeightSmallerValue();
  testLineHeightLargerValue();
  testLineHeightClampMin();
  testLineHeightClampMax();
  testLineHeightNormal_notSet();

  printf("\n--- Gap 1: font-size multiplier ---\n");
  testFontSizePercent160();
  testFontSizeEm();
  testFontSizeSmaller();

  printf("\n--- font-variant: small-caps ---\n");
  testFontVariantSmallCaps();
  testFontVariantCapsLonghand();
  testFontVariantNormalCancels();
  testFontVariantUnknown_notSet();
  testFontVariantCaseInsensitive();

  printf("\n--- ID selector support (#id, tag#id) ---\n");
  testIdSelectorBasic();
  testIdSelectorNotMatchedOnOtherElement();
  testTagIdSelector();
  testIdCascadeOverClass();
  testIdCascadeOverTag();
  testIdSelectorCaseNormalized();
  testIdSelectorNotAffectUnrelatedElement();
  testIdSelectorGrouped();

  printf("\n--- StackBuffer overflow handling ---\n");
  testOverflowedSelectorGroupDropsUsablePrefix();
  testParserRecoversAfterOverflowedSelector();
  testOverflowedDeclarationNotParsedAsProperty();
  testValidDeclarationSurvivesAfterOverflowedOne();

  printf("\n=== Results: %d passed, %d failed ===\n", testsPassed, testsFailed);
  return testsFailed > 0 ? 1 : 0;
}
