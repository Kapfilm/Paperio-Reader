#include "HangmanWords.h"

#include <I18n.h>

#include <algorithm>
#include <iterator>

namespace {

constexpr HangmanWord kEnglishAnimals[] = {
    {"ELEPHANT", "Largest land animal"}, {"PENGUIN", "A bird that swims"},
    {"DOLPHIN", "Clever sea mammal"},    {"HAMSTER", "Small pet with cheek pouches"},
    {"GIRAFFE", "The tallest animal"},   {"OCTOPUS", "Sea animal with eight arms"},
};
constexpr HangmanWord kEnglishNature[] = {
    {"VOLCANO", "A mountain that erupts"},    {"RAINBOW", "Colours after rain"},
    {"OCEAN", "Vast body of salt water"},     {"FOREST", "A large area of trees"},
    {"THUNDER", "The sound after lightning"}, {"GLACIER", "A slowly moving river of ice"},
};
constexpr HangmanWord kEnglishObjects[] = {
    {"COMPASS", "Shows directions"},           {"LANTERN", "A portable light"},
    {"KEYBOARD", "Keys used for typing"},      {"TELESCOPE", "Makes distant things look near"},
    {"BACKPACK", "A bag carried on the back"}, {"UMBRELLA", "Used when it rains"},
};

constexpr HangmanWord kEnglishAnimalsHard[] = {
    {"AXOLOTL", "Amphibian that keeps its juvenile features"},
    {"PANGOLIN", "Mammal protected by overlapping scales"},
    {"CASSOWARY", "Large flightless bird with a casque"},
    {"NARWHAL", "Arctic whale with a long tusk"},
    {"CHINCHILLA", "Andean rodent with extremely dense fur"},
    {"ORANGUTAN", "Great ape native to Asian rainforests"},
    {"ARMADILLO", "Mammal covered with bony armour"},
    {"PLATYPUS", "Egg-laying mammal with a broad bill"},
};
constexpr HangmanWord kEnglishNatureHard[] = {
    {"PERMAFROST", "Ground that remains frozen for years"},
    {"EQUINOX", "Day when light and darkness are nearly equal"},
    {"ESTUARY", "Where a river meets the sea"},
    {"ARCHIPELAGO", "A group of islands"},
    {"AVALANCHE", "A fast-moving mass descending a mountain"},
    {"GEYSER", "A hot spring that erupts intermittently"},
    {"TUNDRA", "Treeless biome of the far north"},
    {"BIOSPHERE", "All regions of Earth where life exists"},
};
constexpr HangmanWord kEnglishObjectsHard[] = {
    {"BAROMETER", "Instrument that measures air pressure"},
    {"PERISCOPE", "Optical device used to see over obstacles"},
    {"ABACUS", "Ancient calculating frame with beads"},
    {"COMPRESSOR", "Machine that raises the pressure of a gas"},
    {"CARABINER", "Metal loop with a spring-loaded gate"},
    {"METRONOME", "Device that marks a steady musical beat"},
    {"SEXTANT", "Navigation instrument using celestial angles"},
    {"KALEIDOSCOPE", "Tube that forms changing symmetrical patterns"},
};

constexpr HangmanWord kRussianAnimals[] = {
    {"СЛОН", "Самое крупное животное на суше"}, {"ПИНГВИН", "Птица, которая отлично плавает"},
    {"ДЕЛЬФИН", "Умное морское млекопитающее"}, {"ХОМЯК", "Питомец с защёчными мешками"},
    {"ЖИРАФ", "Самое высокое животное"},        {"ОСЬМИНОГ", "Морское животное с восемью щупальцами"},
};
constexpr HangmanWord kRussianNature[] = {
    {"ВУЛКАН", "Гора, которая извергается"},      {"РАДУГА", "Цветная дуга после дождя"},
    {"ОКЕАН", "Огромный водоём с солёной водой"}, {"ЛЕС", "Большая территория, покрытая деревьями"},
    {"ГРОЗА", "Дождь с громом и молниями"},       {"ЛЕДНИК", "Медленно движущаяся масса льда"},
};
constexpr HangmanWord kRussianObjects[] = {
    {"КОМПАС", "Показывает стороны света"},       {"ФОНАРЬ", "Переносной источник света"},
    {"КЛАВИАТУРА", "Клавиши для набора текста"},  {"ТЕЛЕСКОП", "Приближает далёкие объекты"},
    {"РЮКЗАК", "Сумка, которую носят за спиной"}, {"ЗОНТ", "Защищает от дождя"},
};

constexpr HangmanWord kRussianAnimalsHard[] = {
    {"АКСОЛОТЛЬ", "Земноводное, сохраняющее признаки личинки"},
    {"ПАНГОЛИН", "Млекопитающее, покрытое крупными чешуйками"},
    {"КАЗУАР", "Крупная нелетающая птица с шлемом на голове"},
    {"НАРВАЛ", "Арктический кит с длинным бивнем"},
    {"ШИНШИЛЛА", "Горный грызун с очень густым мехом"},
    {"ОРАНГУТАН", "Человекообразная обезьяна из лесов Азии"},
    {"БРОНЕНОСЕЦ", "Млекопитающее с костяным панцирем"},
    {"УТКОНОС", "Яйцекладущее млекопитающее с широким клювом"},
};
constexpr HangmanWord kRussianNatureHard[] = {
    {"МЕРЗЛОТА", "Грунт, остающийся замёрзшим много лет"},
    {"РАВНОДЕНСТВИЕ", "День, когда светлое и тёмное время почти равны"},
    {"ЭСТУАРИЙ", "Устье реки, расширяющееся к морю"},
    {"АРХИПЕЛАГ", "Группа близко расположенных островов"},
    {"ЛАВИНА", "Масса снега, быстро сходящая со склона"},
    {"ГЕЙЗЕР", "Источник, периодически выбрасывающий горячую воду"},
    {"ТУНДРА", "Безлесная природная зона Крайнего Севера"},
    {"БИОСФЕРА", "Область планеты, где существует жизнь"},
};
constexpr HangmanWord kRussianObjectsHard[] = {
    {"БАРОМЕТР", "Прибор для измерения атмосферного давления"},
    {"ПЕРИСКОП", "Оптический прибор для наблюдения из укрытия"},
    {"СЧЁТЫ", "Старинное приспособление для вычислений"},
    {"КОМПРЕССОР", "Машина для сжатия и подачи газа"},
    {"КАРАБИН", "Металлическая петля с подпружиненной защёлкой"},
    {"МЕТРОНОМ", "Прибор, задающий равномерный музыкальный темп"},
    {"СЕКСТАНТ", "Навигационный прибор для измерения углов"},
    {"КАЛЕЙДОСКОП", "Трубка с меняющимися симметричными узорами"},
};

template <size_t N>
constexpr size_t countOf(const HangmanWord (&)[N]) {
  return N;
}

const HangmanWord* wordsFor(HangmanLanguage language, HangmanCategory category, HangmanDifficulty difficulty,
                            size_t& count) {
  if (difficulty == HangmanDifficulty::Hard) {
    if (language == HangmanLanguage::Russian) {
      switch (category) {
        case HangmanCategory::Animals:
          count = countOf(kRussianAnimalsHard);
          return kRussianAnimalsHard;
        case HangmanCategory::Nature:
          count = countOf(kRussianNatureHard);
          return kRussianNatureHard;
        case HangmanCategory::Objects:
          count = countOf(kRussianObjectsHard);
          return kRussianObjectsHard;
      }
    }
    switch (category) {
      case HangmanCategory::Animals:
        count = countOf(kEnglishAnimalsHard);
        return kEnglishAnimalsHard;
      case HangmanCategory::Nature:
        count = countOf(kEnglishNatureHard);
        return kEnglishNatureHard;
      case HangmanCategory::Objects:
        count = countOf(kEnglishObjectsHard);
        return kEnglishObjectsHard;
    }
  }
  if (language == HangmanLanguage::Russian) {
    switch (category) {
      case HangmanCategory::Animals:
        count = countOf(kRussianAnimals);
        return kRussianAnimals;
      case HangmanCategory::Nature:
        count = countOf(kRussianNature);
        return kRussianNature;
      case HangmanCategory::Objects:
        count = countOf(kRussianObjects);
        return kRussianObjects;
    }
  }
  switch (category) {
    case HangmanCategory::Animals:
      count = countOf(kEnglishAnimals);
      return kEnglishAnimals;
    case HangmanCategory::Nature:
      count = countOf(kEnglishNature);
      return kEnglishNature;
    case HangmanCategory::Objects:
      count = countOf(kEnglishObjects);
      return kEnglishObjects;
  }
  count = countOf(kEnglishAnimals);
  return kEnglishAnimals;
}

}  // namespace

