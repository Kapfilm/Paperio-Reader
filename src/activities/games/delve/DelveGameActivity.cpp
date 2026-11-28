#include "DelveGameActivity.h"
#include "DelveSprites.h"
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Preferences.h>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include "components/UITheme.h"
#include "UiFonts.h"
#include "fontIds.h"

namespace {
constexpr const char* slots[] = {"/.crosspoint/delve-a.bin", "/.crosspoint/delve-b.bin"};
const uint16_t* monsterArt(delve::Kind k) {
  const uint16_t* art[]={delve_art::rat,delve_art::bat,delve_art::snake,delve_art::goblin,delve_art::skeleton,delve_art::orc,delve_art::troll,delve_art::wraith,delve_art::dragon};
  return art[static_cast<int>(k)];
}
const uint16_t* itemArt(delve::ItemKind k) {
  const uint16_t* art[]={delve_art::potion,delve_art::scroll,delve_art::sword,delve_art::armour,delve_art::torch,delve_art::gold,delve_art::amulet};
  return art[static_cast<int>(k)];
}
void sprite(const GfxRenderer&r,const uint16_t* art,int x,int y,int size) {
  for(int py=0;py<size;++py)for(int px=0;px<size;++px)
    if(art[py*16/size]&(0x8000>>(px*16/size)))r.drawPixel(x+px,y+py);
}
uint32_t checksum(const std::vector<uint8_t>&b,size_t n){uint32_t h=2166136261u;for(size_t i=0;i<n;++i)h=(h^b[i])*16777619u;return h;}
void put(std::vector<uint8_t>&b,uint32_t v){for(int k=0;k<4;++k)b.push_back((v>>(k*8))&255);}
uint32_t get(const std::vector<uint8_t>&b,size_t p){uint32_t v=0;for(int k=0;k<4;++k)v|=uint32_t(b[p+k])<<(8*k);return v;}
std::string format(const char* pattern,int a,int b=0,int c=0,int d=0){char s[192];snprintf(s,sizeof(s),pattern,a,b,c,d);return s;}
}
void DelveGameActivity::onEnter(){Activity::onEnter();renderer.setOrientation(GfxRenderer::Orientation::Portrait);loadView();load();requestUpdate();}
void DelveGameActivity::loadView() {
 Preferences prefs;
 if (prefs.begin("delve-ui", false)) {
  largeView = prefs.getUChar("scale", 0) == 1;
  prefs.end();
 }
}
void DelveGameActivity::saveView() {
 Preferences prefs;
 viewSaved = prefs.begin("delve-ui", false);
 if (viewSaved) {
  viewSaved = prefs.putUChar("scale", largeView ? 1 : 0) == 1;
  prefs.end();
 }
}
void DelveGameActivity::onExit(){if(haveRun)save();Activity::onExit();}
void DelveGameActivity::load(){
 bool found=false;
 for(const char*path:slots){HalFile f;if(!Storage.openFileForRead("DLV",path,f))continue;auto size=f.size();if(size<24||size>8300)continue;std::vector<uint8_t>b(size);if(f.read(b.data(),b.size())!=static_cast<int>(b.size()))continue;
  if(b[0]!='D'||b[1]!='S'||b[2]!='V'||b[3]!=1||checksum(b,b.size()-4)!=get(b,b.size()-4))continue;
  uint32_t seq=get(b,4),nr=get(b,8),nw=get(b,12),nd=get(b,16);if(nr>100000000||nw>nr||nd>10)continue;
  delve::Game candidate;std::vector<uint8_t>payload(b.begin()+20,b.end()-4);if(!candidate.decode(payload))continue;
  if(!found||static_cast<int32_t>(seq-sequence)>0){found=true;sequence=seq;runs=nr;wins=nw;deepest=nd;game=std::move(candidate);}
 }
 haveRun=found;savedAvailable=found&&game.active();
}
void DelveGameActivity::save(){
 if(!haveRun)return;
 if(!Storage.exists("/.crosspoint")&&!Storage.mkdir("/.crosspoint")){saveOk=false;return;}
 auto payload=game.encode();std::vector<uint8_t>b{'D','S','V',1};uint32_t nextSequence=sequence+1;
 put(b,nextSequence);put(b,runs);put(b,wins);put(b,deepest);b.insert(b.end(),payload.begin(),payload.end());put(b,checksum(b,b.size()));
 HalFile f;if(!Storage.openFileForWrite("DLV",slots[nextSequence%2],f)){saveOk=false;return;}
 saveOk=f.write(b.data(),b.size())==b.size();f.flush();const bool closed=f.close();saveOk=saveOk&&closed;if(saveOk){sequence=nextSequence;savedAvailable=game.active();}
}
void DelveGameActivity::newGame(bool daily){
 uint32_t day=0;if(daily){time_t now=HalClock::now();if(now>0){tm date{};localtime_r(&now,&date);day=(date.tm_year+1900)*10000+(date.tm_mon+1)*100+date.tm_mday;}else{saveOk=false;screen=Screen::Title;requestUpdate();return;}}
 uint32_t seed=daily?day:static_cast<uint32_t>(micros())^(runs*7919u)^static_cast<uint32_t>(time(nullptr));
 game.start(seed,day);haveRun=true;++runs;deepest=std::max<uint32_t>(deepest,1);screen=Screen::Playing;selected=0;save();requestUpdate();
}
void DelveGameActivity::changed(){
 deepest=std::max<uint32_t>(deepest,game.run.depth);
 if(!game.active()){if(game.run.won)++wins;screen=Screen::End;selected=0;save();}
 else if(game.run.turns%10==0)save();
 requestUpdate();
}
const char* DelveGameActivity::monsterName(delve::Kind k)const{
 constexpr StrId ids[]={StrId::STR_DELVE_RAT,StrId::STR_DELVE_BAT,StrId::STR_DELVE_SNAKE,StrId::STR_DELVE_GOBLIN,StrId::STR_DELVE_SKELETON,StrId::STR_DELVE_ORC,StrId::STR_DELVE_TROLL,StrId::STR_DELVE_WRAITH,StrId::STR_DELVE_DRAGON};return I18N.get(ids[static_cast<int>(k)]);
}
std::string DelveGameActivity::itemName(const delve::Item&i)const{
 constexpr StrId pot[]={StrId::STR_DELVE_P_HEAL,StrId::STR_DELVE_P_EXTRA,StrId::STR_DELVE_P_STRENGTH,StrId::STR_DELVE_P_LIFE,StrId::STR_DELVE_P_POISON};
 constexpr StrId scroll[]={StrId::STR_DELVE_S_TELEPORT,StrId::STR_DELVE_S_WEAPON,StrId::STR_DELVE_S_ARMOUR,StrId::STR_DELVE_S_LIGHT,StrId::STR_DELVE_S_MAP,StrId::STR_DELVE_S_SLEEP};
 constexpr StrId colours[]={StrId::STR_DELVE_RED,StrId::STR_DELVE_BLUE,StrId::STR_DELVE_GREEN,StrId::STR_DELVE_YELLOW,StrId::STR_DELVE_MURKY};
 constexpr StrId weapons[]={StrId::STR_DELVE_DAGGER,StrId::STR_DELVE_SWORD,StrId::STR_DELVE_MACE,StrId::STR_DELVE_AXE,StrId::STR_DELVE_GREATSWORD};
 constexpr StrId armour[]={StrId::STR_DELVE_LEATHER,StrId::STR_DELVE_STUDDED,StrId::STR_DELVE_CHAIN,StrId::STR_DELVE_SPLINT,StrId::STR_DELVE_PLATE};
 switch(i.kind){
 case delve::ItemKind::Potion:return game.run.knownPotions[i.type]?I18N.get(pot[i.type]):std::string(tr(STR_DELVE_UNKNOWN_POTION))+" "+I18N.get(colours[game.run.colours[i.type]]);
 case delve::ItemKind::Scroll:return game.run.knownScrolls[i.type]?I18N.get(scroll[i.type]):std::string(tr(STR_DELVE_UNKNOWN_SCROLL))+" "+char('A'+game.run.labels[i.type]);
 case delve::ItemKind::Weapon:return std::string(I18N.get(weapons[i.type]))+(i.plus?" +"+std::to_string(i.plus):"");
 case delve::ItemKind::Armour:return std::string(I18N.get(armour[i.type]))+(i.plus?" +"+std::to_string(i.plus):"");
 case delve::ItemKind::Torch:return tr(STR_DELVE_TORCH);
 case delve::ItemKind::Gold:return tr(STR_DELVE_GOLD);
 case delve::ItemKind::Amulet:return tr(STR_DELVE_AMULET);
 }return {};
}
std::string DelveGameActivity::messageText(const delve::Message&m)const{
 using E=delve::Event;
 switch(m.event){
 case E::Enter:return tr(STR_DELVE_MSG_ENTER);case E::Welcome:return tr(STR_DELVE_MSG_WELCOME);case E::Blocked:return tr(STR_DELVE_MSG_BLOCKED);case E::Door:return tr(STR_DELVE_MSG_DOOR);case E::Stairs:return tr(STR_DELVE_MSG_STAIRS);case E::Descend:return format(tr(STR_DELVE_MSG_DESCEND),m.value);case E::LastFloor:return tr(STR_DELVE_MSG_LAST);case E::Miss:return tr(STR_DELVE_MSG_MISS);case E::Hit:return format(tr(STR_DELVE_MSG_HIT),m.value);case E::Killed:return std::string(monsterName(static_cast<delve::Kind>(m.subject)))+": "+tr(STR_DELVE_MSG_KILLED);case E::EnemyMiss:return tr(STR_DELVE_MSG_ENEMY_MISS);case E::EnemyHit:return format(tr(STR_DELVE_MSG_ENEMY_HIT),m.value);case E::Stolen:return format(tr(STR_DELVE_MSG_STOLEN),m.value);case E::TorchDrain:return tr(STR_DELVE_MSG_DRAIN);case E::LowTorch:return tr(STR_DELVE_MSG_LOW);case E::NoTorch:return tr(STR_DELVE_MSG_DARK);case E::Gold:return format(tr(STR_DELVE_MSG_GOLD),m.value);case E::Torch:return tr(STR_DELVE_MSG_TORCH);case E::Picked:{delve::Item i;i.kind=static_cast<delve::ItemKind>(m.subject);i.type=m.value;return std::string(tr(STR_DELVE_MSG_PICKED))+" "+itemName(i);}case E::FullBag:return tr(STR_DELVE_MSG_FULL);case E::Victory:return tr(STR_DELVE_MSG_VICTORY);case E::Dead:return tr(STR_DELVE_MSG_DEAD);case E::LevelUp:return format(tr(STR_DELVE_MSG_LEVEL),m.value);case E::Heal:return tr(STR_DELVE_MSG_HEAL);case E::ExtraHeal:return tr(STR_DELVE_MSG_EXTRA);case E::Strength:return tr(STR_DELVE_MSG_STRONG);case E::Life:return tr(STR_DELVE_MSG_LIFE);case E::Poison:return tr(STR_DELVE_MSG_POISON);case E::Teleport:return tr(STR_DELVE_MSG_TELEPORT);case E::WeaponGlow:return tr(STR_DELVE_MSG_WEAPON);case E::ArmourGlow:return tr(STR_DELVE_MSG_ARMOUR);case E::Light:return tr(STR_DELVE_MSG_LIGHT);case E::Map:return tr(STR_DELVE_MSG_MAP);case E::Sleep:return tr(STR_DELVE_MSG_SLEEP);case E::Equipped:return tr(STR_DELVE_MSG_EQUIP);case E::Dropped:return tr(STR_DELVE_MSG_DROP);case E::CannotDrop:return tr(STR_DELVE_MSG_NODROP);case E::Rested:return format(tr(STR_DELVE_MSG_REST),m.value);case E::UnsafeRest:return tr(STR_DELVE_MSG_UNSAFE);case E::NoStairs:return tr(STR_DELVE_MSG_NOSTAIRS);case E::GiveUp:return tr(STR_DELVE_MSG_GIVEUP);
 }return {};
}
void DelveGameActivity::loop(){
 if(screen==Screen::Playing){
  if(mappedInput.wasPressed(MappedInputManager::Button::Up)){game.step(0,-1);changed();}
  else if(mappedInput.wasPressed(MappedInputManager::Button::Down)){game.step(0,1);changed();}
  else if(mappedInput.wasPressed(MappedInputManager::Button::Left)){game.step(-1,0);changed();}
  else if(mappedInput.wasPressed(MappedInputManager::Button::Right)){game.step(1,0);changed();}
  else if(mappedInput.wasReleased(MappedInputManager::Button::Confirm)){screen=Screen::Bag;selected=0;save();requestUpdate();}
  else if(mappedInput.wasReleased(MappedInputManager::Button::Back)){screen=Screen::Pause;selected=0;save();requestUpdate();}
  return;
 }
 if(screen==Screen::Settings){
  if(mappedInput.wasReleased(MappedInputManager::Button::Back)){
   screen=settingsReturn;selected=0;requestUpdate();
  }else if(mappedInput.wasReleased(MappedInputManager::Button::Confirm)){
   largeView=!largeView;saveView();requestUpdate();
  }
  return;
 }
 if(screen==Screen::Help){if(mappedInput.wasReleased(MappedInputManager::Button::Back)){screen=helpReturn;selected=0;requestUpdate();return;}if(mappedInput.wasReleased(MappedInputManager::Button::Confirm)){helpPage=(helpPage+1)%4;requestUpdate();return;}navigator.onNext([this]{helpPage=(helpPage+1)%4;requestUpdate();});navigator.onPrevious([this]{helpPage=(helpPage+3)%4;requestUpdate();});return;}
 if(mappedInput.wasReleased(MappedInputManager::Button::Back)){
  if(screen==Screen::Title||screen==Screen::End){activityManager.goToGames();return;}
  if(screen==Screen::BagAction){screen=Screen::Bag;selected=bagIndex;}
  else{screen=screen==Screen::ConfirmNew?Screen::Title:Screen::Playing;selected=0;}requestUpdate();return;
 }
 int count=0;
 switch(screen){case Screen::Title:count=savedAvailable?6:5;break;case Screen::Pause:count=9;break;case Screen::Bag:count=std::max(1,static_cast<int>(game.run.bag.size()));break;case Screen::BagAction:count=3;break;case Screen::ConfirmNew:count=2;break;case Screen::Inspect:count=std::max(1,game.visibleEnemies());break;case Screen::End:count=2;break;default:break;}
 navigator.onNext([this,count]{selected=ButtonNavigator::nextIndex(selected,count);requestUpdate();});navigator.onPrevious([this,count]{selected=ButtonNavigator::previousIndex(selected,count);requestUpdate();});
 if(mappedInput.wasReleased(MappedInputManager::Button::Confirm))activate();
}
void DelveGameActivity::activate(){
 if(screen==Screen::Title){int n=selected;if(savedAvailable){if(n==0){screen=Screen::Playing;requestUpdate();return;}--n;}
  if(n==0||n==1){newDaily=n==1;if(newDaily&&HalClock::now()==0){helpReturn=Screen::Title;helpPage=3;screen=Screen::Help;requestUpdate();return;}if(savedAvailable){screen=Screen::ConfirmNew;selected=1;requestUpdate();}else newGame(newDaily);}
  else if(n==2){settingsReturn=Screen::Title;screen=Screen::Settings;selected=0;requestUpdate();}
  else if(n==3){helpReturn=Screen::Title;helpPage=0;screen=Screen::Help;requestUpdate();}else activityManager.goToGames();return;
 }
 if(screen==Screen::ConfirmNew){if(selected==0)newGame(newDaily);else{screen=Screen::Title;selected=0;requestUpdate();}return;}
 if(screen==Screen::End){if(selected==0)newGame(false);else{screen=Screen::Title;selected=0;requestUpdate();}return;}
 if(screen==Screen::Bag){if(!game.run.bag.empty()){bagIndex=selected;selected=0;screen=Screen::BagAction;requestUpdate();}return;}
 if(screen==Screen::BagAction){if(selected==2){screen=Screen::Bag;selected=bagIndex;requestUpdate();return;}if(selected==0)game.useItem(bagIndex);else game.dropItem(bagIndex);screen=Screen::Playing;changed();return;}
 if(screen==Screen::Inspect){screen=Screen::Playing;requestUpdate();return;}
 if(screen==Screen::Pause){int n=selected;selected=0;
  if(n==0){save();screen=Screen::Title;requestUpdate();}
  else if(n==1){screen=Screen::Playing;requestUpdate();}
  else if(n==2){screen=Screen::Bag;requestUpdate();}
  else if(n==3){screen=Screen::Playing;game.descend();save();changed();}
  else if(n==4){screen=Screen::Inspect;requestUpdate();}
  else if(n==5){screen=Screen::Playing;game.wait();changed();}
  else if(n==6){screen=Screen::Playing;game.rest();changed();}
  else if(n==7){settingsReturn=Screen::Pause;screen=Screen::Settings;requestUpdate();}
  else if(n==8){helpReturn=Screen::Pause;helpPage=0;screen=Screen::Help;requestUpdate();}
 }
}
void DelveGameActivity::hints(const char*b,const char*c,const char*u,const char*d){auto l=mappedInput.mapLabels(b,c,u,d);GUI.drawButtonHints(renderer,l.btn1,l.btn2,l.btn3,l.btn4,true);}
void DelveGameActivity::drawListScreen(const char*title,const std::vector<std::string>&items,const char*subtitle){
 auto&m=UITheme::getInstance().getMetrics();int w=renderer.getScreenWidth(),h=renderer.getScreenHeight();GUI.drawHeader(renderer,Rect{0,m.topPadding,w,m.headerHeight},title);
 int y=m.topPadding+m.headerHeight+m.verticalSpacing;
 if(subtitle){auto lines=renderer.wrappedText(UI_HINT_FONT_ID,subtitle,w-2*m.contentSidePadding,3);for(auto&l:lines){renderer.drawText(UI_HINT_FONT_ID,m.contentSidePadding,y,l.c_str());y+=renderer.getLineHeight(UI_HINT_FONT_ID);}y+=8;}
 GUI.drawList(renderer,Rect{0,y,w,h-y-m.buttonHintsHeight-m.verticalSpacing},items.size(),selected,[&](int i){return items[i];});hints(tr(STR_BACK),tr(STR_SELECT),tr(STR_DIR_UP),tr(STR_DIR_DOWN));
}
void DelveGameActivity::drawGame(){
 const int w=renderer.getScreenWidth(),h=renderer.getScreenHeight();const auto&m=UITheme::getInstance().getMetrics();int y=m.topPadding+(largeView?6:0);
 const int hudFont=largeView?STATUS_LARGE_FONT_ID:UI_HINT_FONT_ID;
 const int messageFont=largeView?STATUS_LARGE_FONT_ID:UI_HINT_FONT_ID;
 auto s=format(tr(STR_DELVE_STATUS),game.run.depth,game.run.level,game.run.gold);s=renderer.truncatedText(hudFont,s.c_str(),w-24);renderer.drawText(hudFont,12,y,s.c_str());y+=renderer.getLineHeight(hudFont)+3;
 const int maxHp=std::max(1,int(game.run.maxHp));
 const int hp=std::clamp(int(game.run.hp),0,maxHp);
 const auto health=format(tr(STR_DELVE_HP_VALUE),hp,maxHp);
 const auto torch=format(tr(STR_DELVE_TORCH_VALUE),game.run.torch);
 renderer.drawText(hudFont,12,y,health.c_str());
 const int barX=12+renderer.getTextWidth(hudFont,health.c_str())+8;
 const int torchWidth=renderer.getTextWidth(hudFont,torch.c_str());
 const int barWidth=std::min(64,w-12-torchWidth-8-barX);
 if(barWidth>=20){
  const int barHeight=largeView?10:8;
  const int barY=y+(renderer.getTextHeight(hudFont)-barHeight)/2;
  renderer.drawRect(barX,barY,barWidth,barHeight,1,true);
  const int fill=(barWidth-4)*hp/maxHp;
  if(fill>0)renderer.fillRect(barX+2,barY+2,fill,barHeight-4);
  renderer.drawText(hudFont,barX+barWidth+8,y,torch.c_str());
 }else{
  y+=renderer.getLineHeight(hudFont)+3;
  renderer.drawText(hudFont,12,y,torch.c_str());
 }
 y+=renderer.getLineHeight(hudFont)+5;
 const int messageH=renderer.getLineHeight(messageFont)*3+12;
 const int available=h-m.buttonHintsHeight-messageH-y-8;
 const int cell=std::max(16,std::min(largeView?48:40,w/10));const int cols=std::min(delve::Width,w/cell),rows=std::max(1,std::min(delve::Height,available/cell));
 const int cameraX=std::clamp(int(game.run.x)-cols/2,0,delve::Width-cols),cameraY=std::clamp(int(game.run.y)-rows/2,0,delve::Height-rows);const int ox=(w-cols*cell)/2,oy=y;
 for(int cy=0;cy<rows;++cy)for(int cx=0;cx<cols;++cx){int x=cameraX+cx,yy=cameraY+cy,p=delve::Game::index(x,yy),px=ox+cx*cell,py=oy+cy*cell;auto tile=game.tile(x,yy);
  if(!game.run.seen[p]){renderer.drawLine(px+cell/2,py+cell/2,px+cell/2+1,py+cell/2);continue;}
  if(tile==delve::Tile::Rock){
   bool edge=false;
   for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
    if(game.tile(x+dx,yy+dy)!=delve::Tile::Rock)edge=true;
   if(edge){
    // Keep the white brick faces; only the mortar gets bolder at large scale.
    const int stroke=largeView?2:1;
    for(int r=0;r<3;++r)
     renderer.drawLine(px,py+r*cell/3,px+cell-1,py+r*cell/3,stroke,true);
    renderer.drawLine(px+cell/2,py,px+cell/2,py+cell/3,stroke,true);
    renderer.drawLine(px,py+cell/3,px,py+2*cell/3,stroke,true);
    renderer.drawLine(px+cell/2,py+2*cell/3,px+cell/2,py+cell-1,stroke,true);
   }
   continue;
  }
  const uint16_t*art=nullptr;if(tile==delve::Tile::Door)art=delve_art::door;else if(tile==delve::Tile::Open)art=delve_art::open_door;else if(tile==delve::Tile::Stairs)art=delve_art::stairs;
  if(art)sprite(renderer,art,px+2,py+2,cell-4);else if(game.run.visible[p])renderer.fillRect(px+cell/2,py+cell/2,2,2);
  int item=game.itemAt(x,yy);if(item>=0)sprite(renderer,itemArt(game.run.items[item].kind),px+2,py+2,cell-4);
  int enemy=game.monsterAt(x,yy);if(enemy>=0&&game.run.visible[p]){renderer.fillRect(px+1,py+1,cell-2,cell-2,false);sprite(renderer,monsterArt(game.run.monsters[enemy].kind),px+2,py+2,cell-4);if(!game.run.monsters[enemy].awake||game.run.monsters[enemy].sleep)renderer.drawText(UI_HINT_FONT_ID,px,py,"z");}
  if(x==game.run.x&&yy==game.run.y){renderer.fillRect(px+1,py+1,cell-2,cell-2,false);sprite(renderer,delve_art::hero,px+2,py+2,cell-4);renderer.drawRect(px+1,py+1,cell-2,cell-2);}
 }
 int my=oy+rows*cell+5;renderer.drawLine(0,my-2,w-1,my-2);std::vector<std::string>lines;
 for(const auto&msg:game.messages){auto ls=renderer.wrappedText(messageFont,messageText(msg).c_str(),w-24,3);lines.insert(lines.end(),ls.begin(),ls.end());}
 if(!saveOk){lines.clear();lines.push_back(tr(STR_DELVE_SAVE_ERROR));}
 size_t first=lines.size()>3?lines.size()-3:0;for(size_t i=first;i<lines.size();++i){renderer.drawText(messageFont,12,my,lines[i].c_str());my+=renderer.getLineHeight(messageFont);}
 hints(tr(STR_DELVE_MENU),tr(STR_DELVE_BAG),tr(STR_DIR_LEFT),tr(STR_DIR_RIGHT));
}
void DelveGameActivity::drawHelp(){
 auto&m=UITheme::getInstance().getMetrics();int w=renderer.getScreenWidth(),h=renderer.getScreenHeight();GUI.drawHeader(renderer,Rect{0,m.topPadding,w,m.headerHeight},tr(STR_DELVE_HELP));
 constexpr StrId pages[]={StrId::STR_DELVE_HELP_1,StrId::STR_DELVE_HELP_2,StrId::STR_DELVE_HELP_3,StrId::STR_DELVE_HELP_4};
 int y=m.topPadding+m.headerHeight+15;auto label=format(tr(STR_DELVE_PAGE),helpPage+1,4);renderer.drawText(UI_HINT_FONT_ID,m.contentSidePadding,y,label.c_str());y+=35;
 auto lines=renderer.wrappedText(UI_10_FONT_ID,I18N.get(pages[helpPage]),w-m.contentSidePadding*2,25);for(const auto&l:lines){if(y+renderer.getLineHeight(UI_10_FONT_ID)>h-m.buttonHintsHeight-10)break;renderer.drawText(UI_10_FONT_ID,m.contentSidePadding,y,l.c_str());y+=renderer.getLineHeight(UI_10_FONT_ID)+5;}
 hints(tr(STR_BACK),tr(STR_DELVE_NEXT),tr(STR_DIR_UP),tr(STR_DIR_DOWN));
}
void DelveGameActivity::drawInspect(){
 std::vector<const delve::Monster*>list;for(const auto&mon:game.run.monsters)if(game.run.visible[delve::Game::index(mon.x,mon.y)])list.push_back(&mon);
 if(list.empty()){drawListScreen(tr(STR_DELVE_INSPECT),{tr(STR_DELVE_NO_ENEMIES)});return;}
 selected=std::clamp(selected,0,int(list.size())-1);auto&mon=*list[selected];auto&r=delve::Game::rule(mon.kind);
 constexpr StrId notes[]={StrId::STR_DELVE_NOTE_RAT,StrId::STR_DELVE_NOTE_BAT,StrId::STR_DELVE_NOTE_SNAKE,StrId::STR_DELVE_NOTE_GOBLIN,StrId::STR_DELVE_NOTE_SKELETON,StrId::STR_DELVE_NOTE_ORC,StrId::STR_DELVE_NOTE_TROLL,StrId::STR_DELVE_NOTE_WRAITH,StrId::STR_DELVE_NOTE_DRAGON};
 auto s=format(tr(STR_DELVE_ENEMY_INFO),mon.hp,r.hp,r.lo,r.hi)+" "+I18N.get(notes[static_cast<int>(mon.kind)]);
 std::vector<std::string>names;for(auto*p:list)names.emplace_back(monsterName(p->kind));drawListScreen(tr(STR_DELVE_INSPECT),names,s.c_str());
}
void DelveGameActivity::render(RenderLock&&){renderer.setOrientation(GfxRenderer::Orientation::Portrait);renderer.clearScreen();
 if(screen==Screen::Playing)drawGame();else if(screen==Screen::Help)drawHelp();else if(screen==Screen::Inspect)drawInspect();
 else if(screen==Screen::Title){std::vector<std::string>items;if(savedAvailable)items.emplace_back(tr(STR_GAME_CONTINUE));items.emplace_back(tr(STR_GAME_NEW_SHORT));items.emplace_back(tr(STR_DELVE_DAILY));items.emplace_back(tr(STR_DELVE_SETTINGS));items.emplace_back(tr(STR_DELVE_HELP));items.emplace_back(tr(STR_GAME_EXIT));auto stats=format(tr(STR_DELVE_STATS),runs,wins,deepest);drawListScreen(tr(STR_DELVE_TITLE),items,stats.c_str());}
 else if(screen==Screen::Pause){drawListScreen(tr(STR_DELVE_MENU),{tr(STR_DELVE_TITLE_MENU),tr(STR_GAME_RESUME),tr(STR_DELVE_BAG),tr(STR_DELVE_DESCEND),tr(STR_DELVE_INSPECT),tr(STR_DELVE_WAIT),tr(STR_DELVE_REST),tr(STR_DELVE_SETTINGS),tr(STR_DELVE_HELP)});}
 else if(screen==Screen::Settings){
  const auto item=std::string(tr(STR_DELVE_SCALE))+": "+(largeView?tr(STR_DELVE_LARGE):tr(STR_DELVE_NORMAL));
  drawListScreen(tr(STR_DELVE_SETTINGS),{item},viewSaved?tr(STR_DELVE_SCALE_HELP):tr(STR_DELVE_SCALE_SAVE_ERROR));
 }
 else if(screen==Screen::Bag){std::vector<std::string>items;for(const auto&i:game.run.bag)items.push_back(itemName(i));if(items.empty())items.emplace_back(tr(STR_DELVE_EMPTY_BAG));auto s=format(tr(STR_DELVE_EQUIPMENT),game.attackLow(),game.attackHigh(),game.defence())+" "+tr(STR_DELVE_DROP_HINT);drawListScreen(tr(STR_DELVE_BAG),items,s.c_str());}
 else if(screen==Screen::BagAction){auto name=itemName(game.run.bag[bagIndex]);drawListScreen(name.c_str(),{tr(STR_DELVE_USE),tr(STR_DELVE_DROP),tr(STR_BACK)});}
 else if(screen==Screen::ConfirmNew)drawListScreen(tr(STR_DELVE_NEW_CONFIRM),{tr(STR_GAME_NEW_SHORT),tr(STR_CANCEL)},tr(STR_DELVE_LOSE_RUN));
 else{auto s=format(tr(STR_DELVE_END_STATS),game.run.depth,game.run.turns,game.run.gold,game.run.kills);drawListScreen(game.run.won?tr(STR_DELVE_WON):tr(STR_DELVE_LOST),{tr(STR_GAME_NEW_SHORT),tr(STR_DELVE_TITLE_MENU)},s.c_str());}
 renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
