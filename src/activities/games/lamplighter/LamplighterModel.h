#pragma once
#include <array>
#include <cstdint>
#include <vector>
namespace lamplighter {
class Model {
 public:
  static constexpr int Count=200, MaxCells=81, MaxHistory=500;
  struct Turn { uint8_t cell, mask; };
  uint8_t level=0; uint16_t puzzle=0; uint32_t moves=0;
  std::array<uint8_t,MaxCells> tiles{},locks{};
  std::array<uint8_t,75> completed{};
  std::array<uint16_t,3> last{};
  std::vector<Turn> history;
  Model(){open(0,0);}
  static uint8_t rotateMask(uint8_t m){return ((m<<1)|(m>>3))&15;}
  int size()const{return 5+2*level;}
  int cells()const{return size()*size();}
  int source()const;
  uint8_t solution(int i)const;
  uint8_t initial(int i)const;
  void open(int difficulty,int index);
  void restart();
  std::array<bool,MaxCells> lit()const;
  bool solved()const;
  bool rotate(int i);
  bool toggleLock(int i);
  bool undo();
  int hint();
  int lampCount(bool onlyLit)const;
  bool wasCompleted(int difficulty,int index)const;
  int completedCount(int difficulty)const;
  void recordWin();
  std::vector<uint8_t> encode()const;
  bool decode(const std::vector<uint8_t>& bytes);
 private:
  const uint8_t* row()const;
  void remember(int i);
};
}
