#pragma once
#include <array>
#include <cstdint>
#include <deque>

class HalGPIO {
 public:
  struct ButtonEdge {
    uint8_t button;
    bool pressed;
    unsigned long timeMs;
  };
  std::deque<ButtonEdge> edges;
  std::array<bool, 9> held{};
  void edge(uint8_t button, bool pressed, unsigned long time) {
    held[button] = pressed;
    edges.push_back({button, pressed, time});
  }
  bool popButtonEdge(ButtonEdge& out) {
    if (edges.empty()) return false;
    out = edges.front();
    edges.pop_front();
    return true;
  }
  void flushButtonEdges() { edges.clear(); }
  void update() {}
  bool hasPendingInput() const { return !edges.empty(); }
};
