#include "DelveModel.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace delve {
namespace {
constexpr int dirs[8][2] = {{-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1}};
constexpr MonsterRule rules[] = {
 {4,1,3,0,2,1,3,3,30,true}, {3,1,2,0,2,1,4,2,30,false,true},
 {7,2,4,1,4,2,5,5,20}, {9,2,4,1,6,3,6,6,10,true,false,true},
 {14,3,6,2,9,4,8,15,0}, {18,4,7,3,13,5,9,7,0},
 {28,5,9,3,20,7,10,6,0,false,false,false,true},
 {20,4,8,4,24,8,10,20,0,false,false,false,false,true},
 {60,7,12,5,50,99,99,3,0,false,false,false,false,false,true}};
constexpr int weapons[5][2] = {{1,4},{2,6},{3,7},{4,9},{5,11}};
uint32_t advance(uint32_t& s) { s = static_cast<uint32_t>((static_cast<uint64_t>(s) * 16807) % 2147483647); return s; }
uint32_t mixed(uint32_t seed, uint32_t salt) { seed = seed % 2147483646 + 1; advance(seed); seed = (seed + salt % 1000003) % 2147483646 + 1; advance(seed); return advance(seed); }
uint32_t digest(const std::vector<uint8_t>& b, size_t n) { uint32_t h=2166136261u; for(size_t i=0;i<n;++i) h=(h^b[i])*16777619u; return h; }
}
const MonsterRule& Game::rule(Kind k) { return rules[static_cast<int>(k)]; }
uint32_t Game::next() { return advance(run.rng); }
int Game::random(int lo,int hi) { return lo + next()%static_cast<uint32_t>(hi-lo+1); }
bool Game::chance(int p) { return random(0,99)<p; }
void Game::say(Event e,int v,int s) { if(messages.size()>=4)messages.erase(messages.begin());messages.push_back({e,v,s}); }
Tile Game::tile(int x,int y) const { return inside(x,y)?run.map[index(x,y)]:Tile::Rock; }
bool Game::canStep(int x,int y,int dx,int dy) const {
 if(!inside(x+dx,y+dy)||tile(x+dx,y+dy)==Tile::Rock)return false;
 if(dx&&dy){auto a=tile(x,y),b=tile(x+dx,y+dy);if(a==Tile::Door||a==Tile::Open||b==Tile::Door||b==Tile::Open)return false;}
 return true;
}
int Game::monsterAt(int x,int y) const { for(size_t i=0;i<run.monsters.size();++i)if(run.monsters[i].x==x&&run.monsters[i].y==y)return static_cast<int>(i);return -1; }
int Game::itemAt(int x,int y) const { for(size_t i=0;i<run.items.size();++i)if(run.items[i].x==x&&run.items[i].y==y)return static_cast<int>(i);return -1; }
int Game::radius() const { return run.torch>=100?5:run.torch>0?3:1; }
int Game::attackLow() const {return weapons[run.weapon][0]+run.weaponPlus+run.strength+(run.level-1)/2;}
int Game::attackHigh() const {return weapons[run.weapon][1]+run.weaponPlus+run.strength+(run.level-1)/2;}
int Game::defence() const {return run.armour+1+run.armourPlus;}
int Game::hitChance() const {return std::min(95,75+2*(run.level-1));}
int Game::visibleEnemies() const {int n=0;for(const auto&m:run.monsters)if(run.visible[index(m.x,m.y)])++n;return n;}
std::array<int16_t, Cells> Game::distances(int x,int y,bool seenOnly)const{
 std::array<int16_t,Cells>d;d.fill(-1);if(!inside(x,y))return d;
 std::array<int16_t,Cells>q{};int h=0,t=0;int p=index(x,y);d[p]=0;q[t++]=p;
 while(h<t){p=q[h++];int px=p%Width,py=p/Width;
  for(const auto&v:dirs){int nx=px+v[0],ny=py+v[1];if(!canStep(px,py,v[0],v[1]))continue;int ni=index(nx,ny);if(d[ni]>=0||(seenOnly&&!run.seen[ni]))continue;d[ni]=d[p]+1;q[t++]=ni;}}
 return d;
}
void Game::start(uint32_t seed,uint32_t daily){run=Run{};run.seed=seed%2147483646+1;run.rng=mixed(run.seed,4242);run.daily=daily;
 for(int i=0;i<5;++i)run.colours[i]=i;
 for(int i=4;i>0;--i)std::swap(run.colours[i],run.colours[random(0,i)]);
 for(int i=0;i<6;++i)run.labels[i]=random(0,25);
 run.knownPotions[0]=1;run.bag.push_back({ItemKind::Potion,0});messages.clear();enterLevel(1);say(Event::Enter);
}
void Game::enterLevel(int depth){
 run.depth=std::clamp(depth,1,LastDepth);run.map.fill(Tile::Rock);run.seen.fill(0);run.visible.fill(0);run.monsters.clear();run.items.clear();
 // Keep floor generation separate from combat RNG, as in the original Delve.
 uint32_t combat=run.rng;run.rng=mixed(run.seed,depth);
 struct Room{int x,y,w,h;};std::vector<Room>rooms;
 for(int attempt=0;attempt<300&&rooms.size()<7;++attempt){Room r{0,0,random(3,6),random(3,5)};r.x=random(1,Width-r.w-1);r.y=random(1,Height-r.h-1);bool clear=true;
  for(const auto&q:rooms)if(!(r.x+r.w<q.x-1||r.x-1>q.x+q.w||r.y+r.h<q.y-1||r.y-1>q.y+q.h)){clear=false;break;}
  if(clear)rooms.push_back(r);
 }
 if(rooms.size()<2)rooms={{1,1,5,5},{8,12,5,5}};
 std::sort(rooms.begin(),rooms.end(),[](const Room&a,const Room&b){return a.x*2+a.w<b.x*2+b.w;});
 for(const auto&r:rooms)for(int y=r.y;y<r.y+r.h;++y)for(int x=r.x;x<r.x+r.w;++x)run.map[index(x,y)]=Tile::Floor;
 std::array<uint8_t,Cells>corridor{};
 auto dig=[&](int x,int y){if(run.map[index(x,y)]==Tile::Rock){run.map[index(x,y)]=Tile::Floor;corridor[index(x,y)]=1;}};
 auto join=[&](const Room&a,const Room&b){int x=a.x+a.w/2,y=a.y+a.h/2,tx=b.x+b.w/2,ty=b.y+b.h/2;while(x!=tx){dig(x,y);x+=x<tx?1:-1;}while(y!=ty){dig(x,y);y+=y<ty?1:-1;}dig(x,y);};
 for(size_t i=1;i<rooms.size();++i) { join(rooms[i-1],rooms[i]); }
 join(rooms.front(),rooms.back());
 for(int y=1;y<Height-1;++y)for(int x=1;x<Width-1;++x)if(corridor[index(x,y)]&&chance(35)){
  bool across=tile(x-1,y)!=Tile::Rock&&tile(x+1,y)!=Tile::Rock&&tile(x,y-1)==Tile::Rock&&tile(x,y+1)==Tile::Rock;
  bool down=tile(x,y-1)!=Tile::Rock&&tile(x,y+1)!=Tile::Rock&&tile(x-1,y)==Tile::Rock&&tile(x+1,y)==Tile::Rock;
  if(across||down)run.map[index(x,y)]=Tile::Door;
 }
 run.x=rooms.front().x+rooms.front().w/2;run.y=rooms.front().y+rooms.front().h/2;
 auto d=distances(run.x,run.y);int far=index(run.x,run.y);for(int i=0;i<Cells;++i)if(d[i]>d[far]&&run.map[i]==Tile::Floor)far=i;
 if(depth<LastDepth)run.map[far]=Tile::Stairs;
 else{run.items.push_back({ItemKind::Amulet,0,0,static_cast<uint8_t>(far%Width),static_cast<uint8_t>(far/Width)});
  bool placed=false;for(const auto&v:dirs){int x=far%Width+v[0],y=far/Width+v[1];if(tile(x,y)==Tile::Floor&&!(x==run.x&&y==run.y)){run.monsters.push_back({static_cast<uint8_t>(x),static_cast<uint8_t>(y),Kind::Dragon,60});placed=true;break;}}
  if(!placed)run.monsters.push_back({static_cast<uint8_t>(far%Width),static_cast<uint8_t>(far/Width),Kind::Dragon,60});
 }
 auto spot=[&](int&x,int&y,bool safe){for(int n=0;n<500;++n){x=random(1,Width-2);y=random(1,Height-2);if(tile(x,y)!=Tile::Floor||monsterAt(x,y)>=0||itemAt(x,y)>=0||(x==run.x&&y==run.y))continue;if(safe&&std::abs(x-run.x)+std::abs(y-run.y)<6)continue;return true;}return false;};
 std::vector<Kind>kinds;for(int i=0;i<8;++i)if(depth>=rules[i].from&&depth<=rules[i].to)kinds.push_back(static_cast<Kind>(i));
 for(int i=0;i<3+depth/2;++i){int x=0,y=0;if(spot(x,y,true)){Kind k=kinds[random(0,kinds.size()-1)];run.monsters.push_back({static_cast<uint8_t>(x),static_cast<uint8_t>(y),k,static_cast<int16_t>(rule(k).hp)});}}
 auto put=[&](ItemKind k,int t=0,int amount=0){int x=0,y=0;if(spot(x,y,false))run.items.push_back({k,static_cast<uint8_t>(t),0,static_cast<uint8_t>(x),static_cast<uint8_t>(y),static_cast<uint16_t>(amount)});};
 put(ItemKind::Potion,random(0,4));if(chance(50))put(ItemKind::Potion,random(0,4));if(depth>=2||chance(50))put(ItemKind::Scroll,random(0,5));
 put(ItemKind::Gold,0,random(3,8)*depth);put(ItemKind::Gold,0,random(3,8)*depth);put(ItemKind::Torch);if(chance(30))put(ItemKind::Torch);
 if(chance(45))put(chance(50)?ItemKind::Weapon:ItemKind::Armour,std::clamp(depth/2+random(-1,1),0,4));
 run.rng=combat;updateSight();
}
bool Game::clearLine(int tx,int ty)const{
 int x=run.x,y=run.y,dx=std::abs(tx-x),dy=-std::abs(ty-y),sx=x<tx?1:-1,sy=y<ty?1:-1,e=dx+dy;
 for(;;){if(x==tx&&y==ty)return true;if((x!=run.x||y!=run.y)&&(tile(x,y)==Tile::Rock||tile(x,y)==Tile::Door))return false;int twice=e*2;if(twice>=dy){e+=dy;x+=sx;}if(twice<=dx){e+=dx;y+=sy;}}
}
void Game::updateSight(){run.visible.fill(0);int r=radius();for(int y=std::max(0,run.y-r);y<=std::min(Height-1,run.y+r);++y)for(int x=std::max(0,run.x-r);x<=std::min(Width-1,run.x+r);++x){int dx=x-run.x,dy=y-run.y;if(dx*dx+dy*dy<=r*r+r&&clearLine(x,y)){run.visible[index(x,y)]=1;run.seen[index(x,y)]=1;}}}
void Game::gainXp(int n){run.xp+=n;while(run.level<50&&run.xp>=5*run.level*(run.level+3)/2){++run.level;run.maxHp+=5;run.hp=std::min<int>(run.maxHp,run.hp+5);say(Event::LevelUp,run.level);}}
void Game::attackMonster(int n){auto&m=run.monsters[n];auto r=rule(m.kind);m.awake=1;m.sleep=0;if(!chance(hitChance())){say(Event::Miss,0,static_cast<int>(m.kind));return;}int dmg=std::max(1,random(attackLow(),attackHigh())-random(0,r.def));m.hp-=dmg;say(Event::Hit,dmg,static_cast<int>(m.kind));if(m.hp<=0){say(Event::Killed,0,static_cast<int>(m.kind));++run.kills;gainXp(r.xp);run.monsters.erase(run.monsters.begin()+n);}}
void Game::attackHero(int n){auto&m=run.monsters[n];auto r=rule(m.kind);if(!chance(70)){say(Event::EnemyMiss,0,static_cast<int>(m.kind));return;}int dmg=std::max(1,random(r.lo,r.hi)-random(0,defence()));run.hp-=dmg;say(Event::EnemyHit,dmg,static_cast<int>(m.kind));if(r.dark){run.torch=std::max(0,run.torch-15);say(Event::TorchDrain);}if(r.thief&&run.gold>0&&chance(40)){int loss=std::min<uint32_t>(run.gold,random(5,15)*run.depth);run.gold-=loss;m.hp=0;say(Event::Stolen,loss);}}
void Game::monstersAct(){auto d=distances(run.x,run.y);for(size_t n=0;n<run.monsters.size()&&run.hp>0;++n){auto&m=run.monsters[n];const auto&r=rule(m.kind);if(m.hp<=0)continue;if(r.regen&&m.hp<r.hp)++m.hp;
 bool adjacent=std::abs(m.x-run.x)<=1&&std::abs(m.y-run.y)<=1;
 auto free=[&](int dx,int dy){int x=m.x+dx,y=m.y+dy;return canStep(m.x,m.y,dx,dy)&&monsterAt(x,y)<0&&!(x==run.x&&y==run.y);};
 auto move=[&](int dx,int dy){m.x+=dx;m.y+=dy;if(tile(m.x,m.y)==Tile::Door)run.map[index(m.x,m.y)]=Tile::Open;};
 auto wander=[&](){auto&v=dirs[random(0,7)];if(free(v[0],v[1])&&tile(m.x+v[0],m.y+v[1])!=Tile::Door)move(v[0],v[1]);};
 auto follow=[&](int sign){int here=d[index(m.x,m.y)],best=here*sign,bx=0,by=0;if(here<0)return false;for(const auto&v:dirs){int x=m.x+v[0],y=m.y+v[1];if(!inside(x,y))continue;int dv=d[index(x,y)];if(dv>=0&&dv*sign<best&&free(v[0],v[1])){best=dv*sign;bx=v[0];by=v[1];}}if(bx||by){move(bx,by);return true;}return false;};
 if(m.sleep){--m.sleep;continue;}
 bool sees=run.visible[index(m.x,m.y)]!=0;
 if(!m.awake){if(sees&&(adjacent||chance(35))&&(adjacent||!chance(r.ignore))){m.awake=1;m.lost=0;}else if(!r.guard&&chance(25))wander();continue;}
 m.lost=sees?0:static_cast<uint8_t>(std::min<int>(255,m.lost+1));
 if(m.lost>r.patience||(m.lost&&chance(10))){m.awake=0;m.lost=0;}
 else if(r.coward&&m.hp*3<=r.hp){if(!follow(-1)&&adjacent)attackHero(n);}
 else if(r.erratic&&chance(50))wander();
 else if(adjacent&&canStep(m.x,m.y,run.x-m.x,run.y-m.y))attackHero(n);
 else follow(1);
 }
 run.monsters.erase(std::remove_if(run.monsters.begin(),run.monsters.end(),[](const Monster&m){return m.hp<=0;}),run.monsters.end());
}
void Game::endTurn(){if(!active())return;++run.turns;monstersAct();if(run.hp<=0){run.dead=1;say(Event::Dead);return;}
 if(run.torch>0){--run.torch;if(run.torch==100)say(Event::LowTorch);if(run.torch==0)say(Event::NoTorch);if(++run.regen>=std::max(3,9-run.level)){run.regen=0;if(run.hp<run.maxHp)++run.hp;}}
 updateSight();
}
void Game::pickup(){int n=itemAt(run.x,run.y);if(n<0)return;const Item it=run.items[n];
 if(it.kind==ItemKind::Gold){run.gold+=it.amount;say(Event::Gold,it.amount);}
 else if(it.kind==ItemKind::Torch){run.torch=std::min(600,run.torch+200);say(Event::Torch);}
 else if(it.kind==ItemKind::Amulet){run.won=1;say(Event::Victory);}
 else{if(run.bag.size()>=12){say(Event::FullBag);return;}run.bag.push_back(it);say(Event::Picked,static_cast<int>(it.type),static_cast<int>(it.kind));}
 run.items.erase(run.items.begin()+n);
}
bool Game::step(int dx,int dy){messages.clear();if(!active()||std::abs(dx)>1||std::abs(dy)>1||(!dx&&!dy))return false;
 int x=run.x+dx,y=run.y+dy;if(!canStep(run.x,run.y,dx,dy)){say(Event::Blocked);return false;}
 int m=monsterAt(x,y);if(m>=0){attackMonster(m);endTurn();return true;}
 if(tile(x,y)==Tile::Door){run.map[index(x,y)]=Tile::Open;say(Event::Door);endTurn();return true;}
 run.x=x;run.y=y;pickup();if(active()){if(tile(x,y)==Tile::Stairs)say(Event::Stairs);endTurn();}return true;
}
bool Game::descend(){messages.clear();if(!active()||tile(run.x,run.y)!=Tile::Stairs||run.depth>=LastDepth){say(Event::NoStairs);return false;}enterLevel(run.depth+1);say(run.depth==LastDepth?Event::LastFloor:Event::Descend,run.depth);return true;}
void Game::wait(){messages.clear();if(active()){pickup();endTurn();}}
void Game::rest(){messages.clear();if(!active())return;if(visibleEnemies()){say(Event::UnsafeRest);return;}int n=0;do{endTurn();++n;}while(active()&&run.hp<run.maxHp&&!visibleEnemies()&&n<40);if(active())say(Event::Rested,n);}
bool Game::useItem(int n){messages.clear();if(!active()||n<0||static_cast<size_t>(n)>=run.bag.size())return false;Item it=run.bag[n];run.bag.erase(run.bag.begin()+n);
 if(it.kind==ItemKind::Potion){run.knownPotions[it.type]=1;switch(it.type){case 0:run.hp=std::min<int>(run.maxHp,run.hp+15);say(Event::Heal);break;case 1:run.maxHp+=2;run.hp=run.maxHp;say(Event::ExtraHeal);break;case 2:++run.strength;say(Event::Strength);break;case 3:run.maxHp+=5;run.hp=run.maxHp;say(Event::Life);break;case 4:run.hp-=5;say(Event::Poison);break;}if(run.hp<=0){run.dead=1;say(Event::Dead);return true;}}
 else if(it.kind==ItemKind::Scroll){run.knownScrolls[it.type]=1;switch(it.type){case 0:{std::vector<int>spots;for(int i=0;i<Cells;++i)if(run.map[i]==Tile::Floor&&monsterAt(i%Width,i/Width)<0)spots.push_back(i);if(!spots.empty()){int p=spots[random(0,spots.size()-1)];run.x=p%Width;run.y=p/Width;}say(Event::Teleport);break;}case 1:run.weaponPlus=std::min<int>(50,run.weaponPlus+1);say(Event::WeaponGlow);break;case 2:run.armourPlus=std::min<int>(50,run.armourPlus+1);say(Event::ArmourGlow);break;case 3:run.torch=std::min(600,run.torch+150);say(Event::Light);break;case 4:for(int i=0;i<Cells;++i)if(run.map[i]!=Tile::Rock){run.seen[i]=1;for(const auto&v:dirs)if(inside(i%Width+v[0],i/Width+v[1]))run.seen[index(i%Width+v[0],i/Width+v[1])]=1;}say(Event::Map);break;case 5:for(auto&m:run.monsters)if(run.visible[index(m.x,m.y)]){m.sleep=12;m.awake=0;}say(Event::Sleep);break;}}
 else if(it.kind==ItemKind::Weapon){run.bag.push_back({ItemKind::Weapon,run.weapon,run.weaponPlus});run.weapon=it.type;run.weaponPlus=it.plus;say(Event::Equipped);}
 else if(it.kind==ItemKind::Armour){run.bag.push_back({ItemKind::Armour,run.armour,run.armourPlus});run.armour=it.type;run.armourPlus=it.plus;say(Event::Equipped);}
 else return false;
 updateSight();endTurn();return true;
}
bool Game::dropItem(int n){messages.clear();if(!active()||n<0||static_cast<size_t>(n)>=run.bag.size())return false;if(itemAt(run.x,run.y)>=0||tile(run.x,run.y)==Tile::Stairs||run.items.size()>=32){say(Event::CannotDrop);return false;}Item it=run.bag[n];it.x=run.x;it.y=run.y;run.items.push_back(it);run.bag.erase(run.bag.begin()+n);say(Event::Dropped);endTurn();return true;}
void Game::giveUp(){if(active()){run.dead=1;messages.clear();say(Event::GiveUp);}}

