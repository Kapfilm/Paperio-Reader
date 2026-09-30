#include <Fb2AnchorIndex.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL line " << __LINE__ << ": " << #x << '\n'; return 1; } } while (0)
namespace {
constexpr uint32_t START = 32 + 4096 * 4;
void u32(std::ostream& out, uint32_t n) { out.write(reinterpret_cast<char*>(&n), 4); }
uint64_t hashId(const std::string& s) { uint64_t h=14695981039346656037ULL; for (unsigned char c:s) { h^=c; h*=1099511628211ULL; } return h; }
void source(const std::string& path) {
 std::ofstream out(path,std::ios::binary); out.write("FB2IDX",6); out.put(29);
 // Minimal valid section records, enough for large virtual chapter numbers.
 for (int i=0;i<4000;++i) { const char empty[29] = {}; out.write(empty,sizeof(empty)); }
}
bool build(const std::string& src, const std::string& path, const std::vector<std::string>& ids) {
 Fb2AnchorIndexWriter writer;
 if (!writer.begin(src,path,29)) return false;
 for (uint32_t i=0;i<ids.size();++i) if (!writer.add(ids[i],i)) return false;
 return writer.finish();
}
void patch32(const std::string& path,uint32_t offset,uint32_t value) {
 std::fstream out(path,std::ios::binary|std::ios::in|std::ios::out); out.seekp(offset);u32(out,value);
}
}
int main() {
 const auto dir=std::filesystem::temp_directory_path()/"paperio-anchor-index-test";
 std::filesystem::create_directories(dir);
 const auto src=(dir/"sections.bin").string(), path=(dir/"anchors.bin").string();
 Fb2AnchorIndex index;
 CHECK(!index.open((dir/"missing.bin").string(),path,29)); CHECK(!index.good());
 { Fb2AnchorIndexWriter w; CHECK(!w.begin((dir/"missing.bin").string(),path,29)); CHECK(!w.good()); }
 source(src);
 CHECK(!index.open(src,(dir/"absent.bin").string(),29));
 std::vector<std::string> ids={"first", "duplicate", "", "duplicate", "last"};
 for (int i=0;ids.size()<50;++i) {
   std::string id="collision"+std::to_string(i);
   if (hashId(id)%4096==hashId("first")%4096) ids.push_back(id);
 }
 ids.push_back("tail");
 CHECK(build(src,path,ids)); CHECK(index.open(src,path,29)); CHECK(index.good());
 ioCounts = {};
 CHECK(index.chapterForId("tail")==static_cast<int>(ids.size()-1));
 CHECK(ioCounts.readBytes<128); CHECK(ioCounts.opens==0);
 for (size_t i=0;i<ids.size();++i) CHECK(index.chapterForId(ids[i])==(ids[i].empty()?-1:ids[i]=="duplicate"?1:static_cast<int>(i)));
 CHECK(index.chapterForId("absent")==-1); CHECK(index.good());
 CHECK(index.chapterForId(std::string(65536,'x'))==-1); CHECK(index.good());
 CHECK(!index.open(src,path,28)); CHECK(!index.good()); CHECK(index.open(src,path,29));
 const auto original=std::filesystem::file_size(path);
 std::filesystem::resize_file(path,original-1); CHECK(!index.open(src,path,29));
 CHECK(build(src,path,ids));
 // Force a true hash-and-length match with unequal ID bytes.
 { std::fstream f(path,std::ios::binary|std::ios::in|std::ios::out); f.seekp(START+18); f.write("foist",5); }
 CHECK(index.open(src,path,29)); CHECK(index.chapterForId("first")==-1); CHECK(index.good());
 CHECK(build(src,path,ids));
 patch32(path,START+12,START); // self cycle must fail promptly
 CHECK(index.open(src,path,29)); CHECK(index.chapterForId("first")==-1); CHECK(!index.good());
 CHECK(build(src,path,ids));
 patch32(path,32+(hashId("first")%4096)*4,UINT32_MAX-1);
 CHECK(index.open(src,path,29)); CHECK(index.chapterForId("first")==-1); CHECK(!index.good());
 CHECK(build(src,path,ids));
 patch32(path,START+8,4000); // invalid chapter bound
 CHECK(index.open(src,path,29)); CHECK(index.chapterForId("first")==-1); CHECK(!index.good());
 CHECK(build(src,path,ids));
 // Builder failures cannot replace the prior complete index.
 { Fb2AnchorIndexWriter w; CHECK(w.begin(src,path,29)); CHECK(!w.add("out-of-range",4000)); CHECK(!w.finish()); }
 CHECK(!std::filesystem::exists(path+".part")); CHECK(std::filesystem::file_size(path)==original);
 CHECK(index.open(src,path,29)); CHECK(index.chapterForId("first")==0);
 { Fb2AnchorIndexWriter w; CHECK(w.begin(src,path,29)); CHECK(!w.add(std::string(65536,'x'),0)); }
 CHECK(!std::filesystem::exists(path+".part"));
 // Abandoning a successful partial build also cleans up only .part.
 { Fb2AnchorIndexWriter w; CHECK(w.begin(src,path,29)); CHECK(w.add("unfinished",10)); }
 CHECK(!std::filesystem::exists(path+".part")); CHECK(index.open(src,path,29)); CHECK(index.chapterForId("tail")==static_cast<int>(ids.size()-1));
 std::filesystem::resize_file(src,std::filesystem::file_size(src)-1);
 CHECK(!index.open(src,path,29)); source(src);
 // Lowest chapter wins independently of enumeration order.
 { Fb2AnchorIndexWriter w; CHECK(w.begin(src,path,29)); CHECK(w.add("same",3000)); CHECK(w.add("same",4)); CHECK(w.add("same",2000)); CHECK(w.finish()); CHECK(!w.add("late",1)); }
 CHECK(index.open(src,path,29)); CHECK(index.chapterForId("same")==4);
 // Large books do not increase reader memory or require a whole-index scan.
 { Fb2AnchorIndexWriter w; CHECK(w.begin(src,path,29));
   for (int i=0;i<50000;++i) CHECK(w.add("verse-"+std::to_string(i),i%4000));
   CHECK(w.finish()); }
 CHECK(index.open(src,path,29)); ioCounts={};
 CHECK(index.chapterForId("verse-49999")==1999); CHECK(ioCounts.readBytes<1500); CHECK(ioCounts.opens==0);
 // Empty index is valid and returns missing without invalidating the handle.
 CHECK(build(src,path,{})); CHECK(index.open(src,path,29)); CHECK(index.chapterForId("none")==-1); CHECK(index.good());
 std::cout << "PASS anchor IDs, duplicate precedence, collisions, hash equality, bounded 50000-ID lookup, corruption, truncation, failed and abandoned builds\n";
}
