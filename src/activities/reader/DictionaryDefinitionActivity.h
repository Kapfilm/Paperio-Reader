#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"

class DictionaryDefinitionActivity final : public Activity {
 public:
  DictionaryDefinitionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string word)
      : Activity("DictionaryDefinition", renderer, mappedInput), query(std::move(word)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }

 private:
  enum class State : uint8_t { Loading, Ready, Error };
  struct Line {
    uint32_t start = 0;
    uint16_t length = 0;
  };

  void wrapDefinition();
  void sanitizeDefinition();
  int measure(const char* text, size_t length) const;

  std::string query;
  std::string headword;
  std::string dictionaryName;
  std::string definition;
  std::vector<Line> lines;
  State state = State::Loading;
  const char* errorMessage = nullptr;
  int currentPage = 0;
  int totalPages = 1;
  int linesPerPage = 1;
};
