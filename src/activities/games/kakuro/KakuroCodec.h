#pragma once
#include "KakuroStore.h"
#include <vector>
namespace kakuro_codec {
using Bytes=std::vector<uint8_t>;
inline uint32_t hash(const Bytes&b,size_t n){uint32_t h=2166136261u;for(size_t i=0;i<n;++i){h^=b[i];h*=16777619u;}return h;}
inline void u16(Bytes&b,uint16_t n){b.push_back(n&255);b.push_back(n>>8);}
inline uint16_t get16(const Bytes&b,size_t&i){auto n=b[i]|(b[i+1]<<8);i+=2;return n;}
inline void seal(Bytes&b){auto h=hash(b,b.size());for(int i=0;i<4;++i)b.push_back(h>>(8*i));}
inline bool valid(const Bytes&b){if(b.size()<8)return false;uint32_t n=0;for(int i=0;i<4;++i)n|=uint32_t(b[b.size()-4+i])<<(8*i);return n==hash(b,b.size()-4);}
inline bool decode(const Bytes&b,KakuroSaveSlot&o){
 if(!valid(b)||b.size()!=140||b[0]!='K'||b[1]!=2||b[2]>2||b[3]>=200)return false;
 KakuroSaveSlot t;t.difficulty=static_cast<KakuroBoard::Difficulty>(b[2]);t.variant=b[3];size_t i=4;t.elapsedSec=get16(b,i);t.cursorRow=b[i++];t.cursorColumn=b[i++];
 if(!t.board.build(t.difficulty,t.variant)||!t.board.isPlayable(t.cursorRow,t.cursorColumn))return false;
 for(int j=0;j<64;++j){auto digit=b[i++];if(digit>9||(!t.board.playable[j]&&digit))return false;t.board.user[j]=digit;}
 for(int j=0;j<64;++j){auto hint=b[i++];if(hint>1||(hint&&(!t.board.playable[j]||t.board.user[j]!=t.board.solution[j])))return false;t.board.hinted[j]=hint;}
 o=t;return true;
}
}
