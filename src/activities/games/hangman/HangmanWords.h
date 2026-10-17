#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

enum class HangmanLanguage : uint8_t { English = 0, Russian = 1 };
enum class HangmanCategory : uint8_t { Animals = 0, Nature = 1, Objects = 2 };
enum class HangmanDifficulty : uint8_t { Easy = 0, Hard = 1 };

struct HangmanWord {
  const char* word;
  const char* hint;
};

size_t hangmanAlphabetSize(HangmanLanguage language);
std::string hangmanAlphabetLetter(HangmanLanguage language, size_t index);
size_t hangmanWordCount(HangmanLanguage language, HangmanCategory category, HangmanDifficulty difficulty);
const HangmanWord& hangmanWordAt(HangmanLanguage language, HangmanCategory category, HangmanDifficulty difficulty,
                                 size_t index);

uint32_t hangmanUtf8Codepoint(const char* text);
size_t hangmanUtf8Length(const char* text);
