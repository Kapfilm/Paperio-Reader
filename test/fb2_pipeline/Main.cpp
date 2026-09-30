#include <Fb2.h>
#include <Epub.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <fstream>
void reportIo(const char* stage, std::chrono::steady_clock::time_point start) {
 std::cout << stage << " ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count() << " opens=" << ioCounts.opens << " reads=" << ioCounts.reads << " writes=" << ioCounts.writes << " seeks=" << ioCounts.seeks << " read_bytes=" << ioCounts.readBytes << " write_bytes=" << ioCounts.writeBytes << std::endl;
 ioCounts={};
}
class CountOutput final : public Print {
 public:
  size_t bytes = 0;
  size_t write(uint8_t) override { ++bytes; return 1; }
  size_t write(const uint8_t*, size_t n) override { bytes += n; return n; }
};
int main(int argc, char** argv) {
 if (argc == 4 && std::string(argv[3]) == "--prepared") {
  Epub epub(argv[1], argv[2]);
  if (!epub.load(true) || epub.getSpineItemsCount()!=3941 || epub.getTocItemsCount()!=1265) return 20;
  for (int i=0; i<epub.getTocItemsCount(); ++i) {
   const auto entry=epub.getTocItem(i);
   if (entry.title.empty() || entry.spineIndex<0 || entry.spineIndex>=3941) return 21;
  }
  std::cout << "PASS saved Bible package: 3941 spines, 1265 named TOC targets" << std::endl;
  return 0;
 }
 if (argc != 3) return 2;
 auto start = std::chrono::steady_clock::now();
 std::filesystem::create_directories(argv[2]);
 std::string package, cache;
 int chapters;
 {
  Fb2 book(argv[1], argv[2]);
  if (!book.load([](int p) { std::cerr << "progress=" << p << "\n"; })) return 3;
  if (book.needsPreparation()) return 30;
  package=book.getPackagePath(); cache=book.getCachePath(); chapters=book.getChapterCount();
  reportIo("FB2_LOAD", start);
  std::cout << "PACKAGE chapters=" << chapters << " path=" << package << std::endl;
 }
 auto epubStart=std::chrono::steady_clock::now();
 Epub epub(package, argv[2]);
 if (!epub.load(true)) return 4;
 if (epub.needsFirstOpenIndexing(true)) return 34;
 {
  const auto css=std::filesystem::path(cache)/"css_rules.cache";
  const auto backup=css.string()+".probe-backup";
  const bool existed=std::filesystem::exists(css);
  if (existed) std::filesystem::rename(css,backup);
  const bool ignoreDisabledCss=!epub.needsFirstOpenIndexing(true);
  const bool rebuildEnabledCss=epub.needsFirstOpenIndexing(false);
  if (existed) std::filesystem::rename(backup,css);
  if (!ignoreDisabledCss || !rebuildEnabledCss) return 35;
  std::cout << "PASS disabled embedded styles do not trigger an indexing popup" << std::endl;
 }
 reportIo("EPUB_LOAD", epubStart);
 std::cout << "EPUB spines=" << epub.getSpineItemsCount() << " toc=" << epub.getTocItemsCount() << std::endl;
 if (epub.getSpineItemsCount() != chapters || epub.getTocItemsCount() == 0) return 5;
 for (int i=0;i<epub.getTocItemsCount();i++) {
  auto entry=epub.getTocItem(i);
  std::cout << "TOC " << i << " spine=" << entry.spineIndex << " title=" << entry.title << std::endl;
  if(entry.spineIndex<0 || entry.spineIndex>=chapters || entry.title.empty()) return 6;
 }
 {
  std::deque<uint32_t> sizes;
  if(!Fb2::loadApproxChapterSizes(cache, sizes) || sizes.size()!=static_cast<size_t>(chapters)) return 7;
  const auto index=std::filesystem::path(cache)/".fb2_sections.bin";
  const auto backup=index.string()+".test-backup";
  std::filesystem::copy_file(index, backup, std::filesystem::copy_options::overwrite_existing);
  std::filesystem::resize_file(index, std::filesystem::file_size(index)-1);
  bool accepted=Fb2::loadApproxChapterSizes(cache, sizes);
  std::filesystem::rename(backup,index);
  if(accepted) return 8;
  std::cout << "PASS truncated section index rejected" << std::endl;
 }
 {
  const auto bookmarks=std::filesystem::path(cache)/"bookmarks.bin";
  {std::ofstream out(bookmarks); out << "test-bookmark-preserve";}
  const auto index=std::filesystem::path(cache)/".fb2_sections.bin";
  {std::fstream out(index, std::ios::binary|std::ios::in|std::ios::out); out.seekp(6); out.put(28);}
  Fb2 book(argv[1],argv[2]);
  if(!book.needsPreparation()) return 31;
  if(!book.load()) return 9;
  std::ifstream in(bookmarks); std::string value; in >> value;
  if(value!="test-bookmark-preserve") return 10;
  std::cout << "PASS bookmarks preserved during version 28 upgrade" << std::endl;
 }
 {
  // Missing generated navigation needs a real rebuild, not a silent warm open.
  for (const char* name : {".fb2_nav.bin", ".fb2_anchors.bin", ".fb2_source", ".fb2_anchors_ready"}) {
    const auto path = std::filesystem::path(cache)/name;
    const auto backup = path.string()+".probe-backup";
    std::filesystem::rename(path,backup);
    Fb2 probe(argv[1],argv[2]);
    const bool needed = probe.needsPreparation();
    std::filesystem::rename(backup,path);
    if (!needed) return 32;
  }
  Fb2 cached(argv[1],argv[2]);
  ioCounts = {};
  if (cached.needsPreparation() || ioCounts.writes != 0) return 33;
  std::cout << "PASS prepared FB2 skips indexing; missing/obsolete caches require it; probe writes nothing" << std::endl;
  if(!cached.load() || cached.getChapterCount()!=chapters) return 11;
  Epub warm(package, argv[2]);
  if(!warm.load(true) || warm.getSpineItemsCount()!=chapters || warm.getTocItemsCount()!=epub.getTocItemsCount()) return 12;
  CountOutput output;
  if(!warm.readItemContentsToStream(warm.getSpineItem(0).href, output, 1024) || output.bytes==0) return 13;
  std::cout << "PASS warm reopen and first chapter rendering bytes=" << output.bytes << std::endl;
 }
 {
  const auto cover = std::filesystem::path(cache)/"thumb_640.bmp";
  { std::ofstream out(cover); out << "test-retained-cover"; }
  for (int i=0; i<6; ++i) {
   const auto source = std::filesystem::path(argv[2])/("extra-"+std::to_string(i)+".fb2");
   { std::ofstream out(source); out << "<FictionBook><description><title-info><book-title>Other book</book-title></title-info></description><body><section><title><p>Chapter</p></title><p>Text</p></section></body></FictionBook>"; }
   Fb2 other(source.string(), argv[2]);
   if (!other.load()) return 14;
  }
  std::ifstream in(cover); std::string value; in >> value;
  if (value != "test-retained-cover" || !std::filesystem::exists(std::filesystem::path(cache)/"bookmarks.bin") || !std::filesystem::exists(package)) return 15;
  Fb2 again(argv[1],argv[2]);
  if (!again.load() || !std::filesystem::exists(cover)) return 16;
  std::cout << "PASS cover/bookmarks/package retained after six other books" << std::endl;
 }
 std::cout << "PASS elapsed_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count() << std::endl;
}
