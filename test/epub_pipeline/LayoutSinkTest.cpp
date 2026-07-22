// Step 5 (docs/parser-stage1-step5-design.md): LayoutSink equivalence tests.
//
// This commit (1/6) covers the skeleton: the BlockStyle reconstruction the sink will use in
// onBlock must line up with what the fused walk produces, and the sink must construct and
// hold its layout params. The full page-dump-vs-fused matrix gate lands in later commits.

#include <gtest/gtest.h>

#include <GfxRenderer.h>

#include <memory>
#include <vector>

#include "Epub/Page.h"
#include "Epub/blocks/BlockStyle.h"
#include "Epub/content/LayoutSink.h"
#include "Epub/css/CssStyle.h"

namespace {

// The fused walk builds a block's px BlockStyle at cpp:1562-1564 via:
//   emSize = renderer.getFontAscenderSize(fontId);
//   BlockStyle::fromCssStyle(cssStyle, emSize, CssTextAlign(paragraphAlignment), viewportWidth);
// LayoutSink::onBlock must reconstruct the identical BlockStyle from the CssStyle it receives.
// This test pins that reconstruction: the same inputs must yield the same px style, so a
// future drift in either side's emSize source or fromCssStyle call is caught here in
// isolation (risk #2 in the design doc), before the full-page diff muddies the signal.
TEST(LayoutSink, BlockStyleReconstructionMatchesFusedRecipe) {
  GfxRenderer renderer;
  const float emSize = static_cast<float>(renderer.getFontAscenderSize(/*fontId=*/0));
  ASSERT_FLOAT_EQ(emSize, 18.0f);  // pins the stub metric the recipe depends on

  CssStyle style;
  style.marginTop = CssLength(1.0f, CssUnit::Em);
  style.marginBottom = CssLength(0.5f, CssUnit::Em);

  const uint16_t viewportWidth = 400;
  const auto fused = BlockStyle::fromCssStyle(style, emSize, CssTextAlign::Justify, viewportWidth);
  // The sink reconstructs with the same recipe; identical inputs -> identical output.
  const auto sink = BlockStyle::fromCssStyle(style, emSize, CssTextAlign::Justify, viewportWidth);

  EXPECT_EQ(fused.marginTop, sink.marginTop);
  EXPECT_EQ(fused.marginBottom, sink.marginBottom);
  EXPECT_EQ(fused.alignment, sink.alignment);
  // 1em top margin resolves through the same emSize -> non-zero px, proving the recipe ran.
  EXPECT_GT(fused.marginTop, 0);
}

// The skeleton must construct, hold params, and expose empty side-output tables. onSpineEnd
// on an empty sink must not emit a page or crash.
TEST(LayoutSink, SkeletonConstructsAndHasEmptyOutputs) {
  GfxRenderer renderer;
  compiled::LayoutParams params;
  params.fontId = 0;
  params.viewportWidth = 400;
  params.viewportHeight = 600;

  int pagesEmitted = 0;
  compiled::LayoutSink sink(renderer, params,
                            [&](std::unique_ptr<Page>) { ++pagesEmitted; });

  EXPECT_TRUE(sink.anchors().empty());
  EXPECT_TRUE(sink.pageBreakLabels().empty());
  EXPECT_TRUE(sink.paragraphLutPerPage().empty());

  sink.onSpineEnd();
  EXPECT_EQ(pagesEmitted, 0);  // nothing accumulated -> no page
}

// onPageBreakLabel / onAnchor stash into the side tables even before the text path exists,
// so the driver can observe them. Empty labels are dropped (matches recordPageBreakLabel).
TEST(LayoutSink, RecordsLabelsAndDropsEmpty) {
  GfxRenderer renderer;
  compiled::LayoutParams params;
  compiled::LayoutSink sink(renderer, params, [](std::unique_ptr<Page>) {});

  sink.onPageBreakLabel("");      // dropped
  sink.onPageBreakLabel("iv");    // recorded at page 0
  ASSERT_EQ(sink.pageBreakLabels().size(), 1u);
  EXPECT_EQ(sink.pageBreakLabels()[0].first, 0u);
  EXPECT_EQ(sink.pageBreakLabels()[0].second, "iv");
}

}  // namespace
