#pragma once
struct MappedInputManager {
  enum class Button { Back, Confirm, Left, Right, Up, Down, Power, PageBack, PageForward };
  bool isPressed(Button) const { return false; }
  bool wasAnyPressed() const { return false; }
  bool wasAnyReleased() const { return false; }
};
