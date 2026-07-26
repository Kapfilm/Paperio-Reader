#include "TableLayout.h"

#include <utility>

namespace compiled {

// Greedily pack pre-wrapped table rows into PageTableFragments, page-breaking between rows so a
// fragment never overflows the viewport (over-tall single rows fall back via ctx.onOversizeRow).
void packTableFragments(const std::vector<TableLayoutRow>& rows, const uint16_t totalWidth,
                        const uint16_t viewportHeight, const bool hasBorder, TablePageContext& ctx) {
  ctx.ensurePage();

  std::vector<::TableRow> fragmentRows;
  uint16_t fragmentHeight = 0;
  uint8_t fragmentCols = 0;

  auto emitFragment = [&]() {
    if (fragmentRows.empty()) return;
    // Bordered: outer drawRect covers top+bottom; inter-row separators (+1/row) are already in
    // fragmentHeight; add 1 for the bottom border. Borderless: fragmentHeight is exact.
    const uint16_t fragTotalHeight =
        hasBorder ? static_cast<uint16_t>(fragmentHeight + 1) : static_cast<uint16_t>(fragmentHeight);
    if (ctx.currentY() + fragTotalHeight > viewportHeight && ctx.currentY() > 0) {
      ctx.emitPageAndReset();
    }
    ctx.pushFragment(fragmentCols, totalWidth, fragTotalHeight, std::move(fragmentRows),
                     static_cast<int16_t>(ctx.currentY()), hasBorder);
    ctx.advanceY(fragTotalHeight);
    fragmentRows.clear();
    fragmentHeight = 0;
    fragmentCols = 0;
  };

  for (const TableLayoutRow& lr : rows) {
    if (lr.height > viewportHeight) {
      emitFragment();
      ctx.onOversizeRow(lr);
      continue;
    }

    // A change in column count requires a new fragment.
    if (!fragmentRows.empty() && lr.renderCols != fragmentCols) {
      emitFragment();
    }
    if (fragmentCols == 0) fragmentCols = lr.renderCols;

    const uint16_t rowContrib = hasBorder ? static_cast<uint16_t>(lr.height + 1) : lr.height;
    if (!fragmentRows.empty() && ctx.currentY() + fragmentHeight + rowContrib > viewportHeight) {
      emitFragment();
      fragmentCols = lr.renderCols;
    }

    ::TableRow tr;
    tr.isHeaderRow = lr.isHeaderRow;
    tr.height = lr.height;
    tr.cells = lr.cells;  // copy: the caller keeps ownership of the source rows
    fragmentRows.push_back(std::move(tr));
    fragmentHeight += rowContrib;
  }

  emitFragment();
}

}  // namespace compiled
