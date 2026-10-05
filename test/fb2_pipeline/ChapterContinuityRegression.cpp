#include <Fb2.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <Epub/Section.h>
#include <Epub/Page.h>
#include <Fb2AnchorIndex.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
std::string anchor(const std::string& id) {
  uint64_t hash = 14695981039346656037ull;
  for (unsigned char c : id) { hash ^= c; hash *= 1099511628211ull; }
  char result[32]; snprintf(result, sizeof(result), "fb2-%016llx", static_cast<unsigned long long>(hash));
  return result;
}
std::string pageFingerprint(Section& section) {
  std::ostringstream out;
  for(int p=0;p<section.pageCount;++p) {
    section.currentPage=p; auto page=section.loadPageFromSectionFile(); require(bool(page),"fingerprint page");
    out << p << ':' << page->elements.size() << ';';
    for(const auto& el:page->elements) {
      out << int(el->getTag()) << ',' << el->xPos << ',' << el->yPos << ':';
      if(el->getTag()==TAG_PageLine) {
        const auto& b=*static_cast<const PageLine&>(*el).getBlock();
        for(uint16_t w=0;w<b.wordCount();++w) out << b.wordText(w) << '@' << b.wordXpos(w) << ',';
      }
    }
    for(const auto& f:page->footnotes) out << f.number << '=' << f.href << ';';
  }
  return out.str();
}
void fixture(const std::filesystem::path& path) {
  std::ofstream out(path);
  out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?><FictionBook xmlns:l=\"http://www.w3.org/1999/xlink\"><description><title-info><book-title>Continuous chapter regression</book-title><lang>en</lang></title-info></description><body><section id=\"whole\"><title><p>One real chapter</p></title>";
  for (int i=0; i<1200; ++i) {
    out << "<p id=\"p" << i << "\">TOKEN" << i << " This paragraph must remain in its original chapter with continuous pagination. The next paragraph continues without a synthetic section boundary. Continued reading preserves the chapter page count.</p>";
    if (i==100) out << "<p><a l:href=\"#p1100\">LATE_TARGET</a></p>";
    if (i==1100) out << "<p><a type=\"note\" l:href=\"#note1\">NOTE_LINK</a></p>";
  }
  out << "</section><section id=\"pictures\"><title><p>Images chapter</p></title>";
  for(int i=0;i<4;++i) out << "<p>Picture " << i << "</p><image l:href=\"#img" << i << "\"/>";
  out << "</section></body><body name=\"notes\"><section id=\"note1\"><title><p>Note</p></title><p>Note text with return <a l:href=\"#p1100\">RETURN_TARGET</a></p></section></body>";
  // Four distinct image IDs deliberately exceed the old two-image split threshold.
  for(int i=0;i<4;++i) out << "<binary id=\"img" << i << "\" content-type=\"image/png\">iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jT1kAAAAASUVORK5CYII=</binary>";
  out << "</FictionBook>";
  require(bool(out), "write fixture");
}
}
int main(int argc, char** argv) {
  try {
    require(argc==2 || argc==3, "usage: fb2_chapter_continuity NEW_CACHE_ROOT [existing-source.fb2.zip]");
    const std::filesystem::path root(argv[1]);
    require(!std::filesystem::exists(root), "cache root must not exist");
    std::filesystem::create_directories(root);
    const std::string source = argc==3 ? argv[2] : (root/"chapter.fb2").string();
    if(argc==2) fixture(source);
    Fb2 book(source, root.string()); require(book.load(), "prepare FB2");
    auto epub = std::make_shared<Epub>(book.getPackagePath(), root.string());
    require(epub->load(true), "open converted book");
    require(epub->getSpineItemsCount()==3, "expected 3 real sections, found " + std::to_string(epub->getSpineItemsCount()));
    require(epub->getTocItemsCount()==2, "expected 2 body TOC entries (notes excluded), found " + std::to_string(epub->getTocItemsCount()));
    uint8_t version=0;
    { std::ifstream index(book.getCachePath()+"/.fb2_sections.bin",std::ios::binary); index.seekg(6); index.read(reinterpret_cast<char*>(&version),1); }
    Fb2AnchorIndex ids;
    require(ids.open(book.getCachePath()+"/.fb2_sections.bin",book.getCachePath()+"/.fb2_anchors.bin",version), "open ID index");
    require(ids.chapterForId("p1100")==0 && ids.chapterForId("note1")==2, "late text and note IDs remain in real sections");
    GfxRenderer renderer;
    Section::BuildParams params;
    params.fontId=1; params.viewportWidth=460; params.viewportHeight=760;
    epub->loadImageManifest();
    Section chapter(epub,0,renderer);
    require(chapter.loadSectionFile(params) || (chapter.createSectionFile(params,{},true) && chapter.loadSectionFile(params)), "build full chapter pages");
    require(!chapter.isTruncatedCache() && chapter.pageCount>30, "full long chapter must exceed 30 pages without truncation");
    std::set<int> paragraphs;
    int latePage=-1; bool hasNoteLink=false, hasLateLink=false;
    for(int p=0;p<chapter.pageCount;++p) {
      chapter.currentPage=p; auto page=chapter.loadPageFromSectionFile(); require(bool(page), "read page");
      int lastY=0;
      for(const auto& element:page->elements) {
        lastY=std::max(lastY,static_cast<int>(element->yPos));
        if(element->getTag()!=TAG_PageLine) continue;
        const auto& block=*static_cast<const PageLine&>(*element).getBlock();
        for(uint16_t w=0;w<block.wordCount();++w) {
          std::string word=block.wordText(w);
          if(word.rfind("TOKEN",0)==0) { int n=std::stoi(word.substr(5)); paragraphs.insert(n); if(n==1100) latePage=p; }
        }
      }
      // A continuous prose chapter should fill every non-final page. This detects
      // intermediate fragment flushes even if fragments later share a TOC label.
      require(p==0 || p+1==chapter.pageCount || lastY>600, "premature page flush at page " + std::to_string(p) + " lastY=" + std::to_string(lastY));
      for(const auto& link:page->footnotes) {
        if(std::string(link.href).find(anchor("note1"))!=std::string::npos) hasNoteLink=true;
        if(std::string(link.href).find(anchor("p1100"))!=std::string::npos) hasLateLink=true;
      }
    }
    require(paragraphs.size()==1200, "all 1200 paragraph markers survive pagination");
    require(hasNoteLink && hasLateLink, "both late in-chapter target and external note links survive");
    require(latePage>30 && chapter.getPageForAnchor(anchor("p1100"))==latePage, "late anchor resolves to its actual page");
    auto range=chapter.getPageRangeForTocIndex(0);
    require(range && range->startPage<=1 && range->endPage==chapter.pageCount, "TOC page counter covers entire original chapter: " + (range ? std::to_string(range->startPage)+".."+std::to_string(range->endPage) : "missing"));
    Section pictures(epub,1,renderer);
    require(pictures.loadSectionFile(params) || (pictures.createSectionFile(params,{},true) && pictures.loadSectionFile(params)), "build four-image chapter");
    int images=0;
    for(int p=0;p<pictures.pageCount;++p) { pictures.currentPage=p; auto page=pictures.loadPageFromSectionFile(); require(bool(page),"image page"); for(const auto& el:page->elements) if(el->getTag()==TAG_PageImage) ++images; }
    require(images==4, "all four images render in one real chapter, found " + std::to_string(images));
    Section::BuildParams previewParams=params;
    previewParams.previewAnchor=anchor("p1100"); previewParams.previewMaxPages=2;
    Section preview(epub,0,renderer);
    require(preview.loadSectionFile(previewParams) || (preview.createSectionFile(previewParams,{},true) && preview.loadSectionFile(previewParams)), "build late targeted preview");
    require(preview.pageCount>0 && preview.pageCount<=2, "preview stays bounded to two pages");
    preview.currentPage=0; auto previewPage=preview.loadPageFromSectionFile();
    bool previewHasTarget=false;
    require(bool(previewPage), "load preview");
    for(const auto& el:previewPage->elements) if(el->getTag()==TAG_PageLine) {
      const auto& b=*static_cast<const PageLine&>(*el).getBlock();
      for(uint16_t w=0;w<b.wordCount();++w) if(std::string(b.wordText(w))=="TOKEN1100") previewHasTarget=true;
    }
    require(previewHasTarget,"late targeted preview starts with paragraph 1100");
    const auto count=chapter.pageCount;
    const auto coldFingerprint=pageFingerprint(chapter);
    Section warm(epub,0,renderer); require(warm.loadSectionFile(params), "warm page cache");
    require(warm.pageCount==count && warm.getPageForAnchor(anchor("p1100"))==latePage,"warm page count and anchor unchanged");
    require(pageFingerprint(warm)==coldFingerprint,"every warm page matches cold text, positions and links");
    require(warm.loadSectionFile(params),"repeat warm cache load");
    auto warmRange=warm.getPageRangeForTocIndex(0);
    require(warmRange && warmRange->endPage==count,"reloading does not duplicate TOC boundaries");
    auto activeParams=params; activeParams.viewportWidth=450;
    Section active(epub,0,renderer); active.loadSectionFile(activeParams);
    testMillisValue=0; testMillisStep=1;
    bool sawLivePage=false;
    for(int step=0;step<10000;++step) {
      auto result=active.stepSectionBuild(activeParams,1,{},true);
      require(result!=Section::BuildStep::Failed,"incremental build succeeds");
      if(active.hasActiveBuild() && active.activeBuildPageCount()>1) {
        auto live=active.loadPageFromActiveBuild(1);
        require(live && !live->elements.empty(),"read page from disk index while build active");
        sawLivePage=true; break;
      }
      if(result==Section::BuildStep::Done) break;
    }
    testMillisStep=0;
    require(sawLivePage,"incremental build yields while pages available");
    active.abortSectionBuild();
    require(!active.hasActiveBuild() && !active.loadSectionFile(activeParams),"aborted partial cache is not reusable");
    for(const auto& entry:std::filesystem::recursive_directory_iterator(book.getCachePath())) {
      const auto name=entry.path().filename().string();
      require(name.find(".pages.tmp")==std::string::npos && name.find(".paragraphs.tmp")==std::string::npos && name.find(".anchors.tmp")==std::string::npos,"abort removes disk index scratch files");
    }
    std::cout << "PASS continuous chapter: " << count << " pages, 1200 paragraphs, late anchors, chapter counter, four images, warm page identity, targeted preview, active page read and abort cleanup\n";
    return 0;
  } catch(const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
