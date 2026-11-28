#include "KakuroStore.h"
#include "KakuroCodec.h"
#include <HalStorage.h>
#include <array>
#include <vector>
#include <string>
namespace {
constexpr const char*savePath="/.crosspoint/kakuro_v2.bin";
constexpr const char*statsPath="/.crosspoint/kakuro_v2_stats.bin";
using namespace kakuro_codec;
bool read(const std::string&p,Bytes&b){HalFile f;if(!Storage.openFileForRead("KAK",p.c_str(),f)||f.size()<8||f.size()>256)return false;b.resize(f.size());return f.read(b.data(),b.size())==int(b.size())&&valid(b);}
bool write(const char*p,const Bytes&b){
 if(!Storage.exists("/.crosspoint")&&!Storage.mkdir("/.crosspoint"))return false;
 const std::string tmp=std::string(p)+".tmp",bak=std::string(p)+".bak";HalFile f;
 if(!Storage.openFileForWrite("KAK",tmp.c_str(),f)||f.write(b.data(),b.size())!=b.size())return false;f.flush();if(!f.close())return false;
 Bytes check;if(!read(tmp,check)||check!=b)return false;
 if(Storage.exists(p)){if(Storage.exists(bak.c_str())&&!Storage.remove(bak.c_str()))return false;if(!Storage.rename(p,bak.c_str()))return false;}
 if(!Storage.rename(tmp.c_str(),p)){if(!Storage.exists(p)&&Storage.exists(bak.c_str()))Storage.rename(bak.c_str(),p);return false;}
 if(Storage.exists(bak.c_str()))Storage.remove(bak.c_str());return true;
}

}
bool KakuroStore::hasInProgress(){KakuroSaveSlot s;return load(s);}
bool KakuroStore::load(KakuroSaveSlot&o){for(const auto&p:{std::string(savePath),std::string(savePath)+".bak",std::string(savePath)+".tmp"}){Bytes b;if(read(p,b)&&decode(b,o))return true;}return false;}
bool KakuroStore::save(const KakuroSaveSlot&s){Bytes b{'K',2,uint8_t(s.difficulty),s.variant};u16(b,s.elapsedSec);b.push_back(s.cursorRow);b.push_back(s.cursorColumn);b.insert(b.end(),s.board.user,s.board.user+64);b.insert(b.end(),s.board.hinted,s.board.hinted+64);seal(b);KakuroSaveSlot check;return decode(b,check)&&write(savePath,b);}
bool KakuroStore::clear(){bool ok=true;for(const auto&p:{std::string(savePath),std::string(savePath)+".bak",std::string(savePath)+".tmp"})if(Storage.exists(p.c_str()))ok=Storage.remove(p.c_str())&&ok;return ok;}
KakuroStats KakuroStore::loadStats(){for(const auto&p:{std::string(statsPath),std::string(statsPath)+".bak",std::string(statsPath)+".tmp"}){Bytes b;if(!read(p,b)||b.size()!=102||b[0]!='S'||b[1]!=2)continue;KakuroStats s;size_t i=2;for(auto&n:s.bestTimeSec)n=get16(b,i);for(auto&n:s.completedCount)n=get16(b,i);for(auto&n:s.startedCount)n=get16(b,i);bool good=true;for(auto&n:s.positions){n=b[i++];if(n>=200)good=false;}for(auto&level:s.completed)for(auto&n:level)n=b[i++];for(int l=0;l<3;++l){int total=0;for(auto byte:s.completed[l])for(int bit=0;bit<8;++bit)total+=(byte>>bit)&1;if(total!=s.completedCount[l])good=false;}if(good)return s;}return {};}
bool KakuroStore::saveStats(const KakuroStats&s){Bytes b{'S',2};for(auto n:s.bestTimeSec)u16(b,n);for(auto n:s.completedCount)u16(b,n);for(auto n:s.startedCount)u16(b,n);for(auto n:s.positions)b.push_back(n);for(const auto&l:s.completed)for(auto n:l)b.push_back(n);seal(b);return write(statsPath,b);}
void KakuroStore::recordStart(KakuroBoard::Difficulty d){auto s=loadStats();int l=int(d);if(l>2)return;if(s.startedCount[l]<65535)++s.startedCount[l];saveStats(s);}
void KakuroStore::recordCompletion(KakuroBoard::Difficulty d,uint16_t time,uint8_t index){auto s=loadStats();int l=int(d);if(l>2||index>=200)return;auto&byte=s.completed[l][index/8];if(!(byte&(1<<(index%8)))){byte|=1<<(index%8);++s.completedCount[l];}if(!s.bestTimeSec[l]||time<s.bestTimeSec[l])s.bestTimeSec[l]=time;saveStats(s);}
