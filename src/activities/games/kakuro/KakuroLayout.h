#pragma once
#include <algorithm>
#include <array>
namespace kakuro_ui {
// Empty no-clue blocks connected to the edge stay blank; enclosed ones need a box.
template<class Board> std::array<bool,64> outsideBlocks(const Board& b){
 std::array<bool,64> outside{};std::array<int,64> queue{};int first=0,last=0;
 const auto add=[&](int r,int c){if(r<0||c<0||r>=b.rows||c>=b.columns)return;int i=Board::idx(r,c);if(outside[i]||b.playable[i]||b.clueRight[i]||b.clueDown[i])return;outside[i]=true;queue[last++]=i;};
 for(int r=0;r<b.rows;++r)for(int c=0;c<b.columns;++c)if(r==0||c==0||r==b.rows-1||c==b.columns-1)add(r,c);
 while(first<last){int i=queue[first++],r=i/8,c=i%8;add(r-1,c);add(r+1,c);add(r,c-1);add(r,c+1);}
 return outside;
}
inline int hintBlockTop(int paletteY,int lineHeight,int lineCount){
 return paletteY-std::clamp(lineCount,2,6)*lineHeight-8;
}
struct Layout {int top=76,cell=0,left=0,gridBottom=0,combinationY=0,paletteY=0,paletteCell=0,paletteHeight=0;};
inline Layout layout(int width,int height,int rows,int columns,int lineHeight,int hintHeight){
 Layout v;const int bottom=height-hintHeight-8;
 v.paletteHeight=92;v.paletteY=bottom-v.paletteHeight;v.paletteCell=(width-48)/5;
 // Keep the six preview lines directly above the number palette.
 const int comboHeight=6*lineHeight;
 v.combinationY=v.paletteY-comboHeight-12;
 // Centre every board in the same available rectangle, with air below the header.
 const int gridTop=94,gridBottom=v.combinationY-18;
 const int availableHeight=std::max(1,gridBottom-gridTop);
 v.cell=std::max(1,std::min({(width-32)/columns,availableHeight/rows,64}));
 v.left=(width-v.cell*columns)/2;
 v.top=gridTop+(availableHeight-v.cell*rows)/2;
 v.gridBottom=v.top+v.cell*rows;return v;
}
}
