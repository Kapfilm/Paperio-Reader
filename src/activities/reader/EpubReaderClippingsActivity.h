#pragma once

#include <Epub.h>

#include <string>
#include <vector>

#include "ClippingStore.h"
#include "activities/Activity.h"

class EpubReaderClippingsActivity final : public Activity {
 public:
  EpubReaderClippingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, ClippingStore& store,
                              const Epub& epub)
      : Activity("EpubClippings", renderer, mappedInput), store(store), epub(epub) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }

 private:
  ClippingStore& store;
  const Epub& epub;
  std::vector<std::string> chapterTitles;
  int selectedIndex = 0;
};
