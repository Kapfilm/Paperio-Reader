#include "CaseFilesActivity.h"
#include "CaseFilesText.h"
#include <HalStorage.h>
#include <I18n.h>
#include <algorithm>
#include <cstdio>
#include "UiFonts.h"
#include "components/UITheme.h"
#include "fontIds.h"
namespace {
constexpr const char*path="/.crosspoint/casefiles.bin";
bool read(const std::string&p,std::vector<uint8_t>&b){HalFile f;if(!Storage.openFileForRead("CASE",p.c_str(),f)||f.size()<16||f.size()>1800)return false;b.resize(f.size());return f.read(b.data(),b.size())==static_cast<int>(b.size());}
bool write(const std::vector<uint8_t>&b){
 if(!Storage.exists("/.crosspoint")&&!Storage.mkdir("/.crosspoint"))return false;
 std::string tmp=std::string(path)+".tmp",bak=std::string(path)+".bak";HalFile f;
 if(!Storage.openFileForWrite("CASE",tmp.c_str(),f)||f.write(b.data(),b.size())!=b.size())return false;f.flush();if(!f.close())return false;
 std::vector<uint8_t>check;casefiles::Model model;if(!read(tmp,check)||check!=b||!model.decode(check))return false;
 if(Storage.exists(path)){if(Storage.exists(bak.c_str())&&!Storage.remove(bak.c_str()))return false;if(!Storage.rename(path,bak.c_str()))return false;}
 if(!Storage.rename(tmp.c_str(),path)){if(!Storage.exists(path)&&Storage.exists(bak.c_str()))Storage.rename(bak.c_str(),path);return false;}
 if(Storage.exists(bak.c_str()))Storage.remove(bak.c_str());return true;
}
StrId levelId(int l){return l==0?StrId::STR_CASE_EASY:l==1?StrId::STR_CASE_MEDIUM:StrId::STR_CASE_HARD;}
}
void CaseFilesActivity::load(){game.open(0,0);for(const auto&p:{std::string(path),std::string(path)+".bak",std::string(path)+".tmp"}){std::vector<uint8_t>b;if(read(p,b)&&game.decode(b))break;}}
bool CaseFilesActivity::save(){saveFailed=!write(game.encode());return !saveFailed;}
void CaseFilesActivity::onEnter(){Activity::onEnter();renderer.setOrientation(GfxRenderer::Orientation::Portrait);load();checkVictory();requestUpdate();}
void CaseFilesActivity::onExit(){save();Activity::onExit();}
void CaseFilesActivity::hints(const char*b,const char*c,const char*u,const char*d){auto l=mappedInput.mapLabels(b,c,u,d);GUI.drawButtonHints(renderer,l.btn1,l.btn2,l.btn3,l.btn4,true);}
void CaseFilesActivity::activate(){
 if(screen==Screen::Menu){int n=selected;selected=0;
  if(n==0)screen=Screen::Playing;
  else if(n==1){screen=Screen::Playing;focus=Focus::Clues;clue=0;}
  else if(n==2){screen=Screen::Playing;focus=Focus::Tabs;tabCursor=game.table;}
  else if(n==3){game.undo();message.clear();save();screen=Screen::Playing;}
  else if(n==4){int wrong=game.wrongCount();char b[96];snprintf(b,sizeof(b),tr(STR_CASE_CHECK_BAD),wrong);message=wrong?b:tr(STR_CASE_CHECK_OK);screen=Screen::Playing;}
  else if(n==5){game.hint();message=tr(STR_CASE_HINT_MSG);save();screen=Screen::Playing;checkVictory();}
  else if(n==6||n==7){if(save()){game.open(game.level,(game.index+(n==6?1:199))%200);clue=0;focus=Focus::Grid;tabCursor=game.table;victoryAnnounced=false;message.clear();save();screen=Screen::Playing;}}
  else if(n==8){screen=Screen::Levels;selected=game.level;}
  else if(n==9)screen=Screen::ConfirmRestart;
  else if(n==10){screen=Screen::Rules;helpLine=0;}
  else if(save()){activityManager.goToGames();return;}
 }else if(screen==Screen::Tables){game.table=selected;game.cursor=0;save();screen=Screen::Playing;selected=0;}
 else if(screen==Screen::Levels){if(save()){int target=selected;game.open(target,game.positions[target]);clue=0;focus=Focus::Grid;tabCursor=game.table;victoryAnnounced=false;message.clear();save();screen=Screen::Playing;selected=0;}}
 else if(screen==Screen::ConfirmRestart){game.reset();victoryAnnounced=false;message.clear();save();screen=Screen::Playing;}
 if(screen==Screen::Playing){clue=std::clamp(clue,0,int(game.puzzle.clues.size())-1);tabCursor=game.table;confirmHold.reset();rightHold.reset();}
 requestUpdate();
}
void CaseFilesActivity::loop(){
 if(screen==Screen::Playing){playingLoop();return;}
 if(screen==Screen::Clues){
  nav.onNext([this]{clue=(clue+1)%game.puzzle.clues.size();requestUpdate();});nav.onPrevious([this]{clue=(clue+game.puzzle.clues.size()-1)%game.puzzle.clues.size();requestUpdate();});
  if(mappedInput.wasReleased(MappedInputManager::Button::Confirm)){game.struck[clue]^=1;save();requestUpdate();}
  else if(mappedInput.wasReleased(MappedInputManager::Button::Back)){screen=Screen::Menu;selected=1;requestUpdate();}return;
 }
 if(screen==Screen::Rules){
  const auto&m=UITheme::getInstance().getMetrics();int top=m.topPadding+m.headerHeight+20,lh=renderer.getLineHeight(UI_10_FONT_ID)+3;
  auto lines=renderer.wrappedText(UI_10_FONT_ID,tr(STR_CASE_RULES_TEXT),renderer.getScreenWidth()-40,120);
  int pageLines=std::max(1,(renderer.getScreenHeight()-m.buttonHintsHeight-top-12)/lh),last=std::max(0,int(lines.size())-pageLines);
  nav.onNext([this,last]{helpLine=std::min(helpLine+1,last);requestUpdate();});nav.onPrevious([this]{helpLine=std::max(0,helpLine-1);requestUpdate();});
  if(mappedInput.wasReleased(MappedInputManager::Button::Back)||mappedInput.wasReleased(MappedInputManager::Button::Confirm)){screen=Screen::Menu;selected=10;requestUpdate();}return;
 }
 if(mappedInput.wasReleased(MappedInputManager::Button::Back)){
  if(screen==Screen::Menu){screen=Screen::Playing;}else{screen=Screen::Menu;selected=0;}requestUpdate();return;
 }
 int count=screen==Screen::Menu?12:screen==Screen::Tables?game.pairCount:screen==Screen::Levels?3:1;
 nav.onNext([this,count]{selected=ButtonNavigator::nextIndex(selected,count);requestUpdate();});nav.onPrevious([this,count]{selected=ButtonNavigator::previousIndex(selected,count);requestUpdate();});
 if(mappedInput.wasReleased(MappedInputManager::Button::Confirm))activate();
}
std::string CaseFilesActivity::goalText()const{
 std::string categories;
 for(int g=1;g<game.puzzle.k;++g){if(g>1)categories+=", ";categories+=casefiles::category(game.puzzle,g);}
 return std::string(tr(STR_CASE_BOARD_GOAL))+categories+".";
}
CaseFilesActivity::Layout CaseFilesActivity::layout()const{
 const auto&m=UITheme::getInstance().getMetrics();const int w=renderer.getScreenWidth(),h=renderer.getScreenHeight(),lh=renderer.getLineHeight(STATUS_LARGE_FONT_ID);
 Layout v{};v.themeY=m.topPadding+m.headerHeight+6;v.storyY=v.themeY+renderer.getLineHeight(UI_10_FONT_ID)+10;
 const int contextLineHeight=renderer.getLineHeight(UI_HINT_FONT_ID);
 v.goalY=v.storyY+int(renderer.wrappedText(UI_HINT_FONT_ID,casefiles::themeStory(game.puzzle),w-48,100).size())*contextLineHeight+4;
 v.clueY=v.goalY+int(renderer.wrappedText(UI_HINT_FONT_ID,goalText().c_str(),w-48,100).size())*contextLineHeight+18;
 v.bottom=h-m.buttonHintsHeight-8;v.tabW=104;
 const int footer=2*lh+14;
 int labelRows=1;auto pair=game.pairs[game.table];
 for(int col=0;col<game.puzzle.n;++col)labelRows=std::max(labelRows,int(renderer.wrappedText(STATUS_LARGE_FONT_ID,casefiles::item(game.puzzle,pair.b,col),34,2).size()));
 const int headers=(1+labelRows)*lh+14;
 v.clueH=std::min(210+2*lh,std::max(2*lh+16,v.bottom-v.clueY-footer-headers-game.puzzle.n*38-12));
 v.tabsY=v.clueY+v.clueH+10;v.labelsY=v.tabsY+lh+10;v.gridY=v.labelsY+labelRows*lh+2;
 v.cell=std::max(20,std::min({58,(w-40-v.tabW-16-80)/game.puzzle.n,(v.bottom-footer-v.gridY)/game.puzzle.n}));
 v.gridX=w-20-game.puzzle.n*v.cell;return v;
}
std::vector<CaseFilesActivity::CluePage> CaseFilesActivity::cluePages()const{
 auto v=layout();int lh=renderer.getLineHeight(STATUS_LARGE_FONT_ID),capacity=v.clueH-lh-8,used=0,first=0;
 std::vector<CluePage> pages;
 for(int i=0;i<int(game.puzzle.clues.size());++i){
  auto text=std::to_string(i+1)+". "+casefiles::clueText(game.puzzle,game.puzzle.clues[i]);
  int height=std::min(capacity,int(renderer.wrappedText(STATUS_LARGE_FONT_ID,text.c_str(),renderer.getScreenWidth()-52,100).size())*lh+8);
  if(i>first&&used+height>capacity){pages.push_back({first,i});first=i;used=0;}used+=height;
 }
 pages.push_back({first,int(game.puzzle.clues.size())});return pages;
}
int CaseFilesActivity::cluePageIndex(const std::vector<CluePage>&pages)const{for(int i=0;i<int(pages.size());++i)if(clue>=pages[i].first&&clue<pages[i].last)return i;return 0;}
void CaseFilesActivity::cycleFocus(){focus=focus==Focus::Grid?Focus::Clues:focus==Focus::Clues?Focus::Tabs:Focus::Grid;tabCursor=game.table;rightHold.reset();requestUpdate();}
void CaseFilesActivity::playingLoop(){
 using B=MappedInputManager::Button;using E=casefiles::HoldButton::Event;
 if(victoryPopup){
  if(mappedInput.wasPressed(B::Up)||mappedInput.wasPressed(B::Down)||mappedInput.wasPressed(B::Left)||mappedInput.wasPressed(B::Right)){victoryChoice^=1;requestUpdate();}
  if(mappedInput.wasReleased(B::Back)){victoryPopup=false;screen=Screen::Menu;selected=0;requestUpdate();}
  else if(mappedInput.wasReleased(B::Confirm)){
   if(!save()){requestUpdate();return;}
   victoryPopup=false;
   if(victoryChoice==0){game.open(game.level,(game.index+1)%200);victoryAnnounced=false;clue=0;tabCursor=0;focus=Focus::Grid;message.clear();save();}
   else{screen=Screen::Menu;selected=0;}
   confirmHold.reset();rightHold.reset();requestUpdate();
  }return;
 }
 if(cluePopup){
  if(mappedInput.wasReleased(B::Back)){cluePopup=false;confirmHold.reset();rightHold.reset();requestUpdate();return;}
  auto lines=popupLines();int rows=popupRows(),last=std::max(0,int(lines.size())-rows),next=popupLine;
  if(mappedInput.wasPressed(B::Up))--next;
  else if(mappedInput.wasPressed(B::Down))++next;
  else if(mappedInput.wasPressed(B::Left))next-=rows;
  else if(mappedInput.wasPressed(B::Right)||mappedInput.wasReleased(B::Confirm))next=popupLine==last?0:popupLine+rows;
  next=std::clamp(next,0,last);if(next!=popupLine){popupLine=next;requestUpdate();}return;
 }
 const auto confirm=confirmHold.update(mappedInput.wasPressed(B::Confirm),mappedInput.isPressed(B::Confirm),mappedInput.wasReleased(B::Confirm),millis());
 if(confirm==E::Long){cycleFocus();return;}
 if(mappedInput.wasReleased(B::Back)){confirmHold.reset();rightHold.reset();screen=Screen::Menu;selected=0;save();requestUpdate();return;}
 if(focus==Focus::Clues){
  const auto right=rightHold.update(mappedInput.wasPressed(B::Right),mappedInput.isPressed(B::Right),mappedInput.wasReleased(B::Right),millis());
  if(right==E::Long){cluePopup=true;popupLine=0;rightHold.reset();requestUpdate();return;}
  if(mappedInput.wasPressed(B::Up))clue=(clue+game.puzzle.clues.size()-1)%game.puzzle.clues.size();
  else if(mappedInput.wasPressed(B::Down))clue=(clue+1)%game.puzzle.clues.size();
  else if(mappedInput.wasPressed(B::Left)||right==E::Short){auto pages=cluePages();int page=cluePageIndex(pages);page=(page+(right==E::Short?1:int(pages.size())-1))%pages.size();clue=pages[page].first;}
  else if(confirm==E::Short){game.struck[clue]^=1;save();}
  else return;requestUpdate();return;
 }
 if(focus==Focus::Tabs){
  if(mappedInput.wasPressed(B::Left)||mappedInput.wasPressed(B::Up))tabCursor=(tabCursor+game.pairCount-1)%game.pairCount;
  else if(mappedInput.wasPressed(B::Right)||mappedInput.wasPressed(B::Down))tabCursor=(tabCursor+1)%game.pairCount;
  else if(confirm==E::Short){focus=Focus::Grid;save();requestUpdate();return;}
  else return;
  // A tab preview needs no SD write; confirm, menu and exit persist the view.
  game.table=tabCursor;message.clear();requestUpdate();return;
 }
 int n=game.puzzle.n,r=game.cursor/n,c=game.cursor%n;
 if(mappedInput.wasPressed(B::Up))r=(r+n-1)%n;else if(mappedInput.wasPressed(B::Down))r=(r+1)%n;
 else if(mappedInput.wasPressed(B::Left))c=(c+n-1)%n;else if(mappedInput.wasPressed(B::Right))c=(c+1)%n;
 else if(confirm==E::Short){message.clear();if(!game.cycle()&&game.marks[game.table][game.cursor]==casefiles::Model::Hint)message=tr(STR_CASE_HINT_LOCKED);save();checkVictory();requestUpdate();return;}
 else return;game.cursor=r*n+c;requestUpdate();
}
void CaseFilesActivity::drawGrid(){
 const int w=renderer.getScreenWidth(),lh=renderer.getLineHeight(STATUS_LARGE_FONT_ID);auto v=layout();const auto&p=game.puzzle;auto pair=game.pairs[game.table];
 renderer.drawText(UI_10_FONT_ID,20,v.themeY,casefiles::themeTitle(p),true,EpdFontFamily::BOLD);
 renderer.drawRect(16,v.storyY-6,w-32,v.clueY-v.storyY-4,2,true);
 int y=v.storyY;const int contextLineHeight=renderer.getLineHeight(UI_HINT_FONT_ID);
 for(const auto&line:renderer.wrappedText(UI_HINT_FONT_ID,casefiles::themeStory(p),w-48,100)){renderer.drawText(UI_HINT_FONT_ID,24,y,line.c_str());y+=contextLineHeight;}
 y=v.goalY;
 for(const auto&line:renderer.wrappedText(UI_HINT_FONT_ID,goalText().c_str(),w-48,100)){renderer.drawText(UI_HINT_FONT_ID,24,y,line.c_str());y+=contextLineHeight;}
 auto pages=cluePages();const int page=cluePageIndex(pages);char label[100];snprintf(label,sizeof(label),tr(STR_CASE_BOARD_CLUES),page+1,int(pages.size()));
 renderer.drawText(STATUS_LARGE_FONT_ID,20,v.clueY,label,true,EpdFontFamily::BOLD);
 if(focus==Focus::Clues)renderer.fillRect(16,v.clueY,3,lh,true);
 y=v.clueY+lh+4;
 for(int i=pages[page].first;i<pages[page].last;++i){
  auto text=std::to_string(i+1)+". "+casefiles::clueText(p,p.clues[i]);int maxLines=std::max(1,(v.clueY+v.clueH-y-6)/lh);
  const auto lines=renderer.wrappedText(STATUS_LARGE_FONT_ID,text.c_str(),w-52,maxLines);const int height=int(lines.size())*lh+6;
  if(focus==Focus::Clues&&i==clue)renderer.drawRect(16,y-2,w-32,height,2,true);
  for(const auto&line:lines){renderer.drawText(STATUS_LARGE_FONT_ID,24,y,line.c_str());if(game.struck[i])renderer.drawLine(24,y+renderer.getTextHeight(STATUS_LARGE_FONT_ID)/2,w-24,y+renderer.getTextHeight(STATUS_LARGE_FONT_ID)/2);y+=lh;}y+=8;
 }
 const int chosen=focus==Focus::Tabs?tabCursor:game.table,start=chosen/3*3;
 renderer.fillRect(16,v.tabsY-7,w-32,2,true);
 renderer.drawText(STATUS_LARGE_FONT_ID,20,v.tabsY,tr(STR_CASE_TABLES),true,EpdFontFamily::BOLD);
 auto fieldTitle=std::string(tr(STR_CASE_BOARD_FIELD))+" "+std::to_string(game.table+1)+"/"+std::to_string(game.pairCount);
 renderer.drawText(STATUS_LARGE_FONT_ID,v.gridX,v.tabsY,fieldTitle.c_str(),true,EpdFontFamily::BOLD);
 const int tabY=v.tabsY+lh+8,tabH=2*lh+12;
 renderer.fillRect(20+v.tabW+7,tabY-4,2,std::max(tabH*3,v.gridY+p.n*v.cell-tabY),true);
 for(int i=start;i<std::min(start+3,game.pairCount);++i){int x=16,yy=tabY+(i-start)*tabH;auto q=game.pairs[i];bool active=focus==Focus::Tabs&&i==tabCursor;
  renderer.drawRect(x,yy,v.tabW,tabH-5,2,true);
  if(focus==Focus::Tabs?active:i==game.table)renderer.fillRect(x+v.tabW-5,yy+1,5,tabH-7,true);
  const char* texts[]={casefiles::category(p,q.a),casefiles::category(p,q.b)};
  for(int j=0;j<2;++j){auto text=j==0?std::to_string(i+1)+". "+texts[j]:std::string(texts[j]);auto t=renderer.truncatedText(STATUS_LARGE_FONT_ID,text.c_str(),v.tabW-14);renderer.drawText(STATUS_LARGE_FONT_ID,x+7,yy+4+j*lh,t.c_str());}
 }
 const int pitch=v.cell,x0=v.gridX,gy=v.gridY;
 for(int col=0;col<p.n;++col){auto lines=renderer.wrappedText(STATUS_LARGE_FONT_ID,casefiles::item(p,pair.b,col),pitch-4,2);int yy=gy-2-int(lines.size())*lh;for(const auto&t:lines){renderer.drawText(STATUS_LARGE_FONT_ID,x0+col*pitch+(pitch-renderer.getTextWidth(STATUS_LARGE_FONT_ID,t.c_str()))/2,yy,t.c_str());yy+=lh;}}
 for(int row=0;row<p.n;++row){auto t=renderer.truncatedText(STATUS_LARGE_FONT_ID,casefiles::item(p,pair.a,row),x0-(20+v.tabW+16)-8);renderer.drawText(STATUS_LARGE_FONT_ID,x0-8-renderer.getTextWidth(STATUS_LARGE_FONT_ID,t.c_str()),gy+row*pitch+(pitch-renderer.getTextHeight(STATUS_LARGE_FONT_ID))/2,t.c_str());}
 for(int j=0;j<=p.n;++j){renderer.fillRect(x0+j*pitch,gy,3,p.n*pitch+3,true);renderer.fillRect(x0,gy+j*pitch,p.n*pitch+3,3,true);}
 for(int row=0;row<p.n;++row)for(int col=0;col<p.n;++col){int x=x0+col*pitch,yy=gy+row*pitch,c=row*p.n+col;auto mark=game.marks[game.table][c];int cx=x+pitch/2,cy=yy+pitch/2,r=std::max(4,pitch/5);
  if(mark==casefiles::Model::Cross){renderer.drawLine(cx-r,cy-r,cx+r,cy+r,2,true);renderer.drawLine(cx+r,cy-r,cx-r,cy+r,2,true);}
  else if(mark==casefiles::Model::Tick||mark==casefiles::Model::Hint){renderer.drawLine(cx-r,cy,cx-r/3,cy+r,3,true);renderer.drawLine(cx-r/3,cy+r,cx+r,cy-r,3,true);if(mark==casefiles::Model::Hint)renderer.drawLine(cx-r,cy+r+4,cx+r,cy+r+4,2,true);}
  else if(game.ruledOut(game.table,row,col))renderer.fillRect(cx-1,cy-1,3,3);
  if(game.conflict(game.table,row,col))renderer.drawRoundedRect(x+5,yy+5,pitch-9,pitch-9,1,5,true);
  if(focus==Focus::Grid&&c==game.cursor)renderer.drawRect(x+3,yy+3,pitch-5,pitch-5,3,true);
 }
 y=gy+p.n*pitch+8;auto detail=std::string(casefiles::item(p,pair.a,game.cursor/p.n))+" / "+casefiles::item(p,pair.b,game.cursor%p.n);
 auto status=saveFailed?std::string(tr(STR_CASE_SAVE_ERROR)):game.solved()?std::string(tr(STR_CASE_SOLVED)):message;
 const int boardWidth=p.n*pitch;
 for(const auto&line:renderer.wrappedText(STATUS_LARGE_FONT_ID,detail.c_str(),boardWidth,status.empty()?2:1)){renderer.drawText(STATUS_LARGE_FONT_ID,x0+(boardWidth-renderer.getTextWidth(STATUS_LARGE_FONT_ID,line.c_str()))/2,y,line.c_str());y+=lh;}
 if(!status.empty()&&y+lh<=v.bottom){auto line=renderer.truncatedText(STATUS_LARGE_FONT_ID,status.c_str(),w-40);renderer.drawText(STATUS_LARGE_FONT_ID,20,y,line.c_str());}

}
std::string CaseFilesActivity::gameTitle()const{char text[80];snprintf(text,sizeof(text),tr(STR_CASE_NUMBER_TITLE),game.index+1);return text;}
void CaseFilesActivity::checkVictory(){if(game.solved()&&!victoryAnnounced){victoryAnnounced=true;victoryPopup=true;victoryChoice=0;cluePopup=false;confirmHold.reset();rightHold.reset();}}
std::vector<CaseFilesActivity::PopupLine> CaseFilesActivity::popupLines()const{
 std::vector<PopupLine> lines;const int width=renderer.getScreenWidth()-56;
 for(int i=0;i<int(game.puzzle.clues.size());++i){auto text=std::to_string(i+1)+". "+casefiles::clueText(game.puzzle,game.puzzle.clues[i]);for(auto&line:renderer.wrappedText(UI_10_FONT_ID,text.c_str(),width,100))lines.push_back({line,i});if(i+1<int(game.puzzle.clues.size()))lines.push_back({"",-1});}return lines;
}
int CaseFilesActivity::popupRows()const{const auto&m=UITheme::getInstance().getMetrics();int y=m.topPadding+m.headerHeight+18,height=renderer.getScreenHeight()-m.buttonHintsHeight-y-12;return std::max(1,(height-90-renderer.getLineHeight(STATUS_LARGE_FONT_ID))/(renderer.getLineHeight(UI_10_FONT_ID)+4));}
void CaseFilesActivity::drawCluePopup(){
 const auto&m=UITheme::getInstance().getMetrics();const int w=renderer.getScreenWidth(),h=renderer.getScreenHeight(),x=14,y=m.topPadding+m.headerHeight+18,width=w-28,height=h-m.buttonHintsHeight-y-12;
 renderer.fillRoundedRect(x,y,width,height,12,Color::White);renderer.fillRectDither(x+2,y+2,width-4,48,Color::LightGray);renderer.drawRoundedRect(x,y,width,height,3,12,true);
 auto title=gameTitle()+" — "+tr(STR_CASE_CLUES);renderer.drawCenteredText(UI_10_FONT_ID,y+10,title.c_str());
 auto lines=popupLines();const int rows=popupRows();popupLine=std::clamp(popupLine,0,std::max(0,int(lines.size())-rows));int yy=y+62,lh=renderer.getLineHeight(UI_10_FONT_ID)+4;
 for(int i=popupLine;i<std::min(int(lines.size()),popupLine+rows);++i){renderer.drawText(UI_10_FONT_ID,x+14,yy,lines[i].text.c_str());if(lines[i].clue>=0&&game.struck[lines[i].clue])renderer.drawLine(x+14,yy+renderer.getTextHeight(UI_10_FONT_ID)/2,x+width-14,yy+renderer.getTextHeight(UI_10_FONT_ID)/2);yy+=lh;}
 auto help=renderer.truncatedText(STATUS_LARGE_FONT_ID,tr(STR_CASE_POPUP_SCROLL),width-28);renderer.drawText(STATUS_LARGE_FONT_ID,x+14,y+height-renderer.getLineHeight(STATUS_LARGE_FONT_ID)-10,help.c_str());
}
void CaseFilesActivity::drawVictoryPopup(){
 const auto&m=UITheme::getInstance().getMetrics();const int w=renderer.getScreenWidth(),h=renderer.getScreenHeight(),x=24,width=w-48;
 int lh=renderer.getLineHeight(UI_10_FONT_ID)+4,buttonH=lh+18,height=68+2*lh+2*buttonH+24,y=(h-m.buttonHintsHeight-height)/2;
 renderer.fillRoundedRect(x,y,width,height,12,Color::White);renderer.fillRectDither(x+2,y+2,width-4,48,Color::LightGray);renderer.drawRoundedRect(x,y,width,height,3,12,true);
 auto title=gameTitle();renderer.drawCenteredText(UI_10_FONT_ID,y+10,title.c_str());
 int yy=y+62;for(const auto&line:renderer.wrappedText(UI_10_FONT_ID,saveFailed?tr(STR_CASE_SAVE_ERROR):tr(STR_CASE_VICTORY_MESSAGE),width-32,2)){renderer.drawText(UI_10_FONT_ID,x+16,yy,line.c_str());yy+=lh;}
 const char*labels[]={tr(STR_CASE_NEXT),tr(STR_CASE_VICTORY_MENU)};int by=y+68+2*lh;
 for(int i=0;i<2;++i){bool active=i==victoryChoice;if(active)renderer.fillRoundedRect(x+14,by,width-28,buttonH,6,Color::Black);else renderer.drawRoundedRect(x+14,by,width-28,buttonH,2,6,true);auto text=renderer.truncatedText(UI_10_FONT_ID,labels[i],width-48);renderer.drawText(UI_10_FONT_ID,x+(width-renderer.getTextWidth(UI_10_FONT_ID,text.c_str()))/2,by+8,text.c_str(),!active);by+=buttonH+6;}
}

