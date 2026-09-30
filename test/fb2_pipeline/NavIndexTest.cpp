#include <Fb2NavigationIndex.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL line " << __LINE__ << ": " << #x << '\n'; return 1; } } while (0)
static void u32(std::ostream& out, uint32_t n) { out.write(reinterpret_cast<char*>(&n), 4); }
static void str(std::ostream& out, const std::string& s) { uint16_t n=s.size(); out.write(reinterpret_cast<char*>(&n),2); out.write(s.data(),s.size()); }
static uint64_t hash(const std::string& s) { uint64_t h=14695981039346656037ULL; for (unsigned char c:s) { h^=c;h*=1099511628211ULL; } return h; }
static void source(const std::string& path, const std::vector<std::string>& ids, std::vector<uint32_t>& offsets) {
 std::ofstream out(path,std::ios::binary); out.write("FB2IDX",6); out.put(29);
 for (auto& id:ids) { offsets.push_back(out.tellp()); out.put(1); u32(out,100); str(out,id); str(out,"Chapter title"); for(int i=0;i<5;++i)u32(out,200); }
}
int main() {
 const auto dir=std::filesystem::temp_directory_path()/"paperio-navigation-test";
 std::filesystem::create_directories(dir);
 const auto src=(dir/"sections.bin").string(), nav=(dir/"nav.bin").string();
 // A fresh object and failure before opening index must respect device handles.
 Fb2NavigationIndex missing;
 CHECK(!missing.open((dir/"missing-source.bin").string(), nav, 29));
 CHECK(!missing.good());
 std::vector<std::string> ids={"first", "duplicate", "", "duplicate", "last"};
 // Force long bucket chains; exact comparisons must still select the right ID.
 for (int i=0; ids.size()<70; ++i) {auto s="collision"+std::to_string(i);if(hash(s)%256==hash("first")%256)ids.push_back(s);}
 ids.push_back("tail");
 std::vector<uint32_t> offsets; source(src,ids,offsets);
 CHECK(Fb2NavigationIndex::build(src,nav,29));
 Fb2NavigationIndex index; CHECK(index.open(src,nav,29)); CHECK(index.good()); CHECK(index.chapterCount()==ids.size());
 ioCounts = {};
 CHECK(index.chapterForId("tail")==static_cast<int>(ids.size()-1));
 CHECK(ioCounts.readBytes < 128); // tail lookup must not rescan preceding records
 CHECK(ioCounts.opens == 0);
 uint32_t offset;
 for(uint32_t i=0;i<ids.size();++i) {
   CHECK(index.offsetForChapter(i,offset)); CHECK(offset==offsets[i]);
   CHECK(index.chapterForId(ids[i])==(ids[i].empty()?-1:ids[i]=="duplicate"?1:static_cast<int>(i)));
 }
 CHECK(index.chapterForId("absent")==-1); CHECK(index.good());
 CHECK(!index.offsetForChapter(ids.size(),offset)); CHECK(index.good());
 CHECK(!index.open(src,nav,28)); CHECK(!index.good());
 CHECK(index.open(src,nav,29));
 const auto original=std::filesystem::file_size(nav);
 std::filesystem::resize_file(nav,original-1); CHECK(!index.open(src,nav,29));
 CHECK(Fb2NavigationIndex::build(src,nav,29));
 // Forged hash match must never resolve an unequal identifier.
 { std::fstream f(nav,std::ios::in|std::ios::out|std::ios::binary); uint32_t nodeBase=32+ids.size()*4+1024;
   f.seekp(nodeBase); uint64_t h=hash(ids[2]); f.write(reinterpret_cast<char*>(&h),8); }
 CHECK(index.open(src,nav,29)); CHECK(index.chapterForId("first")==-1); CHECK(index.good());
 CHECK(Fb2NavigationIndex::build(src,nav,29));
 // A matching hash and matching length still require byte-exact source ID equality.
 { std::fstream f(src,std::ios::in|std::ios::out|std::ios::binary); f.seekp(offsets[0]+7);f.write("foist",5); }
 CHECK(index.open(src,nav,29)); CHECK(index.chapterForId("first")==-1); CHECK(index.good());
 { std::fstream f(src,std::ios::in|std::ios::out|std::ios::binary); f.seekp(offsets[0]+7);f.write("first",5); }
 // Cycle/non-decreasing next node detected without unbounded traversal.
 { std::fstream f(nav,std::ios::in|std::ios::out|std::ios::binary); uint32_t base=32+ids.size()*4+1024;
   f.seekp(base+16); u32(f,0); }
 CHECK(index.open(src,nav,29)); CHECK(index.chapterForId("first")==-1); CHECK(!index.good());
 CHECK(Fb2NavigationIndex::build(src,nav,29));
 // Wrong chapter offset rejected rather than read out of bounds.
 { std::fstream f(nav,std::ios::in|std::ios::out|std::ios::binary); f.seekp(32);u32(f,UINT32_MAX); }
 CHECK(index.open(src,nav,29)); CHECK(!index.offsetForChapter(0,offset)); CHECK(!index.good());
 // Malformed source never publishes partial output, keeps previous nav file intact.
 std::filesystem::resize_file(src,std::filesystem::file_size(src)-1);
 CHECK(!Fb2NavigationIndex::build(src,nav,29)); CHECK(!std::filesystem::exists(nav+".part"));
 CHECK(std::filesystem::file_size(nav)==original); CHECK(!index.open(src,nav,29));
 CHECK(!Fb2NavigationIndex::build(src,nav,28));
 std::cout << "PASS navigation LUT, IDs, duplicates, bucket chains, corrupted hash, cycle, truncation, bad offsets, failed build\n";
}
