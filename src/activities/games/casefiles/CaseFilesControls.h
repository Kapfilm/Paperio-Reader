#pragma once
#include <cstdint>
namespace casefiles {
// A hold emits once; its release never also triggers the short action.
class HoldButton {
 public:
  enum class Event { None, Short, Long };
  Event update(bool pressed,bool down,bool released,uint32_t now) {
    if(pressed){started=now;tracking=true;handled=false;}
    if(tracking&&down&&!handled&&uint32_t(now-started)>=650){handled=true;return Event::Long;}
    if(released){bool shortPress=tracking&&!handled;reset();return shortPress?Event::Short:Event::None;}
    return Event::None;
  }
  void reset(){tracking=handled=false;}
 private:uint32_t started=0;bool tracking=false,handled=false;
};
}
