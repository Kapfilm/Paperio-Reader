#include <Fb2.h>
#include <Epub.h>
#include <Fb2AnchorIndex.h>
#include <filesystem>
#include <fstream>
#include <iostream>
class Output final : public Print {
 public:
  std::string data;
  size_t write(uint8_t c) override { data += char(c); return 1; }
  size_t write(const uint8_t* p, size_t n) override { data.append(reinterpret_cast<const char*>(p), n); return n; }
};
int main(int argc, char** argv) {
  if (argc != 5) return 2;
  std::filesystem::create_directories(argv[2]); std::filesystem::create_directories(argv[4]);
  Fb2 book(argv[1], argv[2]); if (!book.load()) return 3;
  Epub epub(book.getPackagePath(), argv[2]); if (!epub.load(true)) return 4;
  Epub reopened(book.getPackagePath(), argv[2]); if (!reopened.load(true, true)) return 10;
  size_t verifiedLinks = 0;
  Fb2AnchorIndex anchors;
  if (!anchors.open(book.getCachePath()+"/.fb2_sections.bin",book.getCachePath()+"/.fb2_anchors.bin",29)) return 5;
  std::ifstream targets(argv[3]); if (!targets) return 6;
  std::ofstream mapping(std::filesystem::path(argv[4])/"targets.tsv");
  std::string id; size_t count=0; ioCounts={};
  while (std::getline(targets,id)) {
    int chapter=anchors.chapterForId(id);
    if (chapter<0) { std::cerr << "Missing " << id << '\n'; return 7; }
    mapping << id << '\t' << chapter << '\n'; ++count;
  }
  std::cout << "LOOKUPS " << count << " reads=" << ioCounts.reads << " bytes=" << ioCounts.readBytes << '\n';
  for (int i=0;i<epub.getSpineItemsCount();++i) {
    Output output; const auto href=epub.getSpineItem(i).href;
    if (!epub.readItemContentsToStream(href,output,1024)) return 8;
    size_t pos = 0;
    while ((pos = output.data.find("href=\"", pos)) != std::string::npos) {
      pos += 6; const auto end = output.data.find('"', pos);
      if (end == std::string::npos) return 11;
      const std::string link = output.data.substr(pos, end-pos); pos = end+1;
      const auto chapterPos = link.find("chapter_");
      if (chapterPos == std::string::npos || link.find('#') == std::string::npos) continue;
      const int expected = std::stoi(link.substr(chapterPos+8));
      // Exact href stored in Page::footnotes, same call as navigateToHref().
      const std::string legacy = link.compare(0, 5, "text/") == 0 ? link : "text/" + link;
      if (epub.resolveHrefToSpineIndex(legacy, i) != expected ||
          reopened.resolveHrefToSpineIndex(legacy, i) != expected ||
          epub.resolveHrefToSpineIndex(link, i) != expected ||
          reopened.resolveHrefToSpineIndex(link, i) != expected) {
        std::cerr << "UI route failed: source=" << i << " href=" << link
                  << " expected=" << expected << " warmBase=" << reopened.getBasePath() << '\n'; return 12;
      }
      ++verifiedLinks;
    }
    std::ofstream f(std::filesystem::path(argv[4])/("chapter_"+std::to_string(i)+".xhtml"));
    f << output.data; if (!f) return 9;
  }
  std::cout << "PASS chapters=" << epub.getSpineItemsCount() << " targets=" << count << " UI_links=" << verifiedLinks << '\n';
}
