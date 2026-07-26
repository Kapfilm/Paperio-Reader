#include "PipelineRunner.h"

#include <GfxRenderer.h>

#include <chrono>
#include <iomanip>
#include <memory>
#include <regex>

#include "Epub.h"
#include "Epub/FootnotePreviews.h"
#include "Epub/Page.h"
#include "Epub/Section.h"
#include "Epub/content/CompiledContent.h"
#include "Epub/content/ContentSink.h"
#include "Epub/content/LayoutSink.h"

namespace pipeline_harness {
namespace {

// Image paths live under the (run-specific) cache dir; strip that prefix so
// dumps compare across runs and machines. The per-book cache subdir is named
// epub_<hash-of-absolute-path>, which differs by checkout location (e.g. a dev's
// /home/... vs CI's /home/runner/work/...), so canonicalize that hash too —
// otherwise image-bearing goldens are machine-specific and fail in CI.
std::string normalizePath(const std::string& path, const std::string& cacheDir) {
  std::string p = (path.rfind(cacheDir, 0) == 0) ? "<cache>" + path.substr(cacheDir.size()) : path;
  p = std::regex_replace(p, std::regex("epub_[0-9]+"), "epub_<hash>");
  // The image cache basename is img_<spine>_<propertyHash>_<counter>.<ext>. propertyHash is
  // settings-derived; canonicalize it so the dump compares on image IDENTITY (spine, counter,
  // extension) rather than the settings hash — this lets LayoutSink (which does not recompute
  // the section propertyHash) match the fused dump while still asserting the right image.
  return std::regex_replace(p, std::regex("img_([0-9]+)_[0-9a-f]{8}_"), "img_$1_<hash>_");
}

void dumpTextLine(std::ostream& out, const PageLine& line) {
  const auto& block = *line.getBlock();
  const auto& bs = block.getBlockStyle();
  out << "  LINE y=" << line.yPos << " x=" << line.xPos << " align=" << static_cast<int>(bs.alignment)
      << " mult=" << std::fixed << std::setprecision(3) << bs.fontSizeMultiplier << " hfid=" << bs.headingFontId
      << " words=" << block.wordCount() << "\n";
  for (uint16_t w = 0; w < block.wordCount(); ++w) {
    out << "   W x=" << block.wordXpos(w) << " s=" << static_cast<int>(block.wordStyle(w))
        << " z=" << static_cast<int>(block.wordSizePct(w)) << " t=" << block.wordText(w) << "\n";
  }
}

}  // namespace

void dumpOnePage(std::ostream& out, const Page& page, const uint16_t pageIndex, const std::string& cacheDir) {
  out << " PAGE " << pageIndex << " elements=" << page.elements.size() << " footnotes=" << page.footnotes.size()
      << "\n";
  for (const auto& el : page.elements) {
    switch (el->getTag()) {
      case TAG_PageLine:
        dumpTextLine(out, static_cast<const PageLine&>(*el));
        break;
      case TAG_PageImage: {
        const auto& img = static_cast<const PageImage&>(*el);
        const auto& ib = img.getImageBlock();
        out << "  IMG y=" << img.yPos << " x=" << img.xPos << " w=" << ib.getWidth() << " h=" << ib.getRenderedHeight()
            << " src=" << normalizePath(ib.getImagePath(), cacheDir) << "\n";
        break;
      }
      case TAG_PageTable: {
        const auto& tbl = static_cast<const PageTableFragment&>(*el);
        out << "  TABLE y=" << tbl.yPos << " x=" << tbl.xPos << " h=" << tbl.getTotalHeight() << "\n";
        break;
      }
      case TAG_PageHR:
        out << "  HR y=" << el->yPos << " x=" << el->xPos << "\n";
        break;
    }
  }
  for (const auto& fn : page.footnotes) {
    out << "  FN n=" << fn.number << " href=" << fn.href << "\n";
  }
}

bool runAndDump(const std::string& epubPath, const std::string& cacheDir, const Profile& profile, std::ostream& out,
                const SpineStatFn& spineStat) {
  GfxRenderer renderer;

  auto epub = std::make_shared<Epub>(epubPath, cacheDir);
  if (!epub->load(true)) {
    out << "ERROR load failed\n";
    return false;
  }
  epub->loadImageManifest();
  FootnotePreviews::gather(*epub);

  out << "BOOK title=" << epub->getTitle() << " lang=" << epub->getLanguage() << " spine=" << epub->getSpineItemsCount()
      << " toc=" << epub->getTocItemsCount() << " reliableToc=" << (epub->hasReliableToc() ? 1 : 0) << "\n";

  for (int i = 0; i < epub->getSpineItemsCount(); ++i) {
    const auto spineStart = std::chrono::steady_clock::now();
    Section section(epub, i, renderer);
    if (!section.createSectionFile(profile.fontId, profile.lineCompression, profile.extraParagraphSpacing,
                                   profile.paragraphAlignment, profile.viewportWidth, profile.viewportHeight,
                                   profile.hyphenationEnabled, profile.embeddedStyle, profile.bionicReadingEnabled,
                                   profile.inlineFootnotePreviews, profile.imageRendering, {}, /*skipEviction=*/true,
                                   {})) {
      out << "SPINE " << i << " ERROR build failed\n";
      return false;
    }
    if (!section.loadSectionFile(profile.fontId, profile.lineCompression, profile.extraParagraphSpacing,
                                 profile.paragraphAlignment, profile.viewportWidth, profile.viewportHeight,
                                 profile.hyphenationEnabled, profile.embeddedStyle, profile.bionicReadingEnabled,
                                 profile.inlineFootnotePreviews, profile.imageRendering)) {
      out << "SPINE " << i << " ERROR load failed\n";
      return false;
    }
    out << "SPINE " << i << " href=" << epub->getSpineItem(i).href << " pages=" << section.pageCount
        << " truncated=" << (section.isTruncatedCache() ? 1 : 0)
        << " cssFallback=" << (section.isEmbeddedStyleFallback() ? 1 : 0) << "\n";
    for (uint16_t p = 0; p < section.pageCount; ++p) {
      section.currentPage = p;
      const auto page = section.loadPageFromSectionFile();
      if (!page) {
        out << " PAGE " << p << " ERROR load failed\n";
        return false;
      }
      dumpOnePage(out, *page, p, cacheDir);
    }
    if (spineStat) {
      const auto us =
          std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - spineStart);
      spineStat(i, section.pageCount, us.count());
    }
  }
  return true;
}

