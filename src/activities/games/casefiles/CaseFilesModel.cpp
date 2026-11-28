#include "CaseFilesModel.h"
#include "CaseFilesData.h"
#include <algorithm>
#include <cstring>
namespace casefiles {
namespace {
int digit(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='z')return c-'a'+10;return -1;}
uint32_t sum(const std::vector<uint8_t>&b,size_t n){uint32_t v=2166136261u;for(size_t i=0;i<n;++i)v=(v^b[i])*16777619u;return v;}
void put(std::vector<uint8_t>&b,uint32_t v){for(int i=0;i<4;++i)b.push_back(v>>(8*i));}
uint32_t get(const std::vector<uint8_t>&b,size_t p){return uint32_t(b[p])|(uint32_t(b[p+1])<<8)|(uint32_t(b[p+2])<<16)|(uint32_t(b[p+3])<<24);}
bool tick(uint8_t m){return m==Model::Tick||m==Model::Hint;}
}
bool Puzzle::parse(const char* line){
 if(!line)return false;Puzzle p;size_t len=strlen(line);if(len<10||len>512)return false;
 auto colon=strchr(line,':');if(!colon)return false;size_t end=colon-line,pos=3;
 int th=digit(line[0]),kk=digit(line[1]),nn=digit(line[2]);if(th<0||th>=6||kk<3||kk>4||nn<4||nn>5)return false;
 p.theme=th;p.k=kk;p.n=nn;
 for(int g=0;g<kk;++g){if(pos>=end)return false;int kind=digit(line[pos++]);if(kind<0||kind>3||(g==0)!=(kind==0))return false;p.groups[g].kind=kind;
  uint8_t used=0;for(int s=0;s<nn;++s){if(pos>=end)return false;int item=digit(line[pos++]);if(item<0||item>=6||(used&(1<<item)))return false;used|=1<<item;p.groups[g].items[s]=item;}
  used=0;for(int person=0;person<nn;++person){int slot=person;if(g){if(pos>=end)return false;slot=digit(line[pos++]);}if(slot<0||slot>=nn||(used&(1<<slot)))return false;used|=1<<slot;p.groups[g].slot[person]=slot;p.groups[g].owner[slot]=person;}
 }
 if(pos!=end)return false;pos=end+1;
 while(pos<len&&line[pos]!='.'){
  Clue c;c.type=line[pos++];c.count=c.type=='e'?3:c.type=='o'?4:2;
  if(c.type!='='&&c.type!='!'&&c.type!='e'&&c.type!='o'&&c.type!='<'&&c.type!='+')return false;
  for(int i=0;i<c.count;++i){if(pos+1>=len)return false;int g=digit(line[pos++]),s=digit(line[pos++]);if(g<0||g>=kk||s<0||s>=nn)return false;c.entities[i]={uint8_t(g),uint8_t(s)};}
  if(c.type=='<'||c.type=='+'){if(pos>=len)return false;int g=digit(line[pos++]);if(g<1||g>=kk)return false;c.group=g;}
  if(c.type=='+'){if(pos>=len)return false;int d=digit(line[pos++]);if(d<1||d>5)return false;c.difference=d;}
  p.clues.push_back(c);if(p.clues.size()>12)return false;
  if(pos<len&&line[pos]==',')++pos;else if(pos<len&&line[pos]!='.')return false;
 }
 while(pos<len)if(line[pos++]!='.')return false;
 if(p.clues.empty())return false;*this=std::move(p);return true;
}
bool Puzzle::clueTrue(const Clue&c)const{
 int a=who(c.entities[0]),b=who(c.entities[1]);
 if(c.type=='=')return a==b;if(c.type=='!')return a!=b;
 if(c.type=='e')return (a==b)!=(a==who(c.entities[2]));
 if(c.type=='o'){int x=who(c.entities[2]),y=who(c.entities[3]);return a!=b&&((a==x&&b==y)||(a==y&&b==x));}
 int va=groups[c.group].items[groups[c.group].slot[a]],vb=groups[c.group].items[groups[c.group].slot[b]];
 return c.type=='<'?va<vb:va-vb==c.difference;
}
bool Model::open(int l,int i){Puzzle p;if(!p.parse(bankLine(l,i)))return false;level=l;index=i;puzzle=std::move(p);pairCount=0;for(int a=0;a<puzzle.k;++a)for(int b=a+1;b<puzzle.k;++b)pairs[pairCount++]={uint8_t(a),uint8_t(b)};positions[l]=i;reset(false);return true;}
void Model::reset(bool clearCompleted){for(auto&m:marks)m.fill(0);struck.fill(0);history.clear();cursor=table=0;if(clearCompleted)completed[(level*200+index)/8]&=~(1<<((level*200+index)%8));}
bool Model::belongs(int pi,int a,int b)const{return puzzle.groups[pairs[pi].a].owner[a]==puzzle.groups[pairs[pi].b].owner[b];}
void Model::setMark(int pi,int c,uint8_t v){if(history.size()==400)history.erase(history.begin());history.push_back({uint8_t(pi),uint8_t(c),marks[pi][c]});marks[pi][c]=v;}
bool Model::cycle(){if(solved()||marks[table][cursor]==Hint)return false;setMark(table,cursor,(marks[table][cursor]+1)%3);noteSolved();return true;}
bool Model::undo(){if(history.empty()||solved())return false;auto c=history.back();history.pop_back();marks[c.table][c.cell]=c.before;table=c.table;cursor=c.cell;return true;}
int Model::wrongCount()const{int out=0;for(int pi=0;pi<pairCount;++pi)for(int c=0;c<puzzle.n*puzzle.n;++c)if(marks[pi][c]&&tick(marks[pi][c])!=belongs(pi,c/puzzle.n,c%puzzle.n))++out;return out;}
bool Model::solved()const{if(!puzzle.n)return false;for(int pi=0;pi<pairCount;++pi)if(pairs[pi].a==0){int n=0;for(int c=0;c<puzzle.n*puzzle.n;++c)if(tick(marks[pi][c])){if(!belongs(pi,c/puzzle.n,c%puzzle.n))return false;++n;}if(n!=puzzle.n)return false;}return true;}
bool Model::hint(){if(solved())return false;for(int pass=0;pass<2;++pass)for(int pi=0;pi<pairCount;++pi)if((pi==table)==(pass==0))for(int c=0;c<puzzle.n*puzzle.n;++c)if(marks[pi][c]&&tick(marks[pi][c])!=belongs(pi,c/puzzle.n,c%puzzle.n)){setMark(pi,c,Blank);table=pi;cursor=c;return true;}
 for(int pass=0;pass<2;++pass)for(int pi=0;pi<pairCount;++pi)if((pi==table)==(pass==0)&&(pi==table||pairs[pi].a==0))for(int c=0;c<puzzle.n*puzzle.n;++c)if(belongs(pi,c/puzzle.n,c%puzzle.n)&&!tick(marks[pi][c])){setMark(pi,c,Hint);table=pi;cursor=c;noteSolved();return true;}return false;}
