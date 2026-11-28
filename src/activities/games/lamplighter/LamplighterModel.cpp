#include "LamplighterModel.h"
#include "LamplighterData.h"
#include <algorithm>
namespace lamplighter {
namespace {
uint32_t hash(const std::vector<uint8_t>&b,size_t n){uint32_t h=2166136261u;for(size_t i=0;i<n;++i)h=(h^b[i])*16777619u;return h;}
void put(std::vector<uint8_t>&b,uint32_t v){for(int k=0;k<4;++k)b.push_back(v>>(8*k));}
uint32_t get(const std::vector<uint8_t>&b,size_t p){return uint32_t(b[p])|(uint32_t(b[p+1])<<8)|(uint32_t(b[p+2])<<16)|(uint32_t(b[p+3])<<24);}
int bits(uint8_t n){int k=0;while(n){k+=n&1;n>>=1;}return k;}
bool orientation(uint8_t a,uint8_t b){for(int k=0;k<4;++k){if(a==b)return true;a=Model::rotateMask(a);}return false;}
}
const uint8_t* Model::row()const {return level==0?data::easy[puzzle]:level==1?data::medium[puzzle]:data::hard[puzzle];}
int Model::source()const{return row()[cells()];}
uint8_t Model::solution(int i)const{return i>=0&&i<cells()?row()[i]>>4:0;}
uint8_t Model::initial(int i)const{return i>=0&&i<cells()?row()[i]&15:0;}
void Model::open(int difficulty,int index){level=std::clamp(difficulty,0,2);puzzle=std::clamp(index,0,Count-1);last[level]=puzzle;restart();}
void Model::restart(){moves=0;tiles.fill(0);locks.fill(0);history.clear();for(int i=0;i<cells();++i)tiles[i]=initial(i);}
std::array<bool,Model::MaxCells> Model::lit()const{
 std::array<bool,MaxCells> on{};std::array<int,MaxCells> q{};int head=0,tail=0;q[tail++]=source();on[source()]=true;
 constexpr int dr[]={-1,0,1,0},dc[]={0,1,0,-1};
 while(head<tail){int i=q[head++];for(int d=0;d<4;++d){if(!(tiles[i]&(1<<d)))continue;int r=i/size()+dr[d],c=i%size()+dc[d];if(r<0||r>=size()||c<0||c>=size())continue;int j=r*size()+c;if((tiles[j]&(1<<((d+2)%4)))&&!on[j]){on[j]=true;q[tail++]=j;}}}return on;
}
bool Model::solved()const{
 const auto on=lit();int edges=0;constexpr int dr[]={-1,0,1,0},dc[]={0,1,0,-1};
 for(int i=0;i<cells();++i){if(!on[i])return false;for(int d=0;d<4;++d){if(!(tiles[i]&(1<<d)))continue;++edges;int r=i/size()+dr[d],c=i%size()+dc[d];if(r<0||r>=size()||c<0||c>=size()||!(tiles[r*size()+c]&(1<<((d+2)%4))))return false;}}
 return edges==2*(cells()-1);
}
void Model::remember(int i){if(history.size()==MaxHistory)history.erase(history.begin());history.push_back({uint8_t(i),tiles[i]});}
bool Model::rotate(int i){if(i<0||i>=cells()||locks[i]||solved())return false;const auto next=rotateMask(tiles[i]);if(next==tiles[i])return false;remember(i);tiles[i]=next;++moves;recordWin();return true;}
bool Model::toggleLock(int i){if(i<0||i>=cells()||locks[i]==2||solved())return false;locks[i]=locks[i]?0:1;return true;}
bool Model::undo(){if(solved())return false;while(!history.empty()){auto t=history.back();history.pop_back();if(locks[t.cell]==2)continue;tiles[t.cell]=t.mask;++moves;return true;}return false;}
int Model::hint(){if(solved())return -1;auto on=lit();int chosen=-1;for(int i=0;i<cells();++i){if(tiles[i]==solution(i)||locks[i]==2)continue;if(chosen<0)chosen=i;bool near=on[i];if(i>=size())near|=on[i-size()];if(i+size()<cells())near|=on[i+size()];if(i%size())near|=on[i-1];if(i%size()+1<size())near|=on[i+1];if(near){chosen=i;break;}}if(chosen>=0){remember(chosen);tiles[chosen]=solution(chosen);locks[chosen]=2;++moves;recordWin();}return chosen;}
int Model::lampCount(bool onlyLit)const{const auto on=lit();int n=0;for(int i=0;i<cells();++i)if(i!=source()&&bits(tiles[i])==1&&(!onlyLit||on[i]))++n;return n;}
bool Model::wasCompleted(int d,int i)const{if(d<0||d>2||i<0||i>=Count)return false;int bit=d*Count+i;return completed[bit/8]&(1<<(bit%8));}
int Model::completedCount(int d)const{int n=0;for(int i=0;i<Count;++i)n+=wasCompleted(d,i);return n;}
void Model::recordWin(){if(solved()){int bit=level*Count+puzzle;completed[bit/8]|=1<<(bit%8);}}
std::vector<uint8_t> Model::encode()const{
 std::vector<uint8_t>b{'L','A','M',1,level};put(b,puzzle);put(b,moves);for(auto v:last)put(b,v);b.insert(b.end(),completed.begin(),completed.end());b.insert(b.end(),tiles.begin(),tiles.begin()+cells());b.insert(b.end(),locks.begin(),locks.begin()+cells());put(b,history.size());for(auto t:history){b.push_back(t.cell);b.push_back(t.mask);}put(b,hash(b,b.size()));return b;
}
bool Model::decode(const std::vector<uint8_t>&b){
 constexpr size_t base=25+75;if(b.size()<base+50+8||b.size()>1270||b[0]!='L'||b[1]!='A'||b[2]!='M'||b[3]!=1||b[4]>2||hash(b,b.size()-4)!=get(b,b.size()-4))return false;
 auto idx=get(b,5);auto move=get(b,9);if(idx>=Count||move>10000000)return false;
 Model next;next.open(b[4],idx);next.moves=move;for(int k=0;k<3;++k){auto v=get(b,13+4*k);if(v>=Count)return false;next.last[k]=v;}if(next.last[next.level]!=idx)return false;
 std::copy_n(b.begin()+25,75,next.completed.begin());size_t p=base;
 if(b.size()<base+2*next.cells()+8)return false;
 for(int i=0;i<next.cells();++i){auto m=b[p++];if(!orientation(next.solution(i),m))return false;next.tiles[i]=m;}
 for(int i=0;i<next.cells();++i){auto l=b[p++];if(l>2||(l==2&&next.tiles[i]!=next.solution(i)))return false;next.locks[i]=l;}
 auto count=get(b,p);p+=4;if(count>MaxHistory||p+count*2+4!=b.size())return false;
 for(uint32_t k=0;k<count;++k){int i=b[p++];auto mask=b[p++];if(i>=next.cells()||!orientation(next.solution(i),mask))return false;next.history.push_back({uint8_t(i),mask});}
 *this=std::move(next);recordWin();return true;
}
}
