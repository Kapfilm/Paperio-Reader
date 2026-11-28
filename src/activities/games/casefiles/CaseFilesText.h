#pragma once
#include "CaseFilesModel.h"
#include <string>
namespace casefiles {
const char* themeTitle(const Puzzle&p);const char* themeStory(const Puzzle&p);
const char* category(const Puzzle&p,int g);const char* item(const Puzzle&p,int g,int s);
std::string entity(const Puzzle&p,Entity e);std::string clueText(const Puzzle&p,const Clue&c);
}