bool Model::ruledOut(int pi,int a,int b)const{for(int j=0;j<puzzle.n;++j)if((j!=b&&tick(marks[pi][a*puzzle.n+j]))||(j!=a&&tick(marks[pi][j*puzzle.n+b])))return true;return false;}
bool Model::conflict(int pi,int a,int b)const{if(!tick(marks[pi][a*puzzle.n+b]))return false;return ruledOut(pi,a,b);}
void Model::noteSolved(){if(solved())completed[(level*200+index)/8]|=1<<((level*200+index)%8);}
bool Model::isCompleted(int l,int i)const{return l>=0&&l<3&&i>=0&&i<200&&(completed[(l*200+i)/8]&(1<<((l*200+i)%8)));}
int Model::completedCount(int l)const{int n=0;for(int i=0;i<200;++i)n+=isCompleted(l,i);return n;}
std::vector<uint8_t> Model::encode()const{
 std::vector<uint8_t>b={'C','A','S',1,uint8_t(level),uint8_t(index),uint8_t(table),uint8_t(cursor)};
 b.insert(b.end(),positions.begin(),positions.end());b.insert(b.end(),completed.begin(),completed.end());b.insert(b.end(),struck.begin(),struck.end());
 for(const auto&m:marks)b.insert(b.end(),m.begin(),m.end());put(b,history.size());for(auto c:history){b.push_back(c.table);b.push_back(c.cell);b.push_back(c.before);}put(b,sum(b,b.size()));return b;
}
bool Model::decode(const std::vector<uint8_t>&b){
 constexpr size_t base=8+3+75+12+150+4;
 if(b.size()<base+4||b.size()>base+1200+4||b[0]!='C'||b[1]!='A'||b[2]!='S'||b[3]!=1||get(b,b.size()-4)!=sum(b,b.size()-4))return false;
 Model out;if(!out.open(b[4],b[5])||b[6]>=out.pairCount||b[7]>=out.puzzle.n*out.puzzle.n)return false;out.table=b[6];out.cursor=b[7];size_t p=8;
 for(auto&v:out.positions){v=b[p++];if(v>=200)return false;}if(out.positions[out.level]!=out.index)return false;for(auto&v:out.completed)v=b[p++];for(size_t i=0;i<out.struck.size();++i){auto v=b[p++];if(v>1||(i>=out.puzzle.clues.size()&&v))return false;out.struck[i]=v;}
 for(int pi=0;pi<6;++pi)for(int c=0;c<25;++c){auto v=b[p++];if(v>Hint||(v&&(pi>=out.pairCount||c>=out.puzzle.n*out.puzzle.n)))return false;if(v==Hint&&!out.belongs(pi,c/out.puzzle.n,c%out.puzzle.n))return false;out.marks[pi][c]=v;}
 uint32_t n=get(b,p);p+=4;if(n>400||p+n*3+4!=b.size())return false;for(uint32_t j=0;j<n;++j){Change c{b[p],b[p+1],b[p+2]};p+=3;if(c.table>=out.pairCount||c.cell>=out.puzzle.n*out.puzzle.n||c.before>Hint)return false;out.history.push_back(c);}
 // A forged undo history must never manufacture a protected incorrect hint.
 auto check=out.marks;for(auto it=out.history.rbegin();it!=out.history.rend();++it){if(it->before==Hint&&!out.belongs(it->table,it->cell/out.puzzle.n,it->cell%out.puzzle.n))return false;check[it->table][it->cell]=it->before;}
 out.noteSolved();*this=std::move(out);return true;
}
}
