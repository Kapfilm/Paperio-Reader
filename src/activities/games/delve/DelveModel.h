#pragma once
// C++ adaptation of Delve by Omer Faruk Inel (MIT, see LICENSE-Delve.txt).
#include <array>
#include <cstdint>
#include <vector>

namespace delve {
constexpr int Width = 15, Height = 19, Cells = Width * Height, LastDepth = 10;
enum class Tile : uint8_t { Rock, Floor, Door, Open, Stairs };
enum class Kind : uint8_t { Rat, Bat, Snake, Goblin, Skeleton, Orc, Troll, Wraith, Dragon };
enum class ItemKind : uint8_t { Potion, Scroll, Weapon, Armour, Torch, Gold, Amulet };
enum class Event : uint8_t {
  Enter, Welcome, Blocked, Door, Stairs, Descend, LastFloor, Miss, Hit, Killed, EnemyMiss, EnemyHit,
  Stolen, TorchDrain, LowTorch, NoTorch, Gold, Torch, Picked, FullBag, Victory, Dead, LevelUp,
  Heal, ExtraHeal, Strength, Life, Poison, Teleport, WeaponGlow, ArmourGlow, Light, Map, Sleep,
  Equipped, Dropped, CannotDrop, Rested, UnsafeRest, NoStairs, GiveUp
};
struct Message { Event event = Event::Enter; int value = 0; int subject = 0; };
struct Monster {
  uint8_t x = 0, y = 0; Kind kind = Kind::Rat; int16_t hp = 0;
  uint8_t awake = 0, sleep = 0, lost = 0;
};
struct Item {
  ItemKind kind = ItemKind::Potion; uint8_t type = 0, plus = 0, x = 0, y = 0; uint16_t amount = 0;
};
struct MonsterRule {
  int hp, lo, hi, def, xp, from, to, patience, ignore;
  bool coward = false, erratic = false, thief = false, regen = false, dark = false, guard = false;
};
struct Run {
  uint32_t seed = 1, rng = 1, turns = 0, gold = 0, kills = 0, daily = 0;
  int16_t hp = 20, maxHp = 20, xp = 0, strength = 0, torch = 300;
  uint8_t depth = 1, x = 1, y = 1, level = 1, regen = 0, weapon = 0, weaponPlus = 0,
          armour = 0, armourPlus = 0;
  uint8_t won = 0, dead = 0;
  std::array<Tile, Cells> map{};
  std::array<uint8_t, Cells> seen{}, visible{};
  std::array<uint8_t, 5> colours{}, knownPotions{};
  std::array<uint8_t, 6> labels{}, knownScrolls{};
  std::vector<Monster> monsters;
  std::vector<Item> items, bag;
};
class Game {
 public:
  Run run;
  std::vector<Message> messages;
  static const MonsterRule& rule(Kind kind);
  static bool inside(int x, int y) { return x >= 0 && x < Width && y >= 0 && y < Height; }
  static int index(int x, int y) { return y * Width + x; }
  Tile tile(int x, int y) const;
  bool canStep(int x, int y, int dx, int dy) const;
  int monsterAt(int x, int y) const;
  int itemAt(int x, int y) const;
  int radius() const;
  int attackLow() const;
  int attackHigh() const;
  int defence() const;
  int hitChance() const;
  int visibleEnemies() const;
  bool active() const { return !run.dead && !run.won; }
  void start(uint32_t seed, uint32_t daily = 0);
  void enterLevel(int depth);
  bool step(int dx, int dy);
  bool descend();
  void wait();
  void rest();
  bool useItem(int item);
  bool dropItem(int item);
  void giveUp();
  void updateSight();
  std::array<int16_t, Cells> distances(int x, int y, bool seenOnly = false) const;
  std::vector<uint8_t> encode() const;
  bool decode(const std::vector<uint8_t>& data);
 private:
  uint32_t next();
  int random(int lo, int hi);
  bool chance(int pct);
  void say(Event event, int value = 0, int subject = 0);
  void endTurn();
  void monstersAct();
  void attackMonster(int n);
  void attackHero(int n);
  void gainXp(int amount);
  void pickup();
  bool clearLine(int x, int y) const;
};
}  // namespace delve
