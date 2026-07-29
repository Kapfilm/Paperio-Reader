#pragma once

#include "ClippingStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class EpubReaderClippingsActivity final : public Activity {
 public:
  EpubReaderClippingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, ClippingStore& store)
      : Activity("EpubClippings", renderer, mappedInput), store(store) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }

 private:
  ClippingStore& store;
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  std::string itemLabel(int index) const;
};
