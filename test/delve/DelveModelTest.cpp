#include "activities/games/delve/DelveModel.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <queue>
#include <stdexcept>
using namespace delve;
#define CHECK(x) do { if(!(x)) throw std::runtime_error(#x); } while(false)
static void arena(Game&g){g.start(42);g.run.map.fill(Tile::Rock);for(int y=1;y<Height-1;++y)for(int x=1;x<Width-1;++x)g.run.map[Game::index(x,y)]=Tile::Floor;g.run.x=7;g.run.y=9;g.run.monsters.clear();g.run.items.clear();g.updateSight();}
int main(){
 // Every generated floor is connected even with cardinal-only navigation.
 for(int seed=1;seed<=160;++seed){Game g;g.start(seed);for(int depth=1;depth<=10;++depth){g.enterLevel(depth);std::array<bool,Cells>seen{};std::queue<int>q;q.push(Game::index(g.run.x,g.run.y));seen[q.front()]=true;while(!q.empty()){int p=q.front();q.pop();for(auto v:std::array<std::array<int,2>,4>{{{{1,0}},{{-1,0}},{{0,1}},{{0,-1}}}}){int x=p%Width+v[0],y=p/Width+v[1];if(Game::inside(x,y)&&g.tile(x,y)!=Tile::Rock&&!seen[Game::index(x,y)]){seen[Game::index(x,y)]=true;q.push(Game::index(x,y));}}}int stairs=0;for(int p=0;p<Cells;++p){if(g.run.map[p]!=Tile::Rock)CHECK(seen[p]);if(g.run.map[p]==Tile::Stairs)++stairs;}CHECK(stairs==(depth<10?1:0));if(depth==10){CHECK(std::count_if(g.run.monsters.begin(),g.run.monsters.end(),[](const Monster&m){return m.kind==Kind::Dragon;})==1);CHECK(std::count_if(g.run.items.begin(),g.run.items.end(),[](const Item&i){return i.kind==ItemKind::Amulet;})==1);}}}
 Game a,b;a.start(8932,20261124);b.start(8932,20261124);CHECK(a.encode()==b.encode());
 Game g;arena(g);auto turns=g.run.turns;g.run.map[Game::index(8,9)]=Tile::Rock;CHECK(!g.step(1,0));CHECK(g.run.turns==turns);g.run.map[Game::index(8,9)]=Tile::Door;CHECK(g.step(1,0));CHECK(g.run.x==7);CHECK(g.tile(8,9)==Tile::Open);CHECK(g.step(1,0));CHECK(g.run.x==8);
 arena(g);g.run.hp=8;g.run.torch=0;for(int i=0;i<30;++i)g.wait();CHECK(g.run.hp==8);CHECK(g.radius()==1);g.run.torch=101;g.wait();CHECK(g.radius()==5);g.wait();CHECK(g.radius()==3);
 arena(g);g.run.bag={{ItemKind::Potion,0}};g.run.hp=2;CHECK(g.useItem(0));CHECK(g.run.hp>=17);CHECK(g.run.bag.empty());
 arena(g);g.run.bag={{ItemKind::Potion,4}};g.run.hp=3;CHECK(g.useItem(0));CHECK(g.run.dead);CHECK(!g.active());
 arena(g);g.run.bag={{ItemKind::Scroll,4}};CHECK(g.useItem(0));CHECK(g.run.seen[Game::index(1,1)]);
 arena(g);g.run.bag={{ItemKind::Scroll,5}};g.run.monsters.push_back({8,9,Kind::Orc,18,1});g.updateSight();CHECK(g.useItem(0));CHECK(g.run.monsters[0].sleep==11);CHECK(g.run.hp==20);
 arena(g);g.run.items.push_back({ItemKind::Torch,0,0,8,9});g.run.torch=100;CHECK(g.step(1,0));CHECK(g.run.torch==299);CHECK(g.run.items.empty());
 arena(g);g.run.depth=10;g.run.items.push_back({ItemKind::Amulet,0,0,8,9});CHECK(g.step(1,0));CHECK(g.run.won);
 arena(g);g.run.depth=10;g.run.items.push_back({ItemKind::Amulet,0,0,8,9});g.run.monsters.push_back({12,15,Kind::Dragon,60});CHECK(g.step(1,0));CHECK(g.run.won);CHECK(g.run.items.empty());
 arena(g);g.run.map[Game::index(7,9)]=Tile::Stairs;CHECK(g.descend());CHECK(g.run.depth==2);
 arena(g);g.run.monsters.push_back({8,9,Kind::Orc,18,1});g.updateSight();turns=g.run.turns;g.rest();CHECK(g.run.turns==turns);
 arena(g);g.run.bag={{ItemKind::Weapon,4,2}};CHECK(g.useItem(0));CHECK(g.run.weapon==4&&g.run.weaponPlus==2);CHECK(g.run.bag[0].kind==ItemKind::Weapon);CHECK(g.dropItem(0));CHECK(g.run.items.size()==1);
 for(int seed=1;seed<=100;++seed){g.start(seed);auto wire=g.encode();Game restored;CHECK(restored.decode(wire));CHECK(restored.encode()==wire);for(size_t i=0;i<wire.size();i+=13){auto broken=wire;broken[i]^=0x80;CHECK(!restored.decode(broken));}CHECK(!restored.decode(std::vector<uint8_t>(wire.begin(),wire.end()-1)));}
 // Finished runs stay finished after power loss; signed overkill is normalized.
 arena(g);g.run.hp=-7;g.run.dead=1;Game done;CHECK(done.decode(g.encode()));CHECK(done.run.dead&&done.run.hp==0&&!done.active());
 arena(g);g.run.won=1;CHECK(done.decode(g.encode()));CHECK(done.run.won&&!done.active());
 // Recompute checksum after deliberately out-of-range 32-bit fields: reject
 // before narrowing can turn 65556 into a valid short or 263 into a valid byte.
 arena(g);auto bad=g.encode();
 auto reseal=[](std::vector<uint8_t>&b){uint32_t h=2166136261u;for(size_t i=0;i<b.size()-4;++i)h=(h^b[i])*16777619u;for(int k=0;k<4;++k)b[b.size()-4+k]=(h>>(8*k))&255;};
 auto write32=[](std::vector<uint8_t>&b,size_t p,uint32_t v){for(int k=0;k<4;++k)b[p+k]=(v>>(8*k))&255;};
 write32(bad,28,65556);reseal(bad);CHECK(!done.decode(bad));
 bad=g.encode();write32(bad,52,263);reseal(bad);CHECK(!done.decode(bad));
 bad=g.encode();write32(bad,68,256);reseal(bad);CHECK(!done.decode(bad));
 std::cout<<"PASS: 1600 generated floors, deterministic daily seed, movement/doors, light/healing, items/combat effects, victory, rest, equipment and 100 save roundtrips/corruption tests\n";
}
