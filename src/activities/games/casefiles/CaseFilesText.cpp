#include "CaseFilesText.h"
#include "CaseFilesTextData.h"
#include <initializer_list>
#include <utility>
namespace casefiles {
namespace {
std::string fill(StrId id,std::initializer_list<std::string> values){
 std::string out=I18N.get(id);size_t pos=0;
 for(const auto&value:values){pos=out.find("%s",pos);if(pos==std::string::npos)break;out.replace(pos,2,value);pos+=value.size();}
 return out;
}
const NaturalGroupText& phrases(const Puzzle&p,int g){return naturalGroups[p.theme][p.groups[g].kind-1];}
const PersonText& personPhrases(const Puzzle&p,Entity e){return phrases(p,e.group).items[p.groups[e.group].items[e.slot]];}
std::string subject(const Puzzle&p,Entity e){return e.group?I18N.get(personPhrases(p,e).person):item(p,e.group,e.slot);}
std::string predicate(const Puzzle&p,Entity e,bool negative=false){
 if(!e.group)return fill(negative?StrId::STR_CASE_NAT_NAME_NEG:StrId::STR_CASE_NAT_NAME_PRED,{item(p,e.group,e.slot)});
 const auto&text=personPhrases(p,e);return I18N.get(negative?text.negative:text.affirmative);
}
std::string amount(const NaturalGroupText&g,int n){
 StrId unit=n==1?g.unitOne:(n>=2&&n<=4?g.unitFew:g.unitMany);
 return std::to_string(n)+" "+I18N.get(unit);
}
}
const char* themeTitle(const Puzzle&p){return I18N.get(themes[p.theme].title);}
const char* themeStory(const Puzzle&p){return I18N.get(themes[p.theme].story);}
const char* category(const Puzzle&p,int g){return g?I18N.get(themes[p.theme].categories[p.groups[g].kind-1]):I18N.get(StrId::STR_CASE_NAME);}
const char* item(const Puzzle&p,int g,int s){int i=p.groups[g].items[s];return g?I18N.get(themes[p.theme].items[p.groups[g].kind-1][i]):I18N.get(themes[p.theme].names[i]);}
std::string entity(const Puzzle&p,Entity e){
 auto out=subject(p,e);
 // Russian relative descriptions carry a closing comma when used as the
 // subject of a sentence. A stand-alone description does not need it.
 if(!out.empty()&&out.back()==',')out.pop_back();
 // Descriptive nouns follow "than" or a colon; personal names retain their case.
 if(e.group && out.size()>=2){
  auto a=static_cast<unsigned char>(out[0]),b=static_cast<unsigned char>(out[1]);
  if(a==0xD0 && b>=0x90 && b<=0x9F)out[1]=static_cast<char>(b+0x20);
  else if(a==0xD0 && b>=0xA0 && b<=0xAF){out[0]=static_cast<char>(0xD1);out[1]=static_cast<char>(b-0x20);}
 }
 return out;
}
std::string clueText(const Puzzle&p,const Clue&c){
 Entity a=c.entities[0],b=c.entities[1];
 if(c.type=='='||c.type=='!'){
  if(b.group==0)std::swap(a,b);
  return subject(p,a)+" "+predicate(p,b,c.type=='!')+".";
 }
 if(c.type=='e'){
  const auto first=subject(p,a)+" "+predicate(p,b);
  const auto second=subject(p,a)+" "+predicate(p,c.entities[2]);
  return fill(StrId::STR_CASE_NAT_XOR,{first,second});
 }
 if(c.type=='o'){
  auto first=entity(p,a);
  const auto description=subject(p,a);
  if(!description.empty()&&description.back()==',')first+=',';
  return fill(StrId::STR_CASE_NAT_PAIR,{first,entity(p,b),predicate(p,c.entities[2]),predicate(p,c.entities[3])});
 }
 const auto&g=phrases(p,c.group);
 const auto relation=c.type=='<'?std::string(I18N.get(g.less)):fill(g.after,{amount(g,c.difference)});
 return subject(p,a)+" "+relation+" "+entity(p,b)+".";
}
}
