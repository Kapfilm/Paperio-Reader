#include <Fb2.h>
#include <Epub.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

class TextOutput final : public Print {
 public:
  std::string text;
  size_t write(uint8_t c) override { text += static_cast<char>(c); return 1; }
  size_t write(const uint8_t* p, size_t n) override { text.append(reinterpret_cast<const char*>(p), n); return n; }
};
template <typename T> bool pod(std::ifstream& in, T& v) {
  return static_cast<bool>(in.read(reinterpret_cast<char*>(&v), sizeof(v)));
}
void metric(const std::string& stage) {
  std::cout << stage << " reads=" << ioCounts.reads << " seeks=" << ioCounts.seeks
            << " opens=" << ioCounts.opens << " readBytes=" << ioCounts.readBytes << std::endl;
}
int main(int argc, char** argv) {
  if (argc != 5) return 2;
  std::filesystem::create_directories(argv[2]);
  std::filesystem::create_directories(argv[4]);
  Fb2 book(argv[1], argv[2]);
  if (!book.load()) return 3;
  Epub epub(book.getPackagePath(), argv[2]);
  if (!epub.load(true)) return 4;
  // Independent sequential stdio decoding is outside the measured HalFile calls.
  std::ifstream records(book.getCachePath() + "/.fb2_sections.bin", std::ios::binary);
  records.seekg(7);
  std::map<std::string, int> chapters;
  int count = 0;
  while (records.peek() != EOF) {
    uint8_t level; uint32_t offset; uint16_t idSize, titleSize;
    if (!pod(records, level) || !pod(records, offset) || !pod(records, idSize)) return 5;
    std::string id(idSize, '\0');
    if (!records.read(id.data(), idSize) || !pod(records, titleSize)) return 5;
    records.seekg(titleSize + 20, std::ios::cur);
    if (!records) return 5;
    if (!id.empty()) chapters.emplace(id, count);
    ++count;
  }
  if (count != epub.getSpineItemsCount()) return 6;
  std::ifstream ids(argv[3]); std::string id;
  while (std::getline(ids, id)) {
    const auto match = chapters.find(id);
    if (match == chapters.end()) return 7;
    const auto spine = epub.getSpineItem(match->second);
    ioCounts = {};
    if (epub.resolveHrefToSpineIndex(spine.href + "#note", 0) != match->second) return 8;
    metric("RESOLVE " + id);
    TextOutput output;
    ioCounts = {};
    if (!epub.readItemContentsToStream(spine.href, output, 1024)) return 9;
    metric("RENDER " + id);
    std::ofstream saved(std::filesystem::path(argv[4]) / (id + ".html"));
    saved << output.text;
    if (!saved) return 10;
  }
  // The first Biblical text chapter contains note links: exercise ID lookup,
  // not only navigation to a pre-resolved chapter address.
  TextOutput linkedChapter;
  const int firstText = chapters.count("n1") ? 3 : 0;
  ioCounts = {};
  if (!epub.readItemContentsToStream(epub.getSpineItem(firstText).href, linkedChapter, 1024)) return 11;
  metric("LINKED_CHAPTER");
  std::ofstream(std::filesystem::path(argv[4]) / "linked-chapter.html") << linkedChapter.text;
  std::cout << "PASS notes=" << chapters.size() << " chapters=" << count << std::endl;
}
