#pragma once
#include "mix.hpp"
#include <array>
#include <string>

namespace mix {
// Original 5x7 bitmap lettering. No vendor assets or firmware are used.
inline std::array<unsigned char,7> oledGlyph(char c) {
 switch(c){
 case '0':return {14,17,19,21,25,17,14};case '1':return {4,12,4,4,4,4,14};
 case '2':return {14,17,1,2,4,8,31};case '3':return {30,1,1,14,1,1,30};
 case '4':return {2,6,10,18,31,2,2};case '5':return {31,16,16,30,1,1,30};
 case '6':return {14,16,16,30,17,17,14};case '7':return {31,1,2,4,8,8,8};
 case '8':return {14,17,17,14,17,17,14};case '9':return {14,17,17,15,1,1,14};
 case 'A':return {14,17,17,31,17,17,17};case 'C':return {14,17,16,16,16,17,14};
 case 'B':return {30,17,17,30,17,17,30};case 'E':return {31,16,16,30,16,16,31};case 'F':return {31,16,16,30,16,16,16};case 'S':return {15,16,16,14,1,1,30};
 case 'H':return {17,17,17,31,17,17,17};case 'I':return {14,4,4,4,4,4,14};case 'L':return {16,16,16,16,16,16,31};
 case 'M':return {17,27,21,21,17,17,17};case 'N':return {17,25,25,21,19,19,17};
 case 'O':return {14,17,17,17,17,17,14};case 'P':return {30,17,17,30,16,16,16};
 case 'T':return {31,4,4,4,4,4,4};case 'U':return {17,17,17,17,17,17,14};
 case 'V':return {17,17,17,17,17,10,4};case 'X':return {17,17,10,4,10,17,17};
 case '*':return {0,21,14,31,14,21,0};
 case '%':return {17,2,4,4,8,16,17};case '-':return {0,0,0,31,0,0,0};
 default:return {};
 }
}
inline std::vector<bool> oledMixPixels(Levels levels,int mic,int battery=-1,int charger=-1,int charging=-1,int shift=0,int physical=-1,bool volumePending=false,bool english=false) {
 std::vector<bool> pixels(128*64);
 auto text=[&](int x,int y,const std::string& value){for(char c:value){auto glyph=oledGlyph(c);for(int row=0;row<7;row++)for(int col=0;col<5;col++)if((glyph[row]&(16>>col))&&x+col>=0&&x+col<128&&y+row>=0&&y+row<64)pixels[(y+row)*128+x+col]=true;x+=6;}};
 auto bar=[&](int y,int value){int width=std::clamp(value,0,100)*120/100;for(int row=0;row<4;row++)for(int x=0;x<120;x++)pixels[(y+row)*128+4+x]=row==0||row==3||x<width||x==119;};
 text(4,1,"NOVAMIX");text(mic==1?76:88,1,mic<0?"MIC --":mic?(english?"MIC MUTE":"MIC MUTO"):"MIC ON");
 text(4,13,"PC");text(100,13,"CHAT");
 // Left means PC, centre means both at full level, right means chat.
 int marker=5+(std::clamp(position(levels),-100,100)+100)*116/200;
 for(int x=4;x<124;x++)pixels[24*128+x]=true;
 for(int y=21;y<=27;y++){pixels[y*128+63]=true;for(int x=marker-1;x<=marker+1;x++)pixels[y*128+x]=true;}
 text(4,33,"VOL");text(100-6*(int)std::to_string(std::max(0,physical)).size(),33,physical<0?"--":std::to_string(physical)+"%");bar(43,std::max(0,physical));if(volumePending)text(111,33,"*");
 text(4,54,(english?"HS ":"CUF ")+(battery<0?std::string("--"):std::to_string(battery)+"%"));
 text(70,54,"BASE"+(charger<0?std::string("--"):std::to_string(charger)+"%"));
 if(charging==2){for(int y=0;y<7;y++){int x=y<3?60-y/2:58+(6-y)/2;pixels[(53+y)*128+x]=true;pixels[(53+y)*128+x+1]=true;}}
 if(shift){auto original=pixels;std::fill(pixels.begin(),pixels.end(),false);for(int y=0;y<63;y++)for(int x=0;x<127;x++)pixels[(y+1)*128+x+1]=original[y*128+x];}
 return pixels;
}
}