// Explicit bounded wire format (little endian), independent of struct padding.
std::vector<uint8_t> Game::encode()const{
 std::vector<uint8_t>b{'D','L','V',2};auto put=[&](uint32_t n){for(int k=0;k<4;++k)b.push_back((n>>(k*8))&255);};
 for(uint32_t v:{run.seed,run.rng,run.turns,run.gold,run.kills,run.daily})put(v);
 for(int v:std::initializer_list<int>{std::max(0,static_cast<int>(run.hp)),run.maxHp,run.xp,run.strength,run.torch,run.depth,run.x,run.y,run.level,run.regen,run.weapon,run.weaponPlus,run.armour,run.armourPlus,run.won,run.dead})put(v);
 for(auto v:run.map) { b.push_back(static_cast<uint8_t>(v)); }
 for(auto v:run.seen) { b.push_back(v); }
 for(auto v:run.colours) { b.push_back(v); }
 for(auto v:run.knownPotions) { b.push_back(v); }
 for(auto v:run.labels) { b.push_back(v); }
 for(auto v:run.knownScrolls) { b.push_back(v); }
 put(run.monsters.size());for(const auto&m:run.monsters){for(int v:std::initializer_list<int>{m.x,m.y,static_cast<int>(m.kind),m.hp,m.awake,m.sleep,m.lost})put(v);}
 auto items=[&](const std::vector<Item>&list){put(list.size());for(const auto&i:list)for(int v:std::initializer_list<int>{static_cast<int>(i.kind),i.type,i.plus,i.x,i.y,i.amount})put(v);};items(run.items);items(run.bag);put(digest(b,b.size()));return b;
}
bool Game::decode(const std::vector<uint8_t>&b){
 if(b.size()<4+24+64+Cells*2+22+12+4||b.size()>8192||b[0]!='D'||b[1]!='L'||b[2]!='V'||b[3]!=2)return false;
 size_t p=4;bool ok=true;auto get=[&](){if(p+4>b.size()){ok=false;return uint32_t(0);}uint32_t v=0;for(int k=0;k<4;++k)v|=uint32_t(b[p++])<<(8*k);return v;};
 Game g;auto&r=g.run;r.seed=get();r.rng=get();r.turns=get();r.gold=get();r.kills=get();r.daily=get();
 auto bounded=[&](uint32_t max){uint32_t v=get();if(v>max)ok=false;return v;};
 r.hp=bounded(1000);r.maxHp=bounded(1000);r.xp=bounded(30000);r.strength=bounded(100);r.torch=bounded(600);r.depth=bounded(10);r.x=bounded(Width-1);r.y=bounded(Height-1);r.level=bounded(50);r.regen=bounded(255);r.weapon=bounded(4);r.weaponPlus=bounded(50);r.armour=bounded(4);r.armourPlus=bounded(50);r.won=bounded(1);r.dead=bounded(1);if(!ok)return false;
 for(auto&v:r.map){if(p>=b.size())return false;v=static_cast<Tile>(b[p++]);if(v>Tile::Stairs)return false;}for(auto&v:r.seen){if(p>=b.size())return false;v=b[p++];if(v>1)return false;}
 for(auto&v:r.colours){if(p>=b.size())return false;v=b[p++];if(v>4)return false;}for(auto&v:r.knownPotions){if(p>=b.size())return false;v=b[p++];if(v>1)return false;}for(auto&v:r.labels){if(p>=b.size())return false;v=b[p++];if(v>25)return false;}for(auto&v:r.knownScrolls){if(p>=b.size())return false;v=b[p++];if(v>1)return false;}
 uint32_t n=get();if(n>16)return false;for(uint32_t i=0;i<n;++i){Monster m;m.x=bounded(Width-1);m.y=bounded(Height-1);m.kind=static_cast<Kind>(bounded(8));m.hp=bounded(1000);m.awake=bounded(1);m.sleep=bounded(12);m.lost=bounded(255);if(!ok)return false;if(!inside(m.x,m.y)||m.kind>Kind::Dragon||m.hp<1||m.hp>rule(m.kind).hp||m.awake>1||m.sleep>12||g.tile(m.x,m.y)==Tile::Rock||g.monsterAt(m.x,m.y)>=0||(m.x==r.x&&m.y==r.y))return false;r.monsters.push_back(m);}
 auto items=[&](std::vector<Item>&list,bool bag){uint32_t count=get();if(count>(bag?12u:32u))return false;for(uint32_t i=0;i<count;++i){Item it;it.kind=static_cast<ItemKind>(bounded(6));it.type=bounded(5);it.plus=bounded(50);it.x=bounded(Width-1);it.y=bounded(Height-1);it.amount=bounded(65535);if(!ok)return false;if(it.kind>ItemKind::Amulet||it.plus>50||!inside(it.x,it.y))return false;if(it.type>(it.kind==ItemKind::Scroll?5:4))return false;if(bag&&it.kind>ItemKind::Armour)return false;if(!bag&&(g.tile(it.x,it.y)==Tile::Rock||g.itemAt(it.x,it.y)>=0))return false;list.push_back(it);}return true;};
 if(!items(r.items,false)||!items(r.bag,true)) { return false; }
 size_t payload=p;uint32_t hash=get();if(!ok||p!=b.size()||hash!=digest(b,payload))return false;
 if(r.seed<1||r.seed>=2147483647||r.rng<1||r.rng>=2147483647||r.depth<1||r.depth>10||!inside(r.x,r.y)||g.tile(r.x,r.y)==Tile::Rock||r.maxHp<1||r.maxHp>1000||r.hp<0||r.hp>r.maxHp||r.level<1||r.level>50||r.weapon>4||r.armour>4||r.weaponPlus>50||r.armourPlus>50||r.strength<0||r.strength>100||r.torch<0||r.torch>600||r.won>1||r.dead>1||(r.won&&r.dead)||(!r.dead&&r.hp==0))return false;
 g.updateSight();g.say(Event::Welcome,r.depth);*this=std::move(g);return true;
}
}  // namespace delve