bool compileContent(const std::string& epubPath, const std::string& cacheDir, const Profile& profile,
                    compiled::ContentSink& sink, std::ostream& out) {
  GfxRenderer renderer;

  auto epub = std::make_shared<Epub>(epubPath, cacheDir);
  if (!epub->load(true)) {
    out << "ERROR load failed\n";
    return false;
  }
  epub->loadImageManifest();
  // Gather book-level footnote previews (footnotes.bin) so the inline-footnote-preview path is
  // exercised in the content compile too, matching runAndDump/layoutViaSink. Without this the
  // preview lookup is empty and no preview is ever injected.
  FootnotePreviews::gather(*epub);

  for (int i = 0; i < epub->getSpineItemsCount(); ++i) {
    sink.beginSpine();
    Section section(epub, i, renderer);
    section.setStage1Sink(&sink);
    if (!section.createSectionFile(profile.fontId, profile.lineCompression, profile.extraParagraphSpacing,
                                   profile.paragraphAlignment, profile.viewportWidth, profile.viewportHeight,
                                   profile.hyphenationEnabled, profile.embeddedStyle, profile.bionicReadingEnabled,
                                   profile.inlineFootnotePreviews, profile.imageRendering, {}, /*skipEviction=*/true,
                                   {})) {
      out << "SPINE " << i << " ERROR build failed\n";
      return false;
    }
  }
  return true;
}

