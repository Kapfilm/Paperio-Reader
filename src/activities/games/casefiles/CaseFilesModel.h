#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace casefiles {
struct Entity {uint8_t group=0,slot=0;};
struct Clue {char type='=';uint8_t count=0,group=0,difference=0;std::array<Entity,4> entities{};};
struct Group {uint8_t kind=0;std::array<uint8_t,5> items{},owner{},slot{};};
struct Puzzle {
 uint8_t theme=0,k=0,n=0;std::array<Group,4> groups{};std::vector<Clue> clues;
 bool parse(const char* line);
 int who(Entity e)const{return groups[e.group].owner[e.slot];}
 bool clueTrue(const Clue& c) const;
};
struct Pair{uint8_t a=0,b=0;};
class Model {
 public:
 enum Mark:uint8_t{Blank,Cross,Tick,Hint};
 Puzzle puzzle;int level=0,index=0,table=0,cursor=0;
 std::array<Pair,6> pairs{};int pairCount=0;
 std::array<std::array<uint8_t,25>,6> marks{};
 std::array<uint8_t,12> struck{};std::array<uint8_t,75> completed{};
 std::array<uint8_t,3> positions{};
 struct Change{uint8_t table,cell,before;};std::vector<Change> history;
 bool open(int newLevel,int newIndex);void reset(bool clearCompleted = true);bool belongs(int pi,int a,int b)const;
 bool cycle();bool undo();bool hint();int wrongCount()const;bool solved()const;
 bool ruledOut(int pi,int a,int b)const;bool conflict(int pi,int a,int b)const;
 void noteSolved();int completedCount(int lev)const;bool isCompleted(int lev,int idx)const;
 std::vector<uint8_t> encode() const;bool decode(const std::vector<uint8_t>& bytes);
 private:void setMark(int pi,int cell,uint8_t value);
};
}
