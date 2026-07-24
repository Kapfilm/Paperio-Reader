# Stage-1 design review (2026-07-24) — steps 1→6a vs original intent

A checkpoint re-evaluation of every design choice against the original intent
(`compiled-book-pipeline-plan.md`, `stage1-extraction-design.md`,
`compiled-content-format.md`, `parser-stage1-handover.md`). Grounded in the docs +
the as-built code, with the two highest-severity claims verified against the source.

## Big picture

The plan's payoff is: **Stage-2 reads `content.bin` → no ZIP/XML/CSS on relayout → ≥3× faster
settings-change relayout + lower Stage-2 memory** (master plan §Goal, §Equivalence table, Phase 3
§Speed/§Memory). What steps 1–6a built is the **producer + a proof that LayoutSink ≡ the fused
layout** — i.e. master-plan Phase 3 *step 2* only. Steps 3–6 (Stage-2 actually reading content.bin,
footnote scan, retiring `images.bin`/`html_<spine>.bin`) are still ahead. **No speed/memory benefit
has landed yet**; the device still parses XHTML+CSS on every relayout. This is per the plan's
sequencing, but worth stating: the work so far is infrastructure; the payoff is still in front of us.

## Verified bugs (fix before 6b)

1. **String-cap vs split-cap inconsistency (correctness).** `MAX_STRING_LENGTH=4096`
   (`Serialization.h:39`), but the 8 KB split (`kMaxSerializedBody=8192`, `ContentSink.h:44`) bounds
   the *serialized body* (per-word `textOff`+overhead + text), not the raw `text`. Worst case
   `text ≈ 0.93 × body` (long words: `add = 7 + wordBytes`), so `text` can reach ~7600 bytes < 8192
   serialized but > 4096. Such a block **writes fine but `readString` rejects it on readback**
   → `readContentBin` fails. Latent: the synthetic corpus never produces it; a real book with a long
   paragraph would. **Fix:** cap the split so `text.size() <= MAX_STRING_LENGTH` (tighten the body
   budget, or raise MAX_STRING_LENGTH for this field), + a test with a long-word block.

2. **Settings-independence violation (invariant; introduced commit 5.2).** Two folds bake px into the
   transmitted `CssStyle`: poem `textIndent = CssLength(marginPx)` (`ChapterHtmlSlimParser.cpp:1919`)
   and `<li> marginLeft = CssLength(blockStyle.marginLeft)` (`:1643`), where both px values depend on
   `emSize` (font) and `viewportWidth` (settings). content.bin (written settings-independent per the
   WBC1 spec's core rule "anything in px stays in Stage-2") would carry a settings-dependent value,
   wrong if re-read at a different font/viewport. Harmless today (Stage-2 doesn't read content.bin
   yet). **Fix (stays byte-identical):** transmit the original em `CssLength` (poem → `cssStyle.
   marginLeft`; `<li>` → `CssLength(1.5*depth, CssUnit::Em)`), let the sink resolve em→px.

3. **Fragile serialization dispatch.** Writer (`CompiledContent.cpp:235`) + reader (`:338`) handle
   Text/Table/Hr then treat **everything else as Image** via a bare `else`. A future `BlockType`
   silently deserializes as an Image and misaligns the stream. **Fix:** explicit `case Image` +
   `default: return false` on read.

## Architectural smells / debt (weigh before Stage-2-reads-content.bin makes the format load-bearing)

- **No format forward-compat runway.** WBC1 is rigid positional (no TLV/length-prefixed records);
  any `CssStyle` field addition breaks every old file. Acceptable *if* content.bin stays a pure
  recompile-on-version-bump cache (the plan's intent), but there's zero evolution runway.
- **`styleEquals`/`packDefined`/`writeStyle`/`readStyle` lockstep across 5 sites** with no
  compile-time guard; float `==` in dedup. Maintenance hazard — adding a CssStyle field silently
  breaks dedup or serialization if any site is missed.
- **Flags byte mixes concerns:** `kContinuation` (serialization artifact), `kFromBrElement`
  (producer replay hint), `kPageBreakBefore/After` (duplicate pooled `CssStyle::pageBreakBefore/
  After`). Consider moving `kContinuation` to record framing and dropping the duplicated page-break
  bits (read from the pooled style instead).
- **Empty-block transcript leaks the producer's margin-merge algorithm** into every Stage-2 consumer
  (`LayoutSink.h:148-151` + `kFromBrElement`). Deliberate — it's how step 5 reached byte-identity
  with the fused path — but anti-normalization vs a clean, independently-consumable content format.
  A future independent Stage-2 must reimplement empty-block reuse byte-for-byte.
- **`BlockSink` has two layout-only methods** (`onXPathAdvance`, `onFootnote`) that `ContentSink`
  ignores — a content seam with layout concerns bolted on. Defensible (both are content-derived) but
  not clean.
- **Redundant encodings:** `Word::textOff` (u32/word) vs the NUL-delimited `text` (the split even
  recomputes boundaries from NUL); `Block::charOffset` vs a derivable running sum; `inlineImageSide`
  and `Image::floatSide` vs `CssFloat` in the pooled style. `Image::floatSide` is structurally always
  0 (floats ride on the following Text block).
- **Dead payload on disk:** `Word::bidiLevel` (always 0), direction flag bits, `Image::floatSide`
  (always 0). Provisioned for RTL per the format spec's explicit deferral, so intended, but shipping
  bytes for unimplemented features.

## Process divergences from the plan

- **Only the synthetic corpus is in the equivalence gate.** The plan names real books (Moby Dick,
  a filepos-footnote book, the 123 KB-CSS book, a huge-single-spine book, an image-heavy book, a
  non-Latin book) as part of *the critical gate*. Not run. Directly connects to bug #1 (real books
  are where the long-paragraph readback failure surfaces). `test/fixtures/moby-dick[.epub]` exists.
- **Speed & Memory gates never measured for Phase 3.** Only Functionality (goldens) has been checked;
  the plan requires all three gates per phase, and `pipeline-baseline-<date>.md` updated per phase
  (last update is Phase 2, 2026-07-19). Reasonable to defer speed/memory until Stage-2-reads-
  content.bin exists (there's no relayout speedup to measure before then), but it should be an
  explicit deferral, not an omission.
- **`content.bin` storage budget (≤1.5× EPUB) never measured.**

## What we got right (kept faith with intent)

- Byte-identical goldens held as the gate through every commit; no whitelisted diffs.
- Settings-split rule (validated vs microreader) applied consistently — *except* the two px folds
  (bug #2), which this review fixes.
- `ImageLayout` (pure function) is a genuinely clean abstraction; `TableLayout` reasonable. Both are
  the natural step-6 seams.
- Additive / `EPUB_STAGE1` / verify-first-delete-last safety method held throughout.
- The producer path itself is renderer-free; the walk's renderer uses are shared-with-fused-layout
  (to be severed in 6d), not in the producer.

## Chosen action (2026-07-24)

Fix the 3 verified bugs + add a real-book (Moby Dick) equivalence test, each a separate commit, THEN
resume step 6b. The architectural-debt items above are logged here for decision *before* the
Stage-2-reads-content.bin step makes WBC1 load-bearing — they do not block 6b.