size_t hangmanAlphabetSize(const HangmanLanguage language) {
  // Keep the lowercase characters used by both hint dictionaries in every
  // interface-language font cache, even when the selected game language is
  // different from the firmware language.
  (void)tr(STR_HANGMAN_WORD_CHARACTERS);
  const char* alphabet =
      language == HangmanLanguage::Russian ? tr(STR_HANGMAN_ALPHABET_RUSSIAN) : tr(STR_HANGMAN_ALPHABET_ENGLISH);
  return hangmanUtf8Length(alphabet);
}

std::string hangmanAlphabetLetter(const HangmanLanguage language, const size_t index) {
  const char* alphabet =
      language == HangmanLanguage::Russian ? tr(STR_HANGMAN_ALPHABET_RUSSIAN) : tr(STR_HANGMAN_ALPHABET_ENGLISH);
  const auto* cursor = reinterpret_cast<const uint8_t*>(alphabet);
  size_t current = 0;
  while (*cursor != 0 && current < index) {
    if ((*cursor & 0x80) == 0) {
      ++cursor;
    } else if ((*cursor & 0xE0) == 0xC0 && cursor[1] != 0) {
      cursor += 2;
    } else if ((*cursor & 0xF0) == 0xE0 && cursor[1] != 0 && cursor[2] != 0) {
      cursor += 3;
    } else if ((*cursor & 0xF8) == 0xF0 && cursor[1] != 0 && cursor[2] != 0 && cursor[3] != 0) {
      cursor += 4;
    } else {
      ++cursor;
    }
    ++current;
  }
  if (*cursor == 0) return {};
  size_t bytes = 1;
  if ((*cursor & 0xE0) == 0xC0) bytes = 2;
  if ((*cursor & 0xF0) == 0xE0) bytes = 3;
  if ((*cursor & 0xF8) == 0xF0) bytes = 4;
  return std::string(reinterpret_cast<const char*>(cursor), bytes);
}

