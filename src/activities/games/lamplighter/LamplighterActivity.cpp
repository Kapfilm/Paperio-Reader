#include "LamplighterActivity.h"
#include <HalStorage.h>
#include <I18n.h>
#include <algorithm>
#include <cstdio>
#include "UiFonts.h"
#include "components/UITheme.h"
#include "fontIds.h"
namespace {
constexpr const char* path="/.crosspoint/lamplighter.bin";
bool read(const std::string&p,std::vector<uint8_t>&b){HalFile f;if(!Storage.openFileForRead("LAMP",p.c_str(),f)||f.size()>1270||f.size()<1)return false;b.resize(f.size());return f.read(b.data(),b.size())==static_cast<int>(b.size());}
bool write(const std::vector<uint8_t>&b){
 const std::string tmp=std::string(path)+".tmp",bak=std::string(path)+".bak";HalFile f;
 if(!Storage.openFileForWrite("LAMP",tmp.c_str(),f)||f.write(b.data(),b.size())!=b.size())return false;
 f.flush();if(!f.close())return false;std::vector<uint8_t>check;lamplighter::Model verified;if(!read(tmp,check)||check!=b||!verified.decode(check))return false;
 if(Storage.exists(path)){if(Storage.exists(bak.c_str())&&!Storage.remove(bak.c_str()))return false;if(!Storage.rename(path,bak.c_str()))return false;}
 if(!Storage.rename(tmp.c_str(),path)){if(!Storage.exists(path)&&Storage.exists(bak.c_str()))Storage.rename(bak.c_str(),path);return false;}
 if(Storage.exists(bak.c_str()))Storage.remove(bak.c_str());return true;
}
}
void LamplighterActivity::onEnter(){Activity::onEnter();renderer.setOrientation(GfxRenderer::Orientation::Portrait);load();requestUpdate();}
void LamplighterActivity::onExit(){save();Activity::onExit();}
void LamplighterActivity::load(){for(const auto&p:{std::string(path),std::string(path)+".bak",std::string(path)+".tmp"}){std::vector<uint8_t>b;if(read(p,b)&&game.decode(b))break;}cursor=game.source();}
bool LamplighterActivity::save(){if(!Storage.exists("/.crosspoint")&&!Storage.mkdir("/.crosspoint")){saveFailed=true;return false;}saveFailed=!write(game.encode());return !saveFailed;}
const char* LamplighterActivity::levelName()const{const StrId ids[]={StrId::STR_LAMP_EASY,StrId::STR_LAMP_MEDIUM,StrId::STR_LAMP_HARD};return I18N.get(ids[game.level]);}
void LamplighterActivity::hints(const char*b,const char*c,const char*u,const char*d){auto l=mappedInput.mapLabels(b,c,u,d);GUI.drawButtonHints(renderer,l.btn1,l.btn2,l.btn3,l.btn4,true);}
void LamplighterActivity::changePuzzle(int delta){if(!save()){requestUpdate();return;}game.open(game.level,(game.puzzle+delta+lamplighter::Model::Count)%lamplighter::Model::Count);cursor=game.source();lockMode=false;save();requestUpdate();}
void LamplighterActivity::move(int dr,int dc){int r=cursor/game.size(),c=cursor%game.size();r=std::clamp(r+dr,0,game.size()-1);c=std::clamp(c+dc,0,game.size()-1);cursor=r*game.size()+c;requestUpdate();}
void LamplighterActivity::loop(){
 if(screen==Screen::Playing){
  if(mappedInput.wasPressed(MappedInputManager::Button::Up))move(-1,0);
  else if(mappedInput.wasPressed(MappedInputManager::Button::Down))move(1,0);
  else if(mappedInput.wasPressed(MappedInputManager::Button::Left))move(0,-1);
  else if(mappedInput.wasPressed(MappedInputManager::Button::Right))move(0,1);
  else if(mappedInput.wasReleased(MappedInputManager::Button::Confirm)){
   if(game.solved()){changePuzzle(1);return;}
   bool changed=lockMode?game.toggleLock(cursor):game.rotate(cursor);if(changed)save();requestUpdate();
  }else if(mappedInput.wasReleased(MappedInputManager::Button::Back)){save();screen=Screen::Menu;selected=0;requestUpdate();}return;
 }
 if(screen==Screen::Help){
  navigator.onNext([this]{++helpOffset;requestUpdate();});navigator.onPrevious([this]{helpOffset=std::max(0,helpOffset-1);requestUpdate();});
  if(mappedInput.wasReleased(MappedInputManager::Button::Back)||mappedInput.wasReleased(MappedInputManager::Button::Confirm)){screen=Screen::Menu;selected=0;requestUpdate();}return;
 }
 const int count=screen==Screen::ConfirmRestart?2:10;
 navigator.onNext([this,count]{selected=ButtonNavigator::nextIndex(selected,count);requestUpdate();});navigator.onPrevious([this,count]{selected=ButtonNavigator::previousIndex(selected,count);requestUpdate();});
 if(mappedInput.wasReleased(MappedInputManager::Button::Back)){screen=Screen::Playing;requestUpdate();}
 else if(mappedInput.wasReleased(MappedInputManager::Button::Confirm))activate();
}
void LamplighterActivity::activate(){
 if(screen==Screen::ConfirmRestart){if(selected==0){game.restart();cursor=game.source();lockMode=false;save();}screen=Screen::Menu;selected=0;requestUpdate();return;}
 switch(selected){
 case 0:screen=Screen::Playing;break;
 case 1:lockMode=!lockMode;screen=Screen::Playing;break;
 case 2:game.undo();save();screen=Screen::Playing;break;
 case 3:{int i=game.hint();if(i>=0)cursor=i;save();screen=Screen::Playing;break;}
 case 4:changePuzzle(1);break;
 case 5:changePuzzle(-1);break;
 case 6:screen=Screen::ConfirmRestart;selected=1;break;
 case 7:{if(!save())break;int level=(game.level+1)%3;game.open(level,game.last[level]);cursor=game.source();lockMode=false;save();break;}
 case 8:helpOffset=0;screen=Screen::Help;break;
 case 9:if(save()){activityManager.goToGames();return;}break;
 }
 requestUpdate();
}
void LamplighterActivity::drawGame(){
 const auto&m=UITheme::getInstance().getMetrics();int w=renderer.getScreenWidth(),h=renderer.getScreenHeight();
 char sub[128];snprintf(sub,sizeof(sub),"%s · %d/200 · %s %d",levelName(),game.puzzle+1,tr(STR_LAMP_COMPLETED),game.completedCount(game.level));
 GUI.drawHeader(renderer,Rect{0,m.topPadding,w,m.headerHeight},tr(STR_LAMP_TITLE));
 int top=m.topPadding+m.headerHeight+8;renderer.drawText(STATUS_LARGE_FONT_ID,16,top,sub);top+=renderer.getLineHeight(STATUS_LARGE_FONT_ID)+14;
 const int bottom=h-m.buttonHintsHeight-3*renderer.getLineHeight(STATUS_LARGE_FONT_ID)-24;
 const int cell=std::max(8,std::min({(w-32)/game.size(),(bottom-top)/game.size(),80}));const int ox=(w-cell*game.size())/2,oy=top+std::max(0,(bottom-top-cell*game.size())/2);const auto on=game.lit();
 // Lightly dotted intersections replace the tile grid.
 const int board=cell*game.size();
 renderer.drawRect(ox,oy,board+1,board+1,1,true);
 for(int row=1;row<game.size();++row)for(int col=1;col<game.size();++col)
  renderer.fillRect(ox+col*cell-1,oy+row*cell-1,2,2,true);
 // Draw right-angled wires with a clear contrast between powered and unpowered.
 for(int i=0;i<game.cells();++i){
  const int x=ox+(i%game.size())*cell,y=oy+(i/game.size())*cell,cx=x+cell/2,cy=y+cell/2;
  const int stroke=on[i]?6:3,half=stroke/2;
  // GfxRenderer's thick drawLine offsets only Y, so a vertical line stays thin.
  // Rectangular strokes give both axes the same width and solid square elbows.
  if(game.tiles[i]&1)renderer.fillRect(cx-half,y,stroke,cy-y+1,true);
  if(game.tiles[i]&2)renderer.fillRect(cx,cy-half,x+cell-cx+1,stroke,true);
  if(game.tiles[i]&4)renderer.fillRect(cx-half,cy,stroke,y+cell-cy+1,true);
  if(game.tiles[i]&8)renderer.fillRect(x,cy-half,cx-x+1,stroke,true);
  renderer.fillRect(cx-half,cy-half,stroke,stroke,true);
 }
 auto disk=[&](int cx,int cy,int radius,bool black){
  for(int yy=-radius;yy<=radius;++yy){
   int xx=radius;while(xx>0&&xx*xx+yy*yy>radius*radius)--xx;
   renderer.fillRect(cx-xx,cy+yy,2*xx+1,1,black);
  }
 };
 for(int i=0;i<game.cells();++i){
  const int x=ox+(i%game.size())*cell,y=oy+(i/game.size())*cell,cx=x+cell/2,cy=y+cell/2;
  int mask=game.tiles[i],bits=0;while(mask){bits+=mask&1;mask>>=1;}
  const int r=std::max(4,cell/6);
  if(i==game.source()){
   renderer.fillRect(cx-r-2,cy-r-2,2*r+5,2*r+5,false);
   renderer.drawRect(cx-r-2,cy-r-2,2*r+5,2*r+5,2,true);
   renderer.fillRect(cx-r+2,cy-r+2,2*r-3,2*r-3,true);
  }else if(bits==1){
   disk(cx,cy,r,true);
   if(!on[i])disk(cx,cy,r-2,false);
  }
  // Small markers preserve lock feedback without framing inactive cells.
  if(game.locks[i]){
   renderer.fillRect(x+cell-7,y+cell-7,3,3,true);
   if(game.locks[i]==2)renderer.fillRect(x+cell-12,y+cell-7,3,3,true);
  }
  if(i==cursor)renderer.drawRect(x+2,y+2,cell-3,cell-3,2,true);
 }

 int y=oy+game.size()*cell+10;char status[96];snprintf(status,sizeof(status),tr(STR_LAMP_STATUS),game.lampCount(true),game.lampCount(false),static_cast<unsigned long>(game.moves));
 renderer.drawText(STATUS_LARGE_FONT_ID,16,y,status);y+=renderer.getLineHeight(STATUS_LARGE_FONT_ID)+4;
 const char* msg=saveFailed?tr(STR_LAMP_SAVE_ERROR):game.solved()?tr(STR_LAMP_SOLVED):lockMode?tr(STR_LAMP_LOCK_HELP):tr(STR_LAMP_TURN_HELP);
 for(const auto&l:renderer.wrappedText(STATUS_LARGE_FONT_ID,msg,w-32,2)){renderer.drawText(STATUS_LARGE_FONT_ID,16,y,l.c_str());y+=renderer.getLineHeight(STATUS_LARGE_FONT_ID);}
 hints(tr(STR_LAMP_MENU),game.solved()?tr(STR_LAMP_NEXT):lockMode?tr(STR_LAMP_LOCK_SHORT):tr(STR_LAMP_ROTATE),tr(STR_DIR_LEFT),tr(STR_DIR_RIGHT));
}
void LamplighterActivity::drawMenu(){
 const auto&m=UITheme::getInstance().getMetrics();int w=renderer.getScreenWidth(),h=renderer.getScreenHeight();GUI.drawHeader(renderer,Rect{0,m.topPadding,w,m.headerHeight},tr(STR_LAMP_TITLE));
 std::vector<std::string>items;
 if(screen==Screen::ConfirmRestart)items={tr(STR_LAMP_RESTART_YES),tr(STR_BACK)};
 else items={tr(STR_GAME_RESUME),std::string(tr(STR_LAMP_LOCK_MODE))+": "+(lockMode?tr(STR_LAMP_ON):tr(STR_LAMP_OFF)),tr(STR_LAMP_UNDO),tr(STR_LAMP_HINT),tr(STR_LAMP_NEXT),tr(STR_LAMP_PREVIOUS),tr(STR_LAMP_RESTART),std::string(tr(STR_LAMP_LEVEL))+": "+levelName(),tr(STR_LAMP_HELP),tr(STR_LAMP_EXIT)};
 int top=m.topPadding+m.headerHeight+12;if(saveFailed){renderer.drawText(UI_HINT_FONT_ID,16,top,tr(STR_LAMP_SAVE_ERROR));top+=renderer.getLineHeight(UI_HINT_FONT_ID)*2;}
 GUI.drawList(renderer,Rect{0,top,w,h-top-m.buttonHintsHeight-8},items.size(),selected,[&](int i){return items[i];});hints(tr(STR_BACK),tr(STR_SELECT),tr(STR_DIR_UP),tr(STR_DIR_DOWN));
}
void LamplighterActivity::drawHelp(){
 const auto&m=UITheme::getInstance().getMetrics();int w=renderer.getScreenWidth(),h=renderer.getScreenHeight();GUI.drawHeader(renderer,Rect{0,m.topPadding,w,m.headerHeight},tr(STR_LAMP_HELP));int y=m.topPadding+m.headerHeight+14;
 const auto lines=renderer.wrappedText(UI_10_FONT_ID,tr(STR_LAMP_HELP_TEXT),w-32,100);int height=renderer.getLineHeight(UI_10_FONT_ID)+4;int count=std::max(1,(h-m.buttonHintsHeight-y-10)/height);helpOffset=std::clamp(helpOffset,0,std::max(0,int(lines.size())-count));
 for(int i=helpOffset;i<int(lines.size())&&i<helpOffset+count;++i){renderer.drawText(UI_10_FONT_ID,16,y,lines[i].c_str());y+=height;}hints(tr(STR_BACK),tr(STR_BACK),tr(STR_DIR_UP),tr(STR_DIR_DOWN));
}
void LamplighterActivity::render(RenderLock&&){renderer.setOrientation(GfxRenderer::Orientation::Portrait);renderer.clearScreen();if(screen==Screen::Playing)drawGame();else if(screen==Screen::Help)drawHelp();else drawMenu();renderer.displayBuffer(HalDisplay::FAST_REFRESH);}