bool layoutViaSink(const std::string& epubPath, const std::string& cacheDir, const Profile& profile,
                   std::ostream& out) {
  GfxRenderer renderer;

  auto epub = std::make_shared<Epub>(epubPath, cacheDir);
  if (!epub->load(true)) {
    out << "ERROR load failed\n";
    return false;
  }
  epub->loadImageManifest();
  FootnotePreviews::gather(*epub);

  out << "BOOK title=" << epub->getTitle() << " lang=" << epub->getLanguage() << " spine=" << epub->getSpineItemsCount()
      << " toc=" << epub->getTocItemsCount() << " reliableToc=" << (epub->hasReliableToc() ? 1 : 0) << "\n";

  compiled::LayoutParams params;
  params.fontId = profile.fontId;
  params.lineCompression = profile.lineCompression;
  params.extraParagraphSpacing = profile.extraParagraphSpacing;
  params.paragraphAlignment = profile.paragraphAlignment;
  params.viewportWidth = profile.viewportWidth;
  params.viewportHeight = profile.viewportHeight;
  params.hyphenationEnabled = profile.hyphenationEnabled;
  params.bionicReadingEnabled = profile.bionicReadingEnabled;
  params.embeddedStyle = profile.embeddedStyle;
  // Empty ladder: matches the {} the PipelineRunner passes to createSectionFile, so
  // resolveBlockFont takes the same scale-only fallback on both sides of the diff.

  params.epubFilePath = epub->getPath();

  for (int i = 0; i < epub->getSpineItemsCount(); ++i) {
    std::vector<std::unique_ptr<Page>> pages;
    // Cache-path prefix in the fused shape img_<spine>_<hash>_; the propertyHash is canonicalized
    // away by the dump's normalizePath, so a placeholder hash is fine — only spine + counter +
    // ext are compared.
    params.imageBasePath = epub->getCachePath() + "/img_" + std::to_string(i) + "_00000000_";
    compiled::LayoutSink sink(renderer, params,
                              [&pages](std::unique_ptr<Page> page) { pages.push_back(std::move(page)); });

    Section section(epub, i, renderer);
    section.setStage1Sink(&sink);
    // The fused build still writes its own section cache to disk; we ignore it and dump the
    // LayoutSink's Page stream instead. The producer drives BOTH in one walk.
    if (!section.createSectionFile(profile.fontId, profile.lineCompression, profile.extraParagraphSpacing,
                                   profile.paragraphAlignment, profile.viewportWidth, profile.viewportHeight,
                                   profile.hyphenationEnabled, profile.embeddedStyle, profile.bionicReadingEnabled,
                                   profile.inlineFootnotePreviews, profile.imageRendering, {}, /*skipEviction=*/true,
                                   {})) {
      out << "SPINE " << i << " ERROR build failed\n";
      return false;
    }
    // Match runAndDump's SPINE line exactly. truncated/cssFallback are section-cache
    // properties with no LayoutSink analogue; emit the same 0/0 the text corpus produces so
    // the dumps line up (any book that actually truncates or falls back is out of the
    // text-corpus subset and handled when those paths are covered).
    out << "SPINE " << i << " href=" << epub->getSpineItem(i).href << " pages=" << pages.size()
        << " truncated=0 cssFallback=0\n";
    // LUT invariant: exactly one paragraph-LUT entry per emitted page (Section enforces this hard
    // check, cpp:1059). Emit a marker ONLY on violation — the fused dump never contains it, so any
    // mismatch fails the equivalence EXPECT_EQ. Proves onXPathAdvance -> emitPage stays in lockstep.
    if (sink.paragraphLutPerPage().size() != pages.size()) {
      out << "  [LUT-INVARIANT-FAIL lut=" << sink.paragraphLutPerPage().size() << " pages=" << pages.size() << "]\n";
    }
    for (uint16_t p = 0; p < pages.size(); ++p) {
      dumpOnePage(out, *pages[p], p, cacheDir);
    }
  }
  return true;
}