size_t hangmanWordCount(const HangmanLanguage language, const HangmanCategory category,
                        const HangmanDifficulty difficulty) {
  size_t count = 0;
  wordsFor(language, category, difficulty, count);
  return count;
}

const HangmanWord& hangmanWordAt(const HangmanLanguage language, const HangmanCategory category,
                                 const HangmanDifficulty difficulty, size_t index) {
  size_t count = 0;
  const HangmanWord* words = wordsFor(language, category, difficulty, count);
  index = count == 0 ? 0 : std::min(index, count - 1);
  return words[index];
}

uint32_t hangmanUtf8Codepoint(const char* text) {
  if (text == nullptr) return 0;
  const auto* s = reinterpret_cast<const uint8_t*>(text);
  if (s[0] < 0x80) return s[0];
  if ((s[0] & 0xE0) == 0xC0 && s[1] != 0) return ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
  if ((s[0] & 0xF0) == 0xE0 && s[1] != 0 && s[2] != 0) {
    return ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
  }
  if ((s[0] & 0xF8) == 0xF0 && s[1] != 0 && s[2] != 0 && s[3] != 0) {
    return ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
  }
  return 0;
}

size_t hangmanUtf8Length(const char* text) {
  if (text == nullptr) return 0;
  size_t length = 0;
  const auto* s = reinterpret_cast<const uint8_t*>(text);
  while (*s != 0) {
    if ((*s & 0x80) == 0) {
      ++s;
    } else if ((*s & 0xE0) == 0xC0 && s[1] != 0) {
      s += 2;
    } else if ((*s & 0xF0) == 0xE0 && s[1] != 0 && s[2] != 0) {
      s += 3;
    } else if ((*s & 0xF8) == 0xF0 && s[1] != 0 && s[2] != 0 && s[3] != 0) {
      s += 4;
    } else {
      ++s;
    }
    ++length;
  }
  return length;
}