void CaseFilesActivity::drawClue(){
 const auto&m=UITheme::getInstance().getMetrics();int y=m.topPadding+m.headerHeight+12,w=renderer.getScreenWidth(),h=renderer.getScreenHeight(),lh=renderer.getLineHeight(UI_10_FONT_ID)+4;
 char label[80];snprintf(label,sizeof(label),tr(STR_CASE_CLUE_COUNT),clue+1,int(game.puzzle.clues.size()));renderer.drawText(UI_10_FONT_ID,20,y,label);y+=lh+8;
 for(const char* context:{casefiles::themeStory(game.puzzle),tr(STR_CASE_GOAL)}) {
  for(const auto& line:renderer.wrappedText(STATUS_LARGE_FONT_ID,context,w-40,3)) {
   renderer.drawText(STATUS_LARGE_FONT_ID,20,y,line.c_str());y+=renderer.getLineHeight(STATUS_LARGE_FONT_ID);
  }
  y+=6;
 }
 renderer.drawLine(20,y,w-20,y);y+=12;
 auto text=casefiles::clueText(game.puzzle,game.puzzle.clues[clue]);
 int maxLines=std::max(1,(h-m.buttonHintsHeight-20-y)/lh);
 for(const auto&line:renderer.wrappedText(UI_10_FONT_ID,text.c_str(),w-40,maxLines)){renderer.drawText(UI_10_FONT_ID,20,y,line.c_str());if(game.struck[clue])renderer.drawLine(20,y+renderer.getTextHeight(UI_10_FONT_ID)/2,w-20,y+renderer.getTextHeight(UI_10_FONT_ID)/2);y+=lh;}
}
void CaseFilesActivity::drawRules(){const auto&m=UITheme::getInstance().getMetrics();int y=m.topPadding+m.headerHeight+20,lh=renderer.getLineHeight(UI_10_FONT_ID)+3;auto lines=renderer.wrappedText(UI_10_FONT_ID,tr(STR_CASE_RULES_TEXT),renderer.getScreenWidth()-40,120);int n=std::max(1,(renderer.getScreenHeight()-m.buttonHintsHeight-y-12)/lh);for(int i=helpLine;i<int(lines.size())&&i<helpLine+n;++i){renderer.drawText(UI_10_FONT_ID,20,y,lines[i].c_str());y+=lh;}}
void CaseFilesActivity::render(RenderLock&&){
 renderer.setOrientation(GfxRenderer::Orientation::Portrait);renderer.clearScreen();const auto&m=UITheme::getInstance().getMetrics();int w=renderer.getScreenWidth(),h=renderer.getScreenHeight();
 std::string title=gameTitle();GUI.drawHeader(renderer,Rect{0,m.topPadding,w,m.headerHeight},title.c_str(),I18N.get(levelId(game.level)));
 if(screen==Screen::Playing){drawGrid();if(cluePopup)drawCluePopup();if(victoryPopup)drawVictoryPopup();}else if(screen==Screen::Clues)drawClue();else if(screen==Screen::Rules)drawRules();
 else if(screen==Screen::ConfirmRestart){int y=m.topPadding+m.headerHeight+24;for(auto&line:renderer.wrappedText(UI_10_FONT_ID,tr(STR_CASE_RESET_QUESTION),w-40,4)){renderer.drawText(UI_10_FONT_ID,20,y,line.c_str());y+=renderer.getLineHeight(UI_10_FONT_ID)+3;}}
 else{
  int top=m.topPadding+m.headerHeight+12;int count=screen==Screen::Menu?12:screen==Screen::Tables?game.pairCount:3;
  if(screen==Screen::Menu){char b[96];snprintf(b,sizeof(b),tr(STR_CASE_PROGRESS),game.completedCount(game.level));renderer.drawText(STATUS_LARGE_FONT_ID,20,top,b);top+=renderer.getLineHeight(STATUS_LARGE_FONT_ID)+10;}
  constexpr StrId menu[]={StrId::STR_GAME_CONTINUE,StrId::STR_CASE_CLUES,StrId::STR_CASE_TABLES,StrId::STR_CASE_UNDO,StrId::STR_CASE_CHECK,StrId::STR_CASE_HINT,StrId::STR_CASE_NEXT,StrId::STR_CASE_PREVIOUS,StrId::STR_CASE_LEVEL,StrId::STR_CASE_RESTART,StrId::STR_CASE_RULES,StrId::STR_GAME_EXIT};
  GUI.drawList(renderer,Rect{0,top,w,h-top-m.buttonHintsHeight-m.verticalSpacing},count,selected,[&](int i){if(screen==Screen::Menu)return std::string(I18N.get(menu[i]));if(screen==Screen::Levels)return std::string(I18N.get(levelId(i)));auto pair=game.pairs[i];return std::string(casefiles::category(game.puzzle,pair.a))+" / "+casefiles::category(game.puzzle,pair.b);});
 }
 if(screen==Screen::Playing){
  if(victoryPopup)hints(tr(STR_CASE_MENU),tr(STR_SELECT),tr(STR_DIR_UP),tr(STR_DIR_DOWN));
  else if(cluePopup)hints(tr(STR_BACK),tr(STR_CASE_NEXT_SHORT),tr(STR_DIR_UP),tr(STR_DIR_DOWN));
  else hints(tr(STR_CASE_MENU),focus==Focus::Grid?tr(STR_CASE_MARK):focus==Focus::Clues?tr(STR_CASE_STRIKE):tr(STR_CASE_BOARD_FIELD),tr(STR_DIR_LEFT),tr(STR_DIR_RIGHT));
 }
 else if(screen==Screen::Clues)hints(tr(STR_BACK),game.struck[clue]?tr(STR_CASE_RESTORE):tr(STR_CASE_STRIKE),tr(STR_CASE_PREV),tr(STR_CASE_NEXT_SHORT));
 else hints(tr(STR_BACK),screen==Screen::ConfirmRestart?tr(STR_YES):tr(STR_SELECT),tr(STR_DIR_UP),tr(STR_DIR_DOWN));
 renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
