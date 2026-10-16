#pragma once

class SokobanStore {
 public:
  static bool saveLevel(int level);
  static bool loadLevel(int& level);
};