bool layoutViaContentBin(const std::string& epubPath, const std::string& cacheDir, const Profile& profile,
                         std::ostream& out) {
  GfxRenderer renderer;

  auto epub = std::make_shared<Epub>(epubPath, cacheDir);
  if (!epub->load(true)) {
    out << "ERROR load failed\n";
    return false;
  }
  epub->loadImageManifest();
  FootnotePreviews::gather(*epub);

  // STAGE 1: compile the whole book into a ContentSink, serialize to content.bin, read it back.
  compiled::ContentSink contentSink;
  for (int i = 0; i < epub->getSpineItemsCount(); ++i) {
    contentSink.beginSpine();
    Section section(epub, i, renderer);
    section.setStage1Sink(&contentSink);
    if (!section.createSectionFile(profile.fontId, profile.lineCompression, profile.extraParagraphSpacing,
                                   profile.paragraphAlignment, profile.viewportWidth, profile.viewportHeight,
                                   profile.hyphenationEnabled, profile.embeddedStyle, profile.bionicReadingEnabled,
                                   profile.inlineFootnotePreviews, profile.imageRendering, {}, /*skipEviction=*/true,
                                   {})) {
      out << "SPINE " << i << " ERROR stage-1 build failed\n";
      return false;
    }
  }
  const std::string binPath = cacheDir + "/content.bin";
  {
    FsFile w;
    if (!w.openForWrite(binPath) || !compiled::writeContentBin(w, contentSink.content())) {
      out << "ERROR content.bin write failed\n";
      return false;
    }
    w.close();
  }
  compiled::CompiledContent content;
  {
    FsFile r;
    if (!r.openForRead(binPath) || !compiled::readContentBin(r, content)) {
      out << "ERROR content.bin read failed\n";
      return false;
    }
    r.close();
  }

  // STAGE 2: replay the read-back CompiledContent through a LayoutSink — NO ZIP/XML/CSS. The dump
  // must be byte-identical to layoutViaSink (which parses). This is the settings-change fast path.
  out << "BOOK title=" << epub->getTitle() << " lang=" << epub->getLanguage() << " spine=" << epub->getSpineItemsCount()
      << " toc=" << epub->getTocItemsCount() << " reliableToc=" << (epub->hasReliableToc() ? 1 : 0) << "\n";

  compiled::LayoutParams params;
  params.fontId = profile.fontId;
  params.lineCompression = profile.lineCompression;
  params.extraParagraphSpacing = profile.extraParagraphSpacing;
  params.paragraphAlignment = profile.paragraphAlignment;
  params.viewportWidth = profile.viewportWidth;
  params.viewportHeight = profile.viewportHeight;
  params.hyphenationEnabled = profile.hyphenationEnabled;
  params.bionicReadingEnabled = profile.bionicReadingEnabled;
  params.embeddedStyle = profile.embeddedStyle;
  params.epubFilePath = epub->getPath();

  for (int i = 0; i < epub->getSpineItemsCount(); ++i) {
    std::vector<std::unique_ptr<Page>> pages;
    params.imageBasePath = epub->getCachePath() + "/img_" + std::to_string(i) + "_00000000_";
    compiled::LayoutSink sink(renderer, params,
                              [&pages](std::unique_ptr<Page> page) { pages.push_back(std::move(page)); });

    const compiled::SpineContent& spine =
        (static_cast<size_t>(i) < content.spines.size()) ? content.spines[i] : compiled::SpineContent{};

    // Coalesce 8 KB split records back into logical blocks. The ContentSink writer splits an
    // oversized TEXT block into kContinuation records (a read-time memory bound); the direct
    // layout path (layoutViaSink) never splits — it gets ONE onBlock per logical block and does
    // its OWN >96-word intermediate flush inside a single ParsedText. To match byte-for-byte,
    // Stage-2 must reassemble the logical block before laying it out. We rebuild a mapping from
    // the ORIGINAL (pre-split) block index — the one anchors/labels/footnotes/xpath reference — to
    // the coalesced block, un-rebasing continuation footnote word indices back to the whole block.
    struct LogicalBlock {
      compiled::Block block;      // the merged block (first record + appended continuations)
      uint32_t firstRecordIndex;  // index in spine.blocks of this logical block's first record
    };
    std::vector<LogicalBlock> logical;
    for (uint32_t bi = 0; bi < spine.blocks.size(); ++bi) {
      const compiled::Block& rec = spine.blocks[bi];
      const bool isCont = (rec.flags & compiled::kContinuation) != 0 && rec.type == compiled::BlockType::Text;
      if (isCont && !logical.empty()) {
        compiled::Block& merged = logical.back().block;
        const uint32_t wordBase = static_cast<uint32_t>(merged.words.size());
        const uint32_t textBase = static_cast<uint32_t>(merged.text.size());
        for (compiled::Word w : rec.words) {
          w.textOff += textBase;
          merged.words.push_back(w);
        }
        merged.text += rec.text;
        for (compiled::FootnoteRef fn : rec.footnotes) {
          fn.wordIndex += wordBase;
          merged.footnotes.push_back(fn);
        }
        // preview runs on a continuation record (rare) shift by the word base too.
        for (compiled::PreviewRun pr : rec.footnotePreviews) {
          pr.startWord += wordBase;
          merged.footnotePreviews.push_back(pr);
        }
      } else {
        logical.push_back({rec, bi});
      }
    }

    // Reconstruct the walk's BlockSink call order per logical block. anchors/labels/footnotes/xpath
    // fire BEFORE onBlock (accumulated during the block's build); chapters fire AFTER onBlock.
    for (const LogicalBlock& lb : logical) {
      const uint32_t bi = lb.firstRecordIndex;
      for (const auto& a : spine.anchors)
        if (a.blockIndex == bi) sink.onAnchor(a.id);
      for (const auto& pl : spine.pageBreakLabels)
        if (pl.blockIndex == bi) sink.onPageBreakLabel(pl.label);
      for (const auto& fn : lb.block.footnotes) sink.onFootnote(static_cast<int>(fn.wordIndex), fn.entry);
      if (lb.block.hasXPath)
        sink.onXPathAdvance(lb.block.xpath.paragraphIndex, lb.block.xpath.listItemIndex,
                            lb.block.xpath.bodyChildByteOffset);
      compiled::Block copy = lb.block;  // LayoutSink takes ownership; keep `content` intact
      static const CssStyle kEmptyStyle{};
      const CssStyle& style =
          (lb.block.styleId < content.stylePool.size()) ? content.stylePool[lb.block.styleId] : kEmptyStyle;
      sink.onBlock(std::move(copy), style);
      for (const auto& ch : content.chapters)
        if (ch.spineIndex == static_cast<uint16_t>(i) && ch.blockIndex == bi) sink.onChapter(ch.level, ch.title);
    }
    sink.onSpineEnd();

    out << "SPINE " << i << " href=" << epub->getSpineItem(i).href << " pages=" << pages.size()
        << " truncated=0 cssFallback=0\n";
    if (sink.paragraphLutPerPage().size() != pages.size()) {
      out << "  [LUT-INVARIANT-FAIL lut=" << sink.paragraphLutPerPage().size() << " pages=" << pages.size() << "]\n";
    }
    for (uint16_t p = 0; p < pages.size(); ++p) {
      dumpOnePage(out, *pages[p], p, cacheDir);
    }
  }
  return true;
}

}  // namespace pipeline_harness
