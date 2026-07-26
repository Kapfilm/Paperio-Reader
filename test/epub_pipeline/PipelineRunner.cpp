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
#include "Epub/content/BlockStreamReader.h"
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

bool compileToContentBin(const std::string& epubPath, const std::string& cacheDir, const Profile& profile,
                         std::ostream& out) {
  GfxRenderer renderer;
  auto epub = std::make_shared<Epub>(epubPath, cacheDir);
  if (!epub->load(true)) {
    out << "ERROR load failed\n";
    return false;
  }
  epub->loadImageManifest();
  FootnotePreviews::gather(*epub);

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
  // Stamp the source book's ZIP content fingerprint so a reader can reject a stale content.bin.
  uint64_t fp = 0;
  if (epub->zipContentFingerprint(&fp)) contentSink.content().sourceFingerprint = fp;

  FsFile w;
  if (!w.openForWrite(cacheDir + "/content.bin") || !compiled::writeContentBin(w, contentSink.content())) {
    out << "ERROR content.bin write failed\n";
    return false;
  }
  w.close();
  return true;
}

bool replayFromContentBin(const std::string& epubPath, const std::string& cacheDir, const Profile& profile,
                          std::ostream& out) {
  GfxRenderer renderer;
  // The epub is opened only for image paths / title / spine hrefs — NO createSectionFile (no
  // ZIP/XML/CSS walk). This is the settings-change fast path being measured.
  auto epub = std::make_shared<Epub>(epubPath, cacheDir);
  if (!epub->load(true)) {
    out << "ERROR load failed\n";
    return false;
  }
  epub->loadImageManifest();

  // STREAMING read: open the v5 content.bin with a BlockStreamReader (loads only the small style
  // pool + spine index), and drive LayoutSink one LOGICAL block at a time per spine — never holding
  // a whole spine in RAM. This is the plan-v2 shape (block-streaming). The file is caller-owned.
  FsFile binFile;
  if (!binFile.openForRead(cacheDir + "/content.bin")) {
    out << "ERROR content.bin open failed\n";
    return false;
  }
  compiled::BlockStreamReader reader;
  if (!reader.open(binFile)) {
    out << "ERROR content.bin read failed (bad/stale/corrupt)\n";
    return false;
  }
  // Reject a content.bin that does not match the book on disk (stale cache → recompile). A 0 stored
  // fingerprint means "unset" (anonymous compile) — skip the check then.
  uint64_t bookFp = 0;
  if (reader.fingerprint() != 0 && epub->zipContentFingerprint(&bookFp) && bookFp != reader.fingerprint()) {
    out << "ERROR content.bin fingerprint mismatch (stale cache)\n";
    return false;
  }
  std::vector<compiled::Chapter> chapters;
  reader.readChapters(chapters);

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

  const auto& stylePool = reader.stylePool();
  for (int i = 0; i < epub->getSpineItemsCount(); ++i) {
    std::vector<std::unique_ptr<Page>> pages;
    params.imageBasePath = epub->getCachePath() + "/img_" + std::to_string(i) + "_00000000_";
    compiled::LayoutSink sink(renderer, params,
                              [&pages](std::unique_ptr<Page> page) { pages.push_back(std::move(page)); });

    if (static_cast<uint32_t>(i) < reader.spineCount()) {
      if (!reader.openSpine(static_cast<uint32_t>(i))) {
        out << "SPINE " << i << " ERROR openSpine failed\n";
        return false;
      }
      const auto& anchors = reader.spineAnchors();  // keyed by first-record index of a logical block
      const auto& labels = reader.spineLabels();

      compiled::Block lb;
      while (reader.nextLogicalBlock(lb)) {
        const uint32_t bi = reader.currentFirstRecordIndex();
        // anchors/labels/footnotes/xpath fire BEFORE onBlock; chapters AFTER.
        for (const auto& a : anchors)
          if (a.blockIndex == bi) sink.onAnchor(a.id);
        for (const auto& pl : labels)
          if (pl.blockIndex == bi) sink.onPageBreakLabel(pl.label);
        for (const auto& fn : lb.footnotes) sink.onFootnote(static_cast<int>(fn.wordIndex), fn.entry);
        if (lb.hasXPath) sink.onXPathAdvance(lb.xpath.paragraphIndex, lb.xpath.listItemIndex, lb.xpath.bodyChildByteOffset);
        static const CssStyle kEmptyStyle{};
        const CssStyle& style = (lb.styleId < stylePool.size()) ? stylePool[lb.styleId] : kEmptyStyle;
        sink.onBlock(std::move(lb), style);
        for (const auto& ch : chapters)
          if (ch.spineIndex == static_cast<uint16_t>(i) && ch.blockIndex == bi) sink.onChapter(ch.level, ch.title);
      }
      if (!reader.ok()) {
        out << "SPINE " << i << " ERROR block stream read failed\n";
        return false;
      }
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
  binFile.close();
  return true;
}

bool layoutViaContentBin(const std::string& epubPath, const std::string& cacheDir, const Profile& profile,
                         std::ostream& out) {
  // Full round-trip: Stage-1 compile+serialize, then Stage-2 read-back+layout. The dump comes
  // from the replay stage (STAGE 1 dumps nothing), so it equals a direct parse+layout.
  std::ostringstream compileLog;
  if (!compileToContentBin(epubPath, cacheDir, profile, compileLog)) {
    out << compileLog.str();
    return false;
  }
  return replayFromContentBin(epubPath, cacheDir, profile, out);
}

}  // namespace pipeline_harness
