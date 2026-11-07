#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>
#include <array>

namespace mix {
struct Levels { int pc=100, chat=100; };
inline uint8_t hardwareVolume(int percent){return (uint8_t)std::lround((100-std::clamp(percent,0,100))*56./100);}
inline int hardwarePercent(int raw){return (int)std::lround((56-std::clamp(raw,0,56))*100./56);}
// The target survives the USB send until feedback arrives. Old replies cannot
// become the starting point of a new keyboard pulse during this interval.
struct VolumeControl {
 int targetRaw=-1;uint64_t guardUntil=0;bool awaiting=false;
 void reset(){targetRaw=-1;guardUntil=0;awaiting=false;}
 void request(int percent,uint64_t now){targetRaw=hardwareVolume(percent);guardUntil=now+1500;awaiting=true;}
 int baseline(int physical,uint64_t now)const{return targetRaw>=0&&now<guardUntil?hardwarePercent(targetRaw):physical;}
 int step(int physical,int ticks,uint64_t now){int raw=targetRaw>=0&&now<guardUntil?targetRaw:hardwareVolume(physical);targetRaw=std::clamp(raw-ticks,0,56);guardUntil=now+1500;awaiting=true;return hardwarePercent(targetRaw);}
 bool observe(int percent,uint64_t now){
  int raw=hardwareVolume(percent);
  if(targetRaw>=0&&now<guardUntil&&raw!=targetRaw)return false;
  if(awaiting&&raw==targetRaw){awaiting=false;guardUntil=std::max(guardUntil,now+350);}
  else if(now>=guardUntil){reset();}
  return true;
 }
};
struct VolumeDisplay {int percent=-1;bool pending=false;};
inline VolumeDisplay volumeDisplay(int confirmed,const VolumeControl& control,uint64_t now){
 bool pending=control.awaiting&&control.targetRaw>=0&&now<control.guardUntil;
 return {pending?hardwarePercent(control.targetRaw):confirmed,pending};
}
// Repaint even an unchanged frame while firmware may draw its own volume popup.
// Idle traffic stays at one frame per second; failures retain a retry backoff.
struct OledRefresh {
 uint64_t activeUntil=0,next=0,last=0,retryUntil=0;bool painted=false;
 void activity(uint64_t now){activeUntil=now+2200;if(now>=retryUntil)next=std::min(next,now);}
 bool due(uint64_t now,bool changed,bool active)const{
  if(now<retryUntil||(painted&&now-last<75))return false;
  return !active||changed||now>=next;
 }
 void sent(uint64_t now,bool success){last=now;painted=true;retryUntil=success?0:now+3000;next=success?now+(now<activeUntil?75:1000):retryUntil;}
};
inline bool same(Levels a,Levels b){return a.pc==b.pc&&a.chat==b.chat;}
inline std::optional<Levels> baseMix(const std::vector<uint8_t>& b){
 size_t o=!b.empty()&&b[0]==0?1:0;
 if(b.size()<o+22||b[o]!=6||b[o+1]!=0x20||b[o+20]>100||b[o+21]>100)return {};
 return Levels{b[o+20],b[o+21]};
}
struct MixEcho {
 Levels expected;uint64_t until=0;
 void sent(Levels v,uint64_t now){expected=v;until=now+300;}
 bool stale(Levels v,uint64_t now)const{return now<until&&!same(v,expected);}
};
inline Levels balance(int value) {
 value=std::clamp(value,-100,100);
 return {value>0?100-value:100,value<0?100+value:100};
}
// Endpoint-preserving curve, not a timer or synthetic wheel ticks.
// It gives smaller changes near silence and unity; mid-range steps are wider.
inline int gentlePercent(int raw) {
 double x=std::clamp(raw,0,100)/100.;
 return (int)std::lround(100*x*x*(3-2*x));
}
inline Levels wheelLevels(Levels raw,bool gentle) {
 return gentle?Levels{gentlePercent(raw.pc),gentlePercent(raw.chat)}:raw;
}
inline int position(Levels v) { return v.pc<v.chat?100-v.pc:v.chat-100; }
inline float gain(float base,int group,int master) {
 return std::clamp(base,0.f,1.f)*std::clamp(group,0,100)/100.f*std::clamp(master,0,100)/100.f;
}
struct Baseline {
 float original=0,expected=0;
 bool controlled=false;
 float apply(float current,int group,int master) {
  if(!controlled || std::abs(current-expected)>.005f)original=current;
  expected=gain(original,group,master);controlled=true;return expected;
 }
 std::optional<float> restore(float current)const {
  return controlled&&std::abs(current-expected)<=.005f?std::optional<float>(original):std::nullopt;
 }
};
inline std::optional<Levels> packet(const std::vector<uint8_t>& b) {
 size_t o=!b.empty()&&b[0]==0?1:0;
 if(b.size()<o+4 || b[o]!=7 || b[o+1]!=0x45 || b[o+2]>100 || b[o+3]>100)return {};
 return Levels{b[o+2],b[o+3]};
}
inline std::optional<int> physical(const std::vector<uint8_t>& b) {
 size_t o=!b.empty()&&b[0]==0?1:0;
 if(b.size()<o+3)return {};
 if(b[o]==7&&b[o+1]==0x25&&b[o+2]<=56)return (int)std::lround((56-b[o+2])*100./56);
 if(b.size()>=o+4&&b[o]==6&&b[o+1]==0x20&&b[o+3]<=56)return (int)std::lround((56-b[o+3])*100./56);
 return {};
}
inline int batteryPercent(uint8_t raw){return raw<=8?(int)raw*100/8:-1;}
inline int chargingState(uint8_t raw){return raw==1||raw==2||raw==4||raw==8?raw:-1;}
struct BatteryStatus {int battery=-1,charger=-1,charging=-1;};
inline std::optional<BatteryStatus> batteryStatus(const std::vector<uint8_t>& b){
 size_t o=!b.empty()&&b[0]==0?1:0;
 if(b.size()<o+5||(b[o]!=6&&b[o]!=7)||b[o+1]!=0xb7)return {};
 int charging=chargingState(b[o+4]);
 return BatteryStatus{charging==2||charging==4||charging==8?batteryPercent(b[o+2]):-1,batteryPercent(b[o+3]),charging};
}
struct Status {int mic=-1,battery=-1,charger=-1,charging=-1;};
inline std::optional<int> micEvent(const std::vector<uint8_t>& b) {
 size_t o=!b.empty()&&b[0]==0?1:0;
 if(b.size()>=o+3&&b[o]==7&&b[o+1]==0xbb&&b[o+2]<=1)return b[o+2];
 return {};
}
inline std::optional<Status> status(const std::vector<uint8_t>& b) {
 size_t o=!b.empty()&&b[0]==0?1:0;
 if(b.size()<o+16||(b[o]!=6&&b[o]!=7)||b[o+1]!=0xb0)return {};
 int charging=chargingState(b[o+15]);bool online=charging==2||charging==4||charging==8;
 return Status{online?(b[o+9]?1:0):-1,online?batteryPercent(b[o+6]):-1,batteryPercent(b[o+7]),charging};
}
inline std::vector<uint8_t> oledHalf(const std::vector<bool>& pixels,int half,size_t reportSize) {
 if(pixels.size()!=128*64 || half<0||half>1||reportSize<518)return {};
 std::vector<uint8_t> out(reportSize);out[0]=6;out[1]=0x93;out[2]=(uint8_t)(half*64);out[4]=64;out[5]=64;
 for(int x=0;x<64;x++)for(int y=0;y<64;y++)if(pixels[y*128+half*64+x])out[6+x*8+y/8]|=(uint8_t)(1<<(y%8));
 return out;
}
}
