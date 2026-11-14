#define UNICODE
#define _UNICODE
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <winioctl.h>
#include <hidclass.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <thread>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <deque>
#include <dwmapi.h>
#include <bcrypt.h>
#include <functional>
#include <atomic>
#include "mix.hpp"
#include "oled.hpp"
#include "tray_status.hpp"
using Microsoft::WRL::ComPtr;
namespace fs=std::filesystem;
constexpr UINT UPDATE=WM_APP+1,TRAY=WM_APP+2;
constexpr UINT OPEN_UI=WM_APP+3,EXIT_APP=WM_APP+4;
constexpr UINT GET_VERSION=WM_APP+20;constexpr LRESULT RELEASE=20;
std::function<void()> openWebUi;
std::atomic<bool> preferencesDirty=false;

HWND windowH=nullptr,osdH=nullptr;
HANDLE stopEvent=nullptr,audioWake=nullptr;
HFONT fontNormal,fontSmall,fontTitle,fontNumber;
HBRUSH background,cardBrush;
bool hotkeys=false,smoke=false,preview=false,serviceTest=false;
unsigned unchangedUpdatesSuppressed=0;
std::wstring exeDir() {wchar_t b[32768];GetModuleFileNameW(nullptr,b,32768);return fs::path(b).parent_path().wstring();}
std::wstring errorText(HRESULT hr) {
 wchar_t* b=nullptr;FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,(DWORD)hr,0,(LPWSTR)&b,0,nullptr);
 std::wostringstream s;s<<L"0x"<<std::hex<<std::uppercase<<(uint32_t)hr;if(b){s<<L" — "<<b;LocalFree(b);}return s.str();
}
struct Failure {std::wstring message;};
void check(HRESULT hr,const wchar_t* action) {if(FAILED(hr))throw Failure{std::wstring(action)+L": "+errorText(hr)};}
std::wstring lower(std::wstring s){for(auto& c:s)c=(wchar_t)towlower(c);return s;}
struct State {
 bool enabled=false,wantEnabled=false,hid=false,audio=false,fromHardware=false;
 bool diagnose=false, english=PRIMARYLANGID(GetUserDefaultUILanguage())!=LANG_ITALIAN;
 int master=100,physical=-1,mic=-1,battery=-1,chargerBattery=-1,charging=-1,wantPhysical=-1,osdKind=0;mix::Levels levels;
 mix::VolumeControl volumeControl;
 std::optional<mix::Levels> wantMix,baseLevels;unsigned long long mixRevision=0,volumeRevision=0;
 std::wstring mixSyncError,volumeSyncError,audioDetails;
 unsigned long long audioCommandRevision=0;bool gentleMix=true;int accent=0;ULONGLONG lastMixEvent=0;
 bool oledMix=true,oledHints=false,overlay=true,keyboardWanted=false,keyboardActive=false;unsigned long long osdSerial=0;
 trayStatus::BatterySettings trayBattery;
 std::wstring endpoint=L"Arctis Nova Pro Wireless",audioError,hidError,hidDetails,notice=L"Pronto. Premi Attiva per iniziare.";
 unsigned long long packets=0,mixPackets=0;
};
State shared;std::mutex mutexState;
struct WheelSample{ULONGLONG time;mix::Levels raw;};
std::deque<WheelSample> wheelSamples;
std::wstring wheelReport(){
 std::deque<WheelSample> samples;{std::lock_guard<std::mutex> lock(mutexState);samples=wheelSamples;}
 std::wostringstream out;out<<L"\nEventi rotella ChatMix grezzi (inclusi duplicati, esclusi OLED/poll):\n";
 for(auto& sample:samples)out<<sample.time<<L" ms PC="<<sample.raw.pc<<L" Chat="<<sample.raw.chat<<L"\n";
 return out.str();
}
State snapshot(){std::lock_guard<std::mutex> lock(mutexState);return shared;}
void notify(){if(windowH)PostMessageW(windowH,UPDATE,0,0);}
void wakeAudio(){if(audioWake)SetEvent(audioWake);}

// Caller holds mutexState. Timestamp records command submission, not sound onset.
void markAudioCommand(){++shared.audioCommandRevision;}

// Runs on the input reader. No USB output or application enumeration here.
void applyMixInput(const std::vector<uint8_t>& bytes){
 auto event=mix::packet(bytes);auto poll=mix::baseMix(bytes);
 auto raw=event?event:poll;if(!raw)return;
 bool applied=false;
 {std::lock_guard<std::mutex> lock(mutexState);
  // A queued settings reply may describe the position before a newer wheel event.
  // Wheel events are always immediate; only poll reconciliation is held back.
  auto now=GetTickCount64();
  if(poll&&shared.lastMixEvent&&now-shared.lastMixEvent<1000)return;
  if(event){shared.lastMixEvent=now;if(wheelSamples.size()>=128)wheelSamples.pop_front();wheelSamples.push_back({now,*raw});}
  bool changed=shared.baseLevels&&!mix::same(*shared.baseLevels,*raw);
  bool initial=!shared.baseLevels&&(event||shared.mixRevision==0);
  shared.baseLevels=*raw;if(event)++shared.mixPackets;
  if(shared.enabled&&!shared.wantMix&&(changed||initial)){
   auto levels=mix::wheelLevels(*raw,shared.gentleMix);
   applied=!mix::same(shared.levels,levels);shared.levels=levels;
   shared.mixSyncError.clear();shared.fromHardware=true;
   if(applied){markAudioCommand();shared.osdKind=1;++shared.osdSerial;preferencesDirty=true;}
  }
 }
 if(applied){wakeAudio();notify();}
}
class Hid {
 HANDLE h=INVALID_HANDLE_VALUE,infoHandle=INVALID_HANDLE_VALUE;USHORT outputSize=0,infoSize=0,featureSize=0;bool preferControl=false,infoPreferControl=false;
 struct Input {
  HANDLE handle=INVALID_HANDLE_VALUE;OVERLAPPED op{};bool pending=false;std::vector<uint8_t> bytes;std::wstring label;
  ~Input(){if(handle!=INVALID_HANDLE_VALUE){CancelIoEx(handle,nullptr);if(pending){DWORD n;GetOverlappedResult(handle,&op,&n,TRUE);}CloseHandle(handle);}if(op.hEvent)CloseHandle(op.hEvent);}
 };
 std::vector<std::unique_ptr<Input>> inputs;
 std::deque<std::wstring> trace;mutable std::mutex traceMutex;std::mutex incomingMutex;
 std::deque<std::vector<uint8_t>> incoming;std::thread reader;HANDLE readerStop=nullptr;std::atomic<bool> anyInputAlive=false;
 void runReader(){
  while(WaitForSingleObject(readerStop,0)==WAIT_TIMEOUT){
   bool alive=false;std::vector<HANDLE> waits{readerStop};
   for(auto& input:inputs){
    for(int n=0;n<32&&input->handle!=INVALID_HANDLE_VALUE;n++){
     auto bytes=read(*input);if(bytes.empty())break;
     std::wostringstream line;line<<input->label<<L" len="<<bytes.size()<<L" :";for(auto b:bytes)line<<L" "<<std::hex<<std::setw(2)<<std::setfill(L'0')<<(int)b;record(line.str());
     if(onInput)onInput(bytes);
     {std::lock_guard<std::mutex> lock(incomingMutex);if(incoming.size()>=256)incoming.pop_front();incoming.push_back(std::move(bytes));}
    }
    if(input->handle!=INVALID_HANDLE_VALUE){alive=true;if(input->pending)waits.push_back(input->op.hEvent);}
   }
   anyInputAlive=alive;
   // Input completions wake this thread independently of blocked OLED writes.
   WaitForMultipleObjects((DWORD)waits.size(),waits.data(),FALSE,10);
  }
 }

 std::vector<uint8_t> read(Input& input){
  if(input.handle==INVALID_HANDLE_VALUE)return {};DWORD n=0;
  if(!input.pending){ResetEvent(input.op.hEvent);BOOL done=ReadFile(input.handle,input.bytes.data(),(DWORD)input.bytes.size(),&n,&input.op);
   if(done)return {input.bytes.begin(),input.bytes.begin()+n};
   if(GetLastError()!=ERROR_IO_PENDING){record(input.label+L" errore lettura "+errorText(HRESULT_FROM_WIN32(GetLastError())));CloseHandle(input.handle);input.handle=INVALID_HANDLE_VALUE;return {};}
   input.pending=true;
  }
  if(WaitForSingleObject(input.op.hEvent,0)!=WAIT_OBJECT_0)return {};
  input.pending=false;if(!GetOverlappedResult(input.handle,&input.op,&n,FALSE)){record(input.label+L" completamento lettura "+errorText(HRESULT_FROM_WIN32(GetLastError())));CloseHandle(input.handle);input.handle=INVALID_HANDLE_VALUE;return {};}
  return {input.bytes.begin(),input.bytes.begin()+n};
 }
 void record(const std::wstring& line){std::lock_guard<std::mutex> lock(traceMutex);trace.push_back(std::to_wstring(GetTickCount64())+L" "+line);if(trace.size()>48)trace.pop_front();}
public:
 std::function<void(const std::vector<uint8_t>&)> onInput;
 std::wstring info,details;size_t found=0;
 ~Hid(){close();}
 bool connected()const{return h!=INVALID_HANDLE_VALUE;}
 bool inputAlive()const{return anyInputAlive.load();}
 std::wstring diagnostics()const {std::lock_guard<std::mutex> lock(traceMutex);std::wstring out=details+L"\nUltimo invio: "+info+L"\nCanali di ingresso: "+std::to_wstring(inputs.size())+L"\nUltimi pacchetti/eventi USB (hex):\n";for(auto& line:trace)out+=line+L"\n";return out;}
 void close(){
  if(reader.joinable()){SetEvent(readerStop);reader.join();}if(readerStop){CloseHandle(readerStop);readerStop=nullptr;}anyInputAlive=false;
  {std::lock_guard<std::mutex> lock(incomingMutex);incoming.clear();}
  inputs.clear();
  if(connected()){CancelIoEx(h,nullptr);CloseHandle(h);h=INVALID_HANDLE_VALUE;}
  if(infoHandle!=INVALID_HANDLE_VALUE){CancelIoEx(infoHandle,nullptr);CloseHandle(infoHandle);infoHandle=INVALID_HANDLE_VALUE;}
 }
 bool open(){
  close();found=0;details.clear();GUID guid;HidD_GetHidGuid(&guid);
  HDEVINFO list=SetupDiGetClassDevsW(&guid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
  if(list==INVALID_HANDLE_VALUE){info=L"Elenco USB: "+errorText(HRESULT_FROM_WIN32(GetLastError()));return false;}
  SP_DEVICE_INTERFACE_DATA data{};data.cbSize=sizeof(data);std::wstring errors;
  for(DWORD i=0;SetupDiEnumDeviceInterfaces(list,nullptr,&guid,i,&data);i++){
   DWORD bytes=0;SetupDiGetDeviceInterfaceDetailW(list,&data,nullptr,0,&bytes,nullptr);std::vector<BYTE> buffer(bytes);
   auto detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());detail->cbSize=sizeof(*detail);
   if(!SetupDiGetDeviceInterfaceDetailW(list,&data,detail,bytes,nullptr,nullptr))continue;
   auto path=lower(detail->DevicePath);
   if(path.find(L"vid_1038&pid_12e0")==std::wstring::npos||path.find(L"mi_04")==std::wstring::npos)continue;
   ++found;
   // Inspect both top-level collections before selecting the control endpoint.
   HANDLE candidate=CreateFileW(detail->DevicePath,0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
   if(candidate==INVALID_HANDLE_VALUE){errors=L"Descrittore HID: "+errorText(HRESULT_FROM_WIN32(GetLastError()));continue;}
   PHIDP_PREPARSED_DATA prep=nullptr;HIDP_CAPS caps{};bool valid=false;
   if(HidD_GetPreparsedData(candidate,&prep)){valid=HidP_GetCaps(prep,&caps)==HIDP_STATUS_SUCCESS;HidD_FreePreparsedData(prep);}
   std::wostringstream description;description<<path<<L"\nUsage page: 0x"<<std::hex<<caps.UsagePage<<L" / usage: 0x"<<caps.Usage<<std::dec<<L" / input: "<<caps.InputReportByteLength<<L" / output: "<<caps.OutputReportByteLength<<L"\n";details+=description.str();
   CloseHandle(candidate);
   if(!valid||caps.Usage!=1||(caps.UsagePage!=0xffc0&&caps.UsagePage!=0xff00))continue;
   // Windows splits the top-level collections into separate input streams.
   // Listen to both, while sending commands only to the primary FFC0 collection.
   if(caps.InputReportByteLength>=4){
    auto input=std::make_unique<Input>();input->handle=CreateFileW(detail->DevicePath,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
    if(input->handle!=INVALID_HANDLE_VALUE){input->bytes.resize(caps.InputReportByteLength);input->op.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);std::wostringstream label;label<<L"RX page=0x"<<std::hex<<caps.UsagePage;input->label=label.str();if(input->op.hEvent){HidD_SetNumInputBuffers(input->handle,128);inputs.push_back(std::move(input));}}
    else details+=L"Apertura ingresso: "+errorText(HRESULT_FROM_WIN32(GetLastError()))+L"\n";
   }
   if(caps.UsagePage==0xffc0&&caps.OutputReportByteLength>=4&&!connected()){
    candidate=CreateFileW(detail->DevicePath,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
    if(candidate==INVALID_HANDLE_VALUE){errors=L"Apertura controllo Nova: "+errorText(HRESULT_FROM_WIN32(GetLastError()));continue;}
    h=candidate;outputSize=caps.OutputReportByteLength;featureSize=caps.FeatureReportByteLength;preferControl=false;info=L"Controllo HID collegato";
   }
   if(caps.UsagePage==0xff00&&caps.OutputReportByteLength>=4&&infoHandle==INVALID_HANDLE_VALUE){infoHandle=CreateFileW(detail->DevicePath,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);infoSize=caps.OutputReportByteLength;infoPreferControl=false;}
  }
  SetupDiDestroyDeviceInfoList(list);
  if(!connected())info=errors.empty()?(found?L"Nessuna collezione HID con report compatibili.":L"Base USB 1038:12E0 non trovata."):errors;
  if(!inputs.empty()){readerStop=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(readerStop){anyInputAlive=true;reader=std::thread([this]{runReader();});}}
  return connected();
 }
 DWORD transmit(std::vector<BYTE>& bytes,bool control,HANDLE target,DWORD request=IOCTL_HID_SET_OUTPUT_REPORT){
  OVERLAPPED op{};op.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!op.hEvent)return GetLastError();
  DWORD n=0;BOOL ok=control?DeviceIoControl(target,request,bytes.data(),(DWORD)bytes.size(),nullptr,0,&n,&op):WriteFile(target,bytes.data(),(DWORD)bytes.size(),&n,&op);
  DWORD result=ok?ERROR_SUCCESS:GetLastError();
  if(result==ERROR_IO_PENDING){
   DWORD wait=WaitForSingleObject(op.hEvent,120);
   if(wait!=WAIT_OBJECT_0){result=wait==WAIT_TIMEOUT?ERROR_TIMEOUT:GetLastError();CancelIoEx(target,&op);GetOverlappedResult(target,&op,&n,TRUE);}
   else result=GetOverlappedResult(target,&op,&n,FALSE)?ERROR_SUCCESS:GetLastError();
  }
  if(result==ERROR_SUCCESS&&!control&&n!=bytes.size())result=ERROR_WRITE_FAULT;
  CloseHandle(op.hEvent);return result;
 }
 bool command(BYTE command,std::initializer_list<BYTE> data={},bool infoRoute=false){
  HANDLE target=infoRoute&&infoHandle!=INVALID_HANDLE_VALUE?infoHandle:h;
  USHORT size=target==infoHandle?infoSize:outputSize;bool& preference=target==infoHandle?infoPreferControl:preferControl;
  if(target==INVALID_HANDLE_VALUE||size<2+data.size())return false;std::vector<BYTE> bytes(size);bytes[0]=6;bytes[1]=command;std::copy(data.begin(),data.end(),bytes.begin()+2);
  DWORD first=transmit(bytes,preference,target);
  if(first==ERROR_SUCCESS)return true;
  DWORD second=transmit(bytes,!preference,target);
  if(second==ERROR_SUCCESS){preference=!preference;return true;}
  info=L"Invio HID fallito: "+errorText(HRESULT_FROM_WIN32(first))+L"; alternativa: "+errorText(HRESULT_FROM_WIN32(second));
  return false;
 }
 bool send(BYTE command,BYTE value){return this->command(command,{value});}
 void noteSoftwareMix(mix::Levels v){record(L"ChatMix software PC="+std::to_wstring(v.pc)+L" chat="+std::to_wstring(v.chat)+L"; livelli nativi invariati; feedback OLED separato");}
 void recordVolumeIgnored(int percent,int raw){record(L"RX volume precedente ignorato="+std::to_wstring(percent)+L" targetRaw="+std::to_wstring(raw));}
 bool setPhysical(int percent){auto raw=mix::hardwareVolume(percent);bool sent=command(0x21,{0,raw});record(L"TX volume 06 21 00 attenuation="+std::to_wstring(raw)+(sent?L" ok":L" errore"));return sent;}
 void probe(bool includeVolume){
  bool status=command(0xb0);
  const wchar_t* volume=includeVolume?(command(0x20)?L"ok":L"errore"):L"—";
  bool battery=command(0xb7);
  record(std::wstring(L"TX probe FFC0 status=")+(status?L"ok":L"errore")+L" volume="+volume+L" battery="+(battery?L"ok":L"errore"));
 }
 bool pixelsOnScreen(const std::vector<bool>& pixels){
  if(!connected()||featureSize<518||pixels.size()!=128*64)return false;
  for(int half=0;half<2;half++){auto report=mix::oledHalf(pixels,half,featureSize);if(transmit(report,true,h,IOCTL_HID_SET_FEATURE)!=ERROR_SUCCESS)return false;}return true;
 }
 bool mixOnScreen(const State& s,int shift){auto volume=mix::volumeDisplay(s.physical,s.volumeControl,GetTickCount64());bool sent=pixelsOnScreen(mix::oledMixPixels(s.levels,s.mic,s.battery,s.chargerBattery,s.charging,shift,volume.percent,volume.pending,s.english));record(std::wstring(L"TX OLED ChatMix ")+(sent?L"ok":L"errore"));return sent;}
 bool micOnScreen(bool muted, bool english){
  if(!connected()||featureSize<518)return false;
  BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=128;bi.bmiHeader.biHeight=-64;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
  void* bits=nullptr;HDC dc=CreateCompatibleDC(nullptr);HBITMAP bmp=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,nullptr,0);if(!bmp||!bits){if(bmp)DeleteObject(bmp);DeleteDC(dc);return false;}
  auto old=SelectObject(dc,bmp);RECT r{0,0,128,64};FillRect(dc,&r,(HBRUSH)GetStockObject(BLACK_BRUSH));auto font=CreateFontW(-17,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");auto oldFont=SelectObject(dc,font);SetTextColor(dc,RGB(255,255,255));SetBkMode(dc,TRANSPARENT);DrawTextW(dc,muted?(english?L"MIC\nMUTED":L"MIC\nMUTO"):(english?L"MIC\nON":L"MIC\nATTIVO"),-1,&r,DT_CENTER|DT_WORDBREAK);
  GdiFlush();std::vector<bool> pixels(128*64);auto rgba=(DWORD*)bits;for(size_t i=0;i<pixels.size();i++)pixels[i]=(rgba[i]&0xffffff)!=0;
  SelectObject(dc,oldFont);SelectObject(dc,old);DeleteObject(font);DeleteObject(bmp);DeleteDC(dc);
  return pixelsOnScreen(pixels);
 }
 void restoreScreen(){command(0x95);}
 std::vector<uint8_t> poll(){
  std::lock_guard<std::mutex> lock(incomingMutex);if(incoming.empty())return {};auto bytes=std::move(incoming.front());incoming.pop_front();return bytes;
 }
 static int readerTest(){
  auto report=[](const char* text,int code){std::ofstream(fs::path(exeDir())/L"test-hid-reader.txt")<<text;return code;};
  std::wstring pipe=L"\\\\.\\pipe\\NovaMix.ReaderTest."+std::to_wstring(GetCurrentProcessId());
  HANDLE server=CreateNamedPipeW(pipe.c_str(),PIPE_ACCESS_INBOUND|FILE_FLAG_OVERLAPPED,PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT,1,64,64,0,nullptr);
  if(server==INVALID_HANDLE_VALUE)return report("FAIL: test input pipe unavailable",1);
  OVERLAPPED connection{};connection.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);ConnectNamedPipe(server,&connection);
  HANDLE client=CreateFileW(pipe.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
  if(client==INVALID_HANDLE_VALUE){CancelIoEx(server,nullptr);CloseHandle(server);CloseHandle(connection.hEvent);return report("FAIL: test input client unavailable",1);}
  WaitForSingleObject(connection.hEvent,1000);CloseHandle(connection.hEvent);
  HANDLE observed=CreateEventW(nullptr,TRUE,FALSE,nullptr),started=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr),finished=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  Hid hid;auto input=std::make_unique<Input>();input->handle=server;input->bytes.resize(64);input->op.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);input->label=L"RX mock pipe";hid.inputs.push_back(std::move(input));
  hid.onInput=[&](const std::vector<uint8_t>& bytes){applyMixInput(bytes);SetEvent(observed);};
  {std::lock_guard<std::mutex> lock(mutexState);shared.enabled=true;shared.gentleMix=false;shared.wantMix.reset();shared.levels={100,100};}
  hid.readerStop=CreateEventW(nullptr,TRUE,FALSE,nullptr);hid.anyInputAlive=true;hid.reader=std::thread([&]{hid.runReader();});
  std::thread writer([&]{SetEvent(started);WaitForSingleObject(release,2000);SetEvent(finished);});WaitForSingleObject(started,1000);
  BYTE bytes[64]{7,0x45,17,100};DWORD written=0;auto begin=GetTickCount64();bool sent=WriteFile(client,bytes,64,&written,nullptr)!=FALSE;
  bool received=WaitForSingleObject(observed,150)==WAIT_OBJECT_0;auto elapsed=GetTickCount64()-begin;
  bool independent=WaitForSingleObject(finished,0)==WAIT_TIMEOUT;bool parsed=mix::same(snapshot().levels,{17,100});
  SetEvent(release);writer.join();hid.close();CloseHandle(client);for(auto event:{observed,started,release,finished})CloseHandle(event);
  bool pass=sent&&written==64&&received&&independent&&parsed;
  std::ofstream(fs::path(exeDir())/L"test-hid-reader.txt")<<(pass?"PASS":"FAIL")<<": production overlapped input reader + ChatMix dispatch, pipe-backed mock device; output worker blocked; input applied after "<<elapsed<<" ms\n";return pass?0:1;
 }

};

#include "stream_audio.hpp"
#include "stream_tests.hpp"

bool writeReport(const fs::path& file,const State& s){
 std::wostringstream f;f<<L"NovaMix C++ 0.20 — diagnostica\n"<<L"Audio: "<<(s.audio?L"accessibile":L"non accessibile")<<L"\n"<<s.audioError<<L"\nHID: "<<(s.hid?L"accessibile":L"non accessibile")<<L"\n"<<s.hidError<<L"\nMix: PC "<<s.levels.pc<<L" Chat "<<s.levels.chat<<L"\nVolume cuffie: "<<s.physical<<L"\nMicrofono (1=muto, 0=attivo, -1=non disponibile): "<<s.mic<<L"\nPacchetti HID ricevuti: "<<s.packets<<L"\nEventi ChatMix interpretati: "<<s.mixPackets<<L"\n";
 f<<L"\nCurva ChatMix graduale: "<<s.gentleMix<<L"\n"<<s.audioDetails<<wheelReport()<<L"\nCollezioni HID esaminate:\n"<<s.hidDetails;
 auto wide=f.str();int n=WideCharToMultiByte(CP_UTF8,0,wide.data(),(int)wide.size(),nullptr,0,nullptr,nullptr);std::string bytes(n,'\0');WideCharToMultiByte(CP_UTF8,0,wide.data(),(int)wide.size(),bytes.data(),n,nullptr,nullptr);std::ofstream out(file,std::ios::binary);out<<bytes;return out.good();
}
void worker(){
 HRESULT co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
 if(FAILED(co)){std::lock_guard<std::mutex> lock(mutexState);shared.hidError=errorText(co);notify();return;}
 {
 Hid hid;hid.onInput=applyMixInput;bool enabled=false,hardwareEnabled=false,micFromEvent=false,oledActive=false;std::array<int,10> lastFrame{};mix::OledRefresh refresh;unsigned long long seenVolumeRevision=0;unsigned tick=0;ULONGLONG nextProbe=0,lastStatus=0,lastBattery=0,oledUntil=0,nextReadback=0;
 while(WaitForSingleObject(stopEvent,50)==WAIT_TIMEOUT){
  State current=snapshot();
  enabled=current.enabled;
  if(current.volumeRevision!=seenVolumeRevision){seenVolumeRevision=current.volumeRevision;refresh.activity(GetTickCount64());}
  if(tick++%60==0&&!hid.connected())hid.open();
  if(enabled&&!hardwareEnabled&&hid.connected()){
   hardwareEnabled=hid.send(0x49,1);
   if(hardwareEnabled)nextProbe=0;
  }
  if(!enabled&&hardwareEnabled){if(hid.connected())hid.send(0x49,0);hardwareEnabled=false;}
  if(hid.connected()&&(!enabled||(!current.wantMix&&current.wantPhysical<0))&&GetTickCount64()>=nextProbe){
   hid.probe(enabled);
   nextProbe=GetTickCount64()+3000;
  }
  if(current.wantMix&&enabled){
   hid.noteSoftwareMix(*current.wantMix);std::lock_guard<std::mutex> lock(mutexState);
   if(shared.mixRevision==current.mixRevision){shared.wantMix.reset();shared.mixSyncError=current.oledMix?L"":L"Schermata NovaMix sulla base disattivata nelle Preferenze.";}
  }
  if(current.wantPhysical>=0&&enabled){bool sent=hid.connected()&&hid.setPhysical(current.wantPhysical);{std::lock_guard<std::mutex> lock(mutexState);if(shared.volumeRevision==current.volumeRevision){shared.wantPhysical=-1;shared.volumeSyncError=sent?L"":L"Invio del volume alla base non riuscito. Esporta la diagnostica.";}}if(sent){hid.command(0x20);nextReadback=GetTickCount64()+150;refresh.activity(GetTickCount64());}else{std::lock_guard<std::mutex> lock(mutexState);if(shared.volumeRevision==current.volumeRevision)shared.volumeControl.reset();}}
  if(enabled&&hid.connected()&&current.volumeControl.awaiting&&GetTickCount64()<current.volumeControl.guardUntil&&GetTickCount64()>=nextReadback){hid.command(0x20);nextReadback=GetTickCount64()+150;}
  if(oledUntil&&(GetTickCount64()>=oledUntil||!current.oledHints||!enabled||current.oledMix)){hid.restoreScreen();oledUntil=0;}
  for(int n=0;n<16;n++){
   auto b=hid.poll();if(b.empty())break;
   bool showMicOnBase=false,cancelOled=false;int micValue=-1;
   {std::lock_guard<std::mutex> lock(mutexState);shared.packets++;
    if(auto volume=mix::physical(b)){
     size_t o=b[0]==0?1:0;bool push=b[o]==7&&b[o+1]==0x25;
     if(push)refresh.activity(GetTickCount64());
     if(shared.volumeControl.observe(*volume,GetTickCount64())){bool changed=shared.physical!=*volume;shared.physical=*volume;if(changed&&enabled){shared.osdKind=2;shared.osdSerial++;cancelOled=true;refresh.activity(GetTickCount64());}}
     else hid.recordVolumeIgnored(*volume,shared.volumeControl.targetRaw);
    }
    if(auto status=mix::status(b)){lastStatus=GetTickCount64();lastBattery=lastStatus;bool changed=status->mic>=0&&shared.mic!=status->mic;shared.mic=status->mic;shared.battery=status->battery;shared.chargerBattery=status->charger;shared.charging=status->charging;micFromEvent=false;if(changed&&enabled){shared.osdKind=3;shared.osdSerial++;showMicOnBase=shared.oledHints&&!shared.oledMix;micValue=status->mic;}}
    if(auto battery=mix::batteryStatus(b)){lastBattery=GetTickCount64();shared.battery=battery->battery;shared.chargerBattery=battery->charger;shared.charging=battery->charging;}
    if(auto muted=mix::micEvent(b)){bool changed=shared.mic!=*muted;shared.mic=*muted;micFromEvent=true;if(changed&&enabled){shared.osdKind=3;shared.osdSerial++;showMicOnBase=shared.oledHints&&!shared.oledMix;micValue=*muted;}}
   }
   if(cancelOled&&oledUntil){hid.restoreScreen();oledUntil=0;}
   if(showMicOnBase){if(hid.micOnScreen(micValue==1,current.english)){oledUntil=GetTickCount64()+1800;}else{hid.restoreScreen();std::lock_guard<std::mutex> lock(mutexState);shared.notice=L"Avviso OLED non disponibile. Il muto resta visibile sul PC.";}}
  }
  if(lastStatus&&GetTickCount64()-lastStatus>5000){std::lock_guard<std::mutex> lock(mutexState);if(!micFromEvent)shared.mic=-1;}
  if(lastBattery&&GetTickCount64()-lastBattery>7000){std::lock_guard<std::mutex> lock(mutexState);shared.battery=-1;shared.chargerBattery=-1;shared.charging=-1;}
  if(hid.connected()&&!hid.inputAlive()){hid.close();micFromEvent=false;lastStatus=0;hardwareEnabled=false;std::lock_guard<std::mutex> lock(mutexState);shared.mic=-1;shared.battery=-1;shared.chargerBattery=-1;shared.charging=-1;shared.physical=-1;shared.volumeControl.reset();oledActive=false;lastBattery=0;}
  auto screen=snapshot();auto now=GetTickCount64();int shift=(int)((now/60000)%2);
  auto displayed=mix::volumeDisplay(screen.physical,screen.volumeControl,now);
  std::array<int,10> frame{screen.levels.pc,screen.levels.chat,screen.mic,screen.battery,screen.chargerBattery,screen.charging,shift,displayed.percent,displayed.pending?1:0,screen.english?1:0};
  if(enabled&&screen.oledMix&&hid.connected()){
   if(refresh.due(now,frame!=lastFrame,oledActive)){
    bool sent=hid.mixOnScreen(screen,shift);refresh.sent(GetTickCount64(),sent);
    if(sent){oledActive=true;lastFrame=frame;}
    else{hid.restoreScreen();oledActive=false;}
    std::lock_guard<std::mutex> lock(mutexState);shared.mixSyncError=sent?L"":L"Invio della schermata OLED non riuscito. Esporta la diagnostica.";
   }
  }else if(oledActive){hid.restoreScreen();oledActive=false;}
  {std::lock_guard<std::mutex> lock(mutexState);shared.hid=hid.connected();shared.hidError=hardwareEnabled||!enabled?(hid.connected()?L"":hid.info):L"Attivazione ChatMix HID non riuscita. "+hid.info;shared.hidDetails=hid.diagnostics();}notify();
  if(current.diagnose){bool written=writeReport(fs::path(exeDir())/L"diagnostica.txt",snapshot());{std::lock_guard<std::mutex> lock(mutexState);shared.diagnose=false;shared.notice=written?L"Diagnostica salvata accanto a NovaMix.exe.":L"Impossibile salvare la diagnostica in questa cartella.";}notify();}
 }
 if(oledUntil||oledActive)hid.restoreScreen();if(hardwareEnabled&&hid.connected())hid.send(0x49,0);
 }
 CoUninitialize();
}

int wheelTest(){
 int failures=0;auto expect=[&](bool value){if(!value)++failures;};
 expect(mix::gentlePercent(0)==0&&mix::gentlePercent(100)==100);
 expect(mix::gentlePercent(8)==2&&mix::gentlePercent(92)==98);
 for(int i=0;i<=100;++i){expect(mix::gentlePercent(i)+mix::gentlePercent(100-i)==100);if(i)expect(mix::gentlePercent(i)>=mix::gentlePercent(i-1));}
 shared=State{};shared.enabled=true;
 applyMixInput({7,0x45,100,0});expect(shared.levels.chat==0);
 applyMixInput({7,0x45,100,8});expect(shared.levels.chat==2);
 auto revision=shared.audioCommandRevision;
 applyMixInput({7,0x45,100,8});expect(shared.audioCommandRevision==revision);
 std::vector<uint8_t> poll(64);poll[0]=6;poll[1]=0x20;poll[20]=100;poll[21]=8;
 applyMixInput(poll);expect(shared.audioCommandRevision==revision&&shared.levels.chat==2);
 poll[21]=0;applyMixInput(poll);expect(shared.levels.chat==2&&shared.baseLevels->chat==8);
 shared.lastMixEvent=0;applyMixInput(poll);expect(shared.levels.chat==0&&shared.baseLevels->chat==0);
 shared.levels={100,70};shared.fromHardware=false;shared.wantMix=shared.levels;
 applyMixInput({7,0x45,100,16});expect(shared.levels.chat==70&&shared.baseLevels->chat==16);
 shared.wantMix.reset();applyMixInput({7,0x45,100,16});expect(shared.levels.chat==70);
 applyMixInput({7,0x45,100,24});expect(shared.levels.chat==15);
 applyMixInput({7,0x45,100,16});expect(shared.levels.chat==7);
 shared.gentleMix=false;applyMixInput({7,0x45,100,8});expect(shared.levels.chat==8);
 std::ofstream(fs::path(exeDir())/L"test-wheel.txt")<<(failures?"FAIL ":"PASS ")<<failures<<" failures: endpoint preserving curve; 0->8 becomes 0->2; duplicate event/poll; software intent preserved; reversal; raw mode\n";
 return failures?1:0;
}
int testCore(){
 int failed=0;auto expect=[&](bool b){if(!b)++failed;};
 for(int b=-100;b<=100;b++){auto v=mix::balance(b);expect(v.pc>=0&&v.pc<=100&&v.chat>=0&&v.chat<=100);expect(mix::position(v)==b);}
 expect(mix::hardwareVolume(100)==0&&mix::hardwareVolume(0)==56&&mix::hardwareVolume(50)==28);
 std::vector<uint8_t> readback(64);readback[0]=6;readback[1]=0x20;readback[20]=91;readback[21]=100;readback[24]=100;expect(mix::baseMix(readback)&&mix::same(*mix::baseMix(readback),{91,100}));readback[22]=100;readback[24]=0;expect(mix::same(*mix::baseMix(readback),{91,100}));readback[20]=255;expect(!mix::baseMix(readback));
 mix::Baseline v;expect(std::abs(v.apply(.7f,50,100)-.35f)<.0001f);
 for(int i=0;i<100;i++)expect(std::abs(v.apply(.35f,50,100)-.35f)<.0001f);
 expect(std::abs(v.apply(.35f,100,100)-.7f)<.0001f);expect(std::abs(*v.restore(.7f)-.7f)<.0001f);
 v.apply(.7f,50,100);expect(!v.restore(.2f));expect(std::abs(v.apply(.2f,50,100)-.1f)<.0001f);expect(std::abs(*v.restore(.1f)-.2f)<.0001f);
 expect(mix::packet({7,0x45,80,100})->pc==80);expect(mix::packet({0,7,0x45,100,30})->chat==30);
 expect(!mix::packet({7,0x45,255,100}));expect(!mix::packet({6,0x45,80,100}));expect(!mix::packet({7,0x45,80}));expect(!mix::packet({}));
 expect(mix::physical({7,0x25,0})==100);expect(mix::physical({7,0x25,56})==0);expect(mix::physical({0,6,0x20,0,28})==50);expect(!mix::physical({7,0x25,57}));
 expect(mix::micEvent({7,0xbb,0})==0);expect(mix::micEvent({7,0xbb,1})==1);expect(mix::micEvent({0,7,0xbb,1})==1);expect(!mix::micEvent({7,0xbb,2}));expect(!mix::micEvent({6,0xbb,1}));expect(!mix::micEvent({7,0xbb}));expect(!mix::micEvent({7,0x25,1}));
 std::vector<uint8_t> status(64);status[0]=6;status[1]=0xb0;status[6]=8;status[9]=1;status[15]=8;expect(mix::status(status)->mic==1&&mix::status(status)->battery==100);status[9]=0;expect(mix::status(status)->mic==0);status[15]=1;expect(mix::status(status)->mic==-1);status.resize(15);expect(!mix::status(status));
 std::vector<bool> pixels(128*64);pixels[0]=true;pixels[63*128+63]=true;pixels[64]=true;pixels[63*128+127]=true;auto left=mix::oledHalf(pixels,0,1024),right=mix::oledHalf(pixels,1,1024);expect(left.size()==1024&&right.size()==1024&&right[2]==64&&left[6]==1&&left[517]==128&&right[6]==1&&right[517]==128);expect(mix::oledHalf(pixels,0,517).empty());
 auto mixImage=mix::oledMixPixels({0,100},1,62,87,2,0,43),activeImage=mix::oledMixPixels({0,100},0,62,87,2,0,43);
 expect(mixImage.size()==8192&&mixImage!=activeImage);expect(mixImage[22*128+121]&&!mixImage[22*128+5]);
 auto pcImage=mix::oledMixPixels({100,0},1,62,87,2,0,100);expect(pcImage[22*128+5]&&!pcImage[22*128+121]);expect(pcImage[44*128+100]&&!mixImage[44*128+100]);
 expect(mix::oledMixPixels({100,100},1)[22*128+63]);
 {auto image=mix::oledMixPixels({43,100},1,62,87,2,0,57,true);std::ofstream bitmap(fs::path(exeDir())/L"test-oled.pbm");bitmap<<"P1\n128 64\n";for(size_t i=0;i<image.size();i++)bitmap<<(image[i]?0:1)<<((i%128==127)?'\n':' ');}
 // Slow pulses, stale USB replies between pulses, acknowledgements and settling.
 mix::VolumeControl control;int actual=60;auto t1=control.step(actual,-1,1000);expect(t1<actual);expect(!control.observe(actual,1050));
 auto t2=control.step(actual,-1,1300);expect(t2<t1);expect(!control.observe(t1,1350));expect(control.observe(t2,1400));expect(!control.observe(actual,1450));
 auto t3=control.step(t2,-1,1500);expect(t3<t2);expect(control.observe(t3,1550));expect(!control.observe(t2,2300));expect(control.observe(80,3100));expect(control.baseline(80,3100)==80);
 control.reset();actual=100;for(int i=0;i<56;i++){int target=control.step(actual,-1,3000+i*500);expect(target<actual);expect(!control.observe(actual,3010+i*500));expect(control.observe(target,3050+i*500));actual=target;}expect(actual==0);expect(control.step(0,-1,32000)==0);
 control.request(50,40000);expect(!control.observe(75,41000));expect(control.observe(75,41600));expect(control.baseline(75,41600)==75);
 for(int raw=0;raw<=56;raw++)expect(mix::hardwareVolume(mix::hardwarePercent(raw))==raw);
 status.resize(64);status[15]=2;status[6]=5;status[7]=7;expect(mix::status(status)->battery==62&&mix::status(status)->charger==87&&mix::status(status)->mic==0&&mix::status(status)->charging==2);
 status[15]=4;expect(mix::status(status)->mic==0);status[15]=1;expect(mix::status(status)->battery==-1&&mix::status(status)->charger==87);
 auto battery=mix::batteryStatus({7,0xb7,8,4,2});expect(battery&&battery->battery==100&&battery->charger==50&&battery->charging==2);
 expect(mix::batteryStatus({0,6,0xb7,5,7,8})->battery==62);expect(!mix::batteryStatus({7,0xb7,5}));expect(!mix::batteryStatus({7,0x20,5,7,8}));expect(mix::batteryStatus({7,0xb7,255,255,255})->charger==-1);
 expect(mix::batteryStatus({7,0xb7,8,4,1})->battery==-1);expect(mix::batteryPercent(0)==0&&mix::batteryPercent(8)==100&&mix::batteryPercent(9)==-1);
 {
  trayStatus::BatterySettings settings;
  settings.showPercent = true;
  settings.background = false;
  settings.fontSize = 8;
  expect(settings.style(25) == (uint32_t(settings.low) | trayStatus::percentageStyle));
  expect(settings.style(26) == (uint32_t(settings.medium) | trayStatus::percentageStyle));
  expect(settings.style(51) == (uint32_t(settings.normal) | trayStatus::percentageStyle));
  settings.lowLimit = 18;
  settings.mediumLimit = 65;
  expect(settings.style(18) != settings.style(19) && settings.style(19) == settings.style(65) &&
         settings.style(65) != settings.style(66));
  settings.background = true;
  settings.fontSize = 12;
  expect((settings.style(25) & trayStatus::backgroundStyle) != 0);
  expect(int(settings.style(25) >> trayStatus::fontSizeShift) + 8 == 12);
  for (int size : {16, 20, 24, 32}) {
   for (int value : {-1, 0, 8, 25, 99, 100}) {
    HICON icon = trayStatus::createIcon(false, value, size, settings.style(value));
    ICONINFO info{};
    const bool valid = icon && GetIconInfo(icon, &info);
    BITMAP image{};
    expect(valid && GetObjectW(info.hbmColor, sizeof(image), &image) &&
           image.bmWidth == size && image.bmHeight == size && image.bmBitsPixel == 32);
    if (valid) { DeleteObject(info.hbmColor); DeleteObject(info.hbmMask); }
    if (icon) DestroyIcon(icon);
   }
   HICON mic = trayStatus::createIcon(true, 1, size);
   expect(mic != nullptr);
   if (mic) DestroyIcon(mic);
  }
  trayStatus::BatteryAlert alert;
  expect(!alert.due(37, 20, 10) && alert.due(12, 20, 11));
  alert.sent(true, 11);
  expect(!alert.due(12, 20, 12) && !alert.due(-1, 20, 13) && !alert.due(12, 20, 14));
  expect(!alert.due(21, 20, 15) && !alert.due(12, 20, 16));
  expect(!alert.due(25, 20, 17) && alert.due(20, 20, 18));
  alert.sent(false, 18);
  expect(!alert.due(12, 20, 5017) && alert.due(12, 20, 5018));
  expect(!alert.due(12, 0, 5019));
 }
 expect(mix::oledMixPixels({43,100},1,62,87,2)!=mix::oledMixPixels({43,100},1,62,87,8));expect(mix::oledMixPixels({43,100},1,62,87,2)!=mix::oledMixPixels({43,100},1,62,87,2,1));
 mix::VolumeControl displayControl;displayControl.request(57,1000);auto requested=mix::volumeDisplay(80,displayControl,1100);expect(requested.pending&&requested.percent==57);
 displayControl.observe(57,1200);auto confirmed=mix::volumeDisplay(57,displayControl,1200);expect(!confirmed.pending&&confirmed.percent==57);
 displayControl.request(40,2000);expect(!mix::volumeDisplay(57,displayControl,3600).pending&&mix::volumeDisplay(57,displayControl,3600).percent==57);
 mix::OledRefresh timing;expect(timing.due(1000,true,false));timing.sent(1000,true);expect(!timing.due(1050,false,true));expect(timing.due(2000,false,true));
 timing.activity(1100);expect(timing.due(1100,false,true));timing.sent(1100,true);expect(timing.due(1175,false,true));expect(!timing.due(1150,true,true));
 int redraws=0;for(uint64_t now=1175;now<3400;now+=25){if(timing.due(now,false,true)){timing.sent(now,true);redraws++;}}expect(redraws>=25);
 timing.sent(4000,false);timing.activity(4100);expect(!timing.due(4100,true,false));expect(timing.due(7000,true,false));
 expect(mix::oledMixPixels({43,100},1,62,87,2,0,57,true)!=mix::oledMixPixels({43,100},1,62,87,2,0,57,false));
 std::ofstream(fs::path(exeDir())/L"test-risultato.txt")<<(failed?"FAIL ":"PASS ")<<failed<<" failed; balance, restore, HID framing, volume, microphone offline/mute, OLED tile packing\n";
 return failed?1:0;
}
int replayMute(const fs::path& path){
 std::ifstream input(path);std::string line;int active=0,muted=0,last=-1;
 while(std::getline(input,line)){auto at=line.find(" :");if(line.find("RX page=")==std::string::npos||at==std::string::npos)continue;std::istringstream hex(line.substr(at+2));unsigned byte;std::vector<uint8_t> bytes;while(hex>>std::hex>>byte){if(byte>255)break;bytes.push_back((uint8_t)byte);}if(auto value=mix::micEvent(bytes)){last=*value;if(*value)muted++;else active++;}}
 bool pass=active>0&&muted>0;std::ofstream(fs::path(exeDir())/L"test-replay.txt")<<(pass?"PASS":"FAIL")<<": captured HID mute events decoded; active="<<active<<" muted="<<muted<<" last="<<last<<"\n";return pass?0:1;
}
int replayChatmix(const fs::path& path){
 std::ifstream input(path);std::string line;int sent=0,readbacks=0;std::set<std::pair<int,int>> base,stream;
 while(std::getline(input,line)){if(line.find("TX ChatMix 06 47")!=std::string::npos)++sent;auto at=line.find(" :");if(line.find("RX page=")==std::string::npos||at==std::string::npos)continue;std::istringstream hex(line.substr(at+2));unsigned byte;std::vector<uint8_t> bytes;while(hex>>std::hex>>byte){if(byte>255)break;bytes.push_back((uint8_t)byte);}if(auto v=mix::baseMix(bytes)){++readbacks;base.insert({v->pc,v->chat});if(bytes.size()>24)stream.insert({bytes[22],bytes[24]});}}
 bool pass=sent>0&&readbacks>1&&base.size()==1&&stream.size()>1;std::ofstream(fs::path(exeDir())/L"test-chatmix-capture.txt")<<(pass?"PASS":"FAIL")<<": captured 0x47 writes="<<sent<<", audio readbacks="<<readbacks<<", distinct ChatMix pairs="<<base.size()<<", distinct Stream Mix pairs="<<stream.size()<<"\n";return pass?0:1;
}

void text(HDC dc,const std::wstring& value,RECT r,HFONT font,COLORREF color,UINT flags=DT_LEFT|DT_TOP|DT_WORDBREAK){SelectObject(dc,font);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);DrawTextW(dc,value.c_str(),-1,&r,flags);}
void box(HDC dc,int x,int y,int w,int h){auto old=SelectObject(dc,cardBrush);auto pen=CreatePen(PS_SOLID,1,RGB(43,56,62));auto op=SelectObject(dc,pen);RoundRect(dc,x,y,x+w,y+h,18,18);SelectObject(dc,op);SelectObject(dc,old);DeleteObject(pen);}
void rounded(HDC dc,RECT r,COLORREF color,int radius=14){auto brush=CreateSolidBrush(color);auto old=SelectObject(dc,brush);auto pen=SelectObject(dc,GetStockObject(NULL_PEN));RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius);SelectObject(dc,pen);SelectObject(dc,old);DeleteObject(brush);}
LRESULT CALLBACK osdProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
 if(msg==WM_NCHITTEST)return HTTRANSPARENT;
 if(msg==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
 if(msg==WM_TIMER){KillTimer(hwnd,1);ShowWindow(hwnd,SW_HIDE);return 0;}
 if(msg==WM_ERASEBKGND)return 1;
 if(msg==WM_PAINT){PAINTSTRUCT ps;HDC out=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);HDC dc=CreateCompatibleDC(out);HBITMAP bmp=CreateCompatibleBitmap(out,r.right,r.bottom);auto old=SelectObject(dc,bmp);FillRect(dc,&r,cardBrush);auto s=snapshot();COLORREF fg=RGB(228,241,237),accent=RGB(184,174,242);
  text(dc,s.osdKind==1?L"ChatMix":s.osdKind==2?(s.english?L"Headset volume":L"Volume cuffie"):s.osdKind==3?(s.english?L"Microphone":L"Microfono"):(s.english?L"Mix volume":L"Volume app"),{18,14,332,38},fontNormal,fg);
  if(s.osdKind==1){text(dc,L"PC "+std::to_wstring(s.levels.pc)+L"%",{18,47,180,80},fontNumber,fg);text(dc,L"Chat "+std::to_wstring(s.levels.chat)+L"%",{180,47,334,80},fontNumber,accent,DT_RIGHT);rounded(dc,{18,94,164,100},RGB(57,68,73),6);rounded(dc,{188,94,334,100},RGB(57,68,73),6);if(s.levels.pc)rounded(dc,{18,94,18+146*s.levels.pc/100,100},fg,6);if(s.levels.chat)rounded(dc,{188,94,188+146*s.levels.chat/100,100},accent,6);}
  else if(s.osdKind==3){text(dc,s.mic<0?(s.english?L"Unavailable":L"Non disponibile"):s.mic?(s.english?L"MIC MUTED":L"MIC MUTO"):(s.english?L"MIC ON":L"MIC ATTIVO"),{18,48,334,93},fontNumber,s.mic==1?RGB(243,161,163):accent);}
  else {int value=s.osdKind==2?std::max(0,s.physical):s.master;text(dc,std::to_wstring(value)+L"%",{18,44,334,83},fontNumber,accent);rounded(dc,{18,94,334,100},RGB(57,68,73),6);if(value)rounded(dc,{18,94,18+316*value/100,100},accent,6);}
  BitBlt(out,0,0,r.right,r.bottom,dc,0,0,SRCCOPY);SelectObject(dc,old);DeleteObject(bmp);DeleteDC(dc);EndPaint(hwnd,&ps);return 0;}
 return DefWindowProcW(hwnd,msg,wp,lp);
}
void showOsd(){
 if(!osdH)return;HWND taskbar=FindWindowW(L"Shell_TrayWnd",nullptr);MONITORINFO monitor{sizeof(monitor)};
 if(!GetMonitorInfoW(MonitorFromWindow(taskbar?taskbar:windowH,MONITOR_DEFAULTTOPRIMARY),&monitor)){SystemParametersInfoW(SPI_GETWORKAREA,0,&monitor.rcWork,0);}
 if(monitor.rcWork.right<=monitor.rcWork.left||monitor.rcWork.bottom<=monitor.rcWork.top)monitor.rcWork={0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)};
 BOOL shown=SetWindowPos(osdH,HWND_TOPMOST,monitor.rcWork.right-372,monitor.rcWork.bottom-138,352,118,SWP_NOACTIVATE|SWP_SHOWWINDOW);InvalidateRect(osdH,nullptr,FALSE);SetTimer(osdH,1,preview?8000:1900,nullptr);
 if(preview){RECT actual;GetWindowRect(osdH,&actual);std::ofstream(fs::path(exeDir())/L"preview-osd.txt")<<"shown="<<shown<<" visible="<<IsWindowVisible(osdH)<<" x="<<actual.left<<" y="<<actual.top<<" w="<<actual.right-actual.left<<" h="<<actual.bottom-actual.top<<"\n";}
}
void tray(HWND hwnd, bool add, bool english = false, bool force = false) {
 static bool registered = false, lastEnglish = false;
 static int lastSize = 0;
 NOTIFYICONDATAW n{};
 n.cbSize = sizeof(n);
 n.hWnd = hwnd;
 n.uID = 1;
 if (!add) { Shell_NotifyIconW(NIM_DELETE, &n); return; }
 const int size = trayStatus::iconSize();
 if (!force && registered && lastEnglish == english && lastSize == size) return;
 n.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
 n.uCallbackMessage = TRAY;
 n.hIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, size, size, 0));
 if (!n.hIcon) return;
 wcscpy_s(n.szTip, english ? L"NovaMix — double-click to open" : L"NovaMix — doppio clic per aprire");
 registered = (registered && Shell_NotifyIconW(NIM_MODIFY, &n)) || Shell_NotifyIconW(NIM_ADD, &n);
 lastSize = size;
 lastEnglish = english;
 DestroyIcon(n.hIcon);
}
void openDesktopUi(const std::wstring& address);
#include "webbridge.hpp"
#include "desktop.hpp"
HHOOK volumeHook=nullptr;
LRESULT CALLBACK volumeKeyboardProc(int code,WPARAM wp,LPARAM lp){
 if(code==HC_ACTION&&hotkeys){auto key=reinterpret_cast<KBDLLHOOKSTRUCT*>(lp);int id=key->vkCode==VK_VOLUME_UP?1:key->vkCode==VK_VOLUME_DOWN?2:key->vkCode==VK_VOLUME_MUTE?3:0;
  if(id){if(wp==WM_KEYDOWN||wp==WM_SYSKEYDOWN)PostMessageW(windowH,WM_HOTKEY,id,0);return 1;}}
 return CallNextHookEx(volumeHook,code,wp,lp);
}
LRESULT CALLBACK coreProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
 static UINT taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
 if(msg==taskbarCreated){
  if(!preview&&!smoke){
   auto state=snapshot();
   tray(hwnd,true,state.english,true);
   trayStatus::update(hwnd,false,state.mic,state.battery,state.trayBattery,state.english,true);
  }
  return 0;
 }
 switch(msg){
 case GET_VERSION:return RELEASE;
 case WM_CREATE:{
  if(!preview&&!smoke){
   auto state=snapshot();
   tray(hwnd,true,state.english,true);
   trayStatus::update(hwnd,false,state.mic,state.battery,state.trayBattery,state.english,true);
  }
  SetTimer(hwnd,6,1000,nullptr);
  if(smoke){for(int i=0;i<100;i++)PostMessageW(hwnd,UPDATE,0,0);SetTimer(hwnd,42,1200,nullptr);}
  return 0;
 }
 case WM_DPICHANGED:{
  if(!preview&&!smoke){
   auto state=snapshot();
   trayStatus::update(hwnd,false,state.mic,state.battery,state.trayBattery,state.english,true);
  }
  return 0;
 }
 case OPEN_UI:if(openWebUi)openWebUi();return 0;
 case EXIT_APP:DestroyWindow(hwnd);return 0;
 case UPDATE:{auto s=snapshot();static unsigned long long lastOsd=0;static bool attempted=false;
  if(!preview&&!smoke)trayStatus::update(hwnd,false,s.mic,s.battery,s.trayBattery,s.english);
  if(s.osdSerial!=lastOsd){lastOsd=s.osdSerial;if(s.overlay)showOsd();}else ++unchangedUpdatesSuppressed;
  bool want=s.keyboardWanted&&s.enabled&&s.hid&&s.physical>=0;
  if(!want){if(volumeHook){UnhookWindowsHookEx(volumeHook);volumeHook=nullptr;}hotkeys=false;attempted=false;}
  else if(!attempted){attempted=true;if(preview)hotkeys=true;else{volumeHook=SetWindowsHookExW(WH_KEYBOARD_LL,volumeKeyboardProc,GetModuleHandleW(nullptr),0);hotkeys=volumeHook!=nullptr;}}
  {std::lock_guard<std::mutex> lock(mutexState);shared.keyboardActive=hotkeys;}return 0;}
 case WM_HOTKEY:{static int beforeMute=50;{std::lock_guard<std::mutex> lock(mutexState);if(!shared.enabled||shared.physical<0)return 0;auto now=GetTickCount64();int target=shared.volumeControl.baseline(shared.physical,now);if(wp==1)target=shared.volumeControl.step(shared.physical,1,now);else if(wp==2)target=shared.volumeControl.step(shared.physical,-1,now);else if(wp==3){if(target){beforeMute=target;target=0;}else target=beforeMute;shared.volumeControl.request(target,now);}else return 0;shared.wantPhysical=target;++shared.volumeRevision;}PostMessageW(hwnd,UPDATE,0,0);return 0;}
 case trayStatus::callback:
  if (LOWORD(lp)==NIN_SELECT||LOWORD(lp)==NIN_KEYSELECT||LOWORD(lp)==NIN_BALLOONUSERCLICK)
   PostMessageW(hwnd,OPEN_UI,0,0);
  return 0;
 case TRAY:
  if (lp == WM_LBUTTONDBLCLK) PostMessageW(hwnd, OPEN_UI, 0, 0);
  else if (lp == WM_RBUTTONUP) {
   const bool english = snapshot().english;
   auto guide = fs::path(exeDir()) / L"Configura audio NovaMix.exe";
   std::error_code error;
   HMENU menu = CreatePopupMenu();
   AppendMenuW(menu, MF_STRING, 1, english ? L"Open NovaMix" : L"Apri NovaMix");
   if (fs::is_regular_file(guide, error)) AppendMenuW(menu, MF_STRING, 3, english ? L"Configure audio" : L"Configura audio");
   AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
   AppendMenuW(menu, MF_STRING, 2, english ? L"Quit" : L"Esci");
   POINT cursor{};
   GetCursorPos(&cursor);
   SetForegroundWindow(hwnd);
   UINT choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, hwnd, nullptr);
   DestroyMenu(menu);
   PostMessageW(hwnd, WM_NULL, 0, 0);
   if (choice == 1) PostMessageW(hwnd, OPEN_UI, 0, 0);
   if (choice == 2) DestroyWindow(hwnd);
   if (choice == 3 && reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd, L"open", guide.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
    MessageBoxW(hwnd, english ? L"Could not open the NovaMix audio setup." : L"Impossibile aprire Configura audio NovaMix.", L"NovaMix", MB_OK | MB_ICONWARNING);
  }
  return 0;
 case WM_TIMER:if(wp==42){if(serviceTest){SendMessageW(hwnd,WM_CLOSE,0,0);bool alive=IsWindow(hwnd)&&WaitForSingleObject(stopEvent,0)==WAIT_TIMEOUT;int before=snapshot().master;{std::lock_guard<std::mutex> lock(mutexState);shared.physical=60;}SendMessageW(hwnd,WM_HOTKEY,2,0);{std::lock_guard<std::mutex> lock(mutexState);shared.wantPhysical=-1;shared.volumeControl.observe(60,GetTickCount64());}SendMessageW(hwnd,WM_HOTKEY,2,0);bool volume=snapshot().wantPhysical==mix::hardwarePercent(mix::hardwareVolume(60)+2)&&snapshot().master==before&&SendMessageW(hwnd,GET_VERSION,0,0)==RELEASE;std::ofstream(fs::path(exeDir())/L"test-service.txt")<<(alive&&volume?"PASS":"FAIL")<<": closing window preserves engine; keyboard commands queue physical volume, coalesce repeated keys, preserve software master; version handshake correct\n";if(!alive||!volume)unchangedUpdatesSuppressed=0;}DestroyWindow(hwnd);return 0;}if(wp==6){desktop.poll();if(!preview&&!smoke){auto s=snapshot();tray(hwnd,true,s.english);trayStatus::update(hwnd,false,s.mic,s.battery,s.trayBattery,s.english,false,true);}if(preferencesDirty.exchange(false))savePreferences();}return 0;
 case WM_QUERYENDSESSION:return TRUE;
 case WM_ENDSESSION:if(wp)DestroyWindow(hwnd);return 0;
 case WM_CLOSE:ShowWindow(hwnd,SW_HIDE);return 0;
 case WM_DESTROY:savePreferences();if(!preview&&!smoke){trayStatus::remove(hwnd,false);tray(hwnd,false);}if(volumeHook){UnhookWindowsHookEx(volumeHook);volumeHook=nullptr;}hotkeys=false;SetEvent(stopEvent);PostQuitMessage(0);return 0;
 }return DefWindowProcW(hwnd,msg,wp,lp);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
 int argc;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);std::vector<std::wstring> args(argv,argv+argc);LocalFree(argv);
 if(std::find(args.begin(),args.end(),L"--desktop-self-test")!=args.end())return DesktopUi::selfTest();
 if(std::find(args.begin(),args.end(),L"--stream-self-test")!=args.end())return streamSelfTest();
 if(std::find(args.begin(),args.end(),L"--wheel-self-test")!=args.end())return wheelTest();
 if(std::find(args.begin(),args.end(),L"--self-test")!=args.end())return testCore();
 if(std::find(args.begin(),args.end(),L"--hid-reader-self-test")!=args.end())return Hid::readerTest();
 auto replay=std::find(args.begin(),args.end(),L"--replay-hid");if(replay!=args.end()&&std::next(replay)!=args.end())return replayMute(*std::next(replay));
 auto mixReplay=std::find(args.begin(),args.end(),L"--replay-chatmix");if(mixReplay!=args.end()&&std::next(mixReplay)!=args.end())return replayChatmix(*std::next(mixReplay));
 if(std::find(args.begin(),args.end(),L"--api-self-test")!=args.end()){WebUi tests;return tests.selfTest();}
 auto diag=std::find(args.begin(),args.end(),L"--diagnose");
 if(diag!=args.end()){
  HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);State s;
  if (SUCCEEDED(hr)) {
   try {
    ComPtr<IMMDeviceEnumerator> enumerator;
    check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(&enumerator)), L"Dispositivi audio Windows");
    std::map<std::wstring, int> numbers;
    streams::enumerate(*enumerator.Get(), numbers);
    for (const auto& device : streamSetup.devices) s.audioDetails += device.name + L"\n";
    s.audio = !streamSetup.devices.empty();
   } catch (const Failure& error) { s.audioError = error.message; }
   CoUninitialize();
  } else s.audioError = errorText(hr);
  auto file=std::next(diag)!=args.end()?fs::path(*std::next(diag)):fs::path(exeDir())/L"diagnostica.txt";writeReport(file,s);return s.audio&&s.hid?0:2;
 }
 serviceTest=std::find(args.begin(),args.end(),L"--service-self-test")!=args.end();
 smoke=serviceTest||std::find(args.begin(),args.end(),L"--smoke-test")!=args.end();
 preview=serviceTest||std::find(args.begin(),args.end(),L"--preview")!=args.end();
 if (preview) {
  shared.enabled = shared.wantEnabled = shared.audio = shared.hid = true;
  shared.battery = 75;
  shared.chargerBattery = 50;
  shared.mic = 0;
  shared.physical = 75;
  shared.levels = {75, 100};
  shared.master = 64;
  streamSetup.devices = {{1,L"pc",L"PC Virtual Output"},{2,L"chat",L"Chat Virtual Output"},{3,L"nova",L"Arctis Nova Pro Wireless"}};
  streamSetup.selected = {L"pc",L"chat",L"nova"};
  streamSetup.signal = {38, 62};
 } else if (!smoke) loadPreferences();
 std::wstring mutexName=(smoke||preview)?L"Local\\NovaMix.Cpp.Preview."+std::to_wstring(GetCurrentProcessId()):L"Local\\NovaMix.Cpp.Active";
 HANDLE singleton=CreateMutexW(nullptr,FALSE,mutexName.c_str());if(GetLastError()==ERROR_ALREADY_EXISTS){auto old=FindWindowW(L"NovaMixService",nullptr);DWORD_PTR running=0;bool same=old&&SendMessageTimeoutW(old,GET_VERSION,0,0,SMTO_ABORTIFHUNG,1000,&running)&&running==RELEASE;if(same)PostMessageW(old,OPEN_UI,0,0);else MessageBoxW(nullptr,L"E gia in esecuzione una versione diversa di NovaMix.\n\nChiudila dal menu vicino all\x0027orologio > Esci. Poi riapri questo eseguibile.\nLa nuova versione non e stata avviata.",L"NovaMix - aggiornamento",MB_OK|MB_ICONINFORMATION);CloseHandle(singleton);return 0;}
 HRESULT apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(apartment)){CloseHandle(singleton);return 1;} SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED);
 INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_BAR_CLASSES};InitCommonControlsEx(&cc);
 background=CreateSolidBrush(RGB(18,25,29));cardBrush=CreateSolidBrush(RGB(25,29,37));
 auto font=[](int size,int weight){return CreateFontW(-size,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");};
 fontNormal=font(16,FW_SEMIBOLD);fontSmall=font(13,FW_NORMAL);fontTitle=font(31,FW_SEMIBOLD);fontNumber=font(30,FW_NORMAL);
 WNDCLASSEXW cls{sizeof(cls)};cls.lpfnWndProc=coreProc;cls.hInstance=instance;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));cls.hIconSm=cls.hIcon;cls.hbrBackground=background;cls.lpszClassName=L"NovaMixService";RegisterClassExW(&cls);
 RECT r{0,0,480,700};AdjustWindowRectEx(&r,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE,0);
 stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);audioWake=CreateEventW(nullptr,FALSE,TRUE,nullptr);
 windowH=CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"NovaMix background",WS_OVERLAPPED,CW_USEDEFAULT,CW_USEDEFAULT,1,1,nullptr,nullptr,instance,nullptr);
 if(!windowH){CloseHandle(audioWake);CloseHandle(stopEvent);CloseHandle(singleton);return 1;}
 BOOL dark=TRUE;DwmSetWindowAttribute(windowH,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));DWM_WINDOW_CORNER_PREFERENCE corner=DWMWCP_ROUND;DwmSetWindowAttribute(windowH,DWMWA_WINDOW_CORNER_PREFERENCE,&corner,sizeof(corner));
 WNDCLASSEXW osdClass{sizeof(osdClass)};osdClass.lpfnWndProc=osdProc;osdClass.hInstance=instance;osdClass.lpszClassName=L"NovaMixOsd";osdClass.hCursor=cls.hCursor;RegisterClassExW(&osdClass);osdH=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_TRANSPARENT,osdClass.lpszClassName,L"NovaMix OSD",WS_POPUP,0,0,352,118,nullptr,nullptr,instance,nullptr);if(osdH){SetLayeredWindowAttributes(osdH,0,245,LWA_ALPHA);SetWindowRgn(osdH,CreateRoundRectRgn(0,0,352,118,18,18),FALSE);}
 (void)show;ShowWindow(windowH,SW_HIDE);
 WebUi web;if(!web.start()){MessageBoxW(nullptr,L"Impossibile avviare l'interfaccia locale.",L"NovaMix",MB_OK);DestroyWindow(windowH);CloseHandle(audioWake);CloseHandle(stopEvent);CloseHandle(singleton);return 1;}
 openWebUi=[&web]{web.open();};if(preview)std::ofstream(fs::path(exeDir())/L"preview-url.txt")<<web.url();
 bool backgroundOnly=std::find(args.begin(),args.end(),L"--background")!=args.end();if(!backgroundOnly&&!smoke)PostMessageW(windowH,OPEN_UI,0,0);
 std::thread thread,audioThread;if(!preview){thread=std::thread(worker);audioThread=std::thread(audioWorker);}else PostMessageW(windowH,UPDATE,0,0);MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(windowH,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
 if(audioThread.joinable())audioThread.join();if(thread.joinable())thread.join();desktop.close();web.stop();openWebUi={};if(osdH)DestroyWindow(osdH);CloseHandle(audioWake);CloseHandle(stopEvent);CloseHandle(singleton);DeleteObject(background);DeleteObject(cardBrush);DeleteObject(fontNormal);DeleteObject(fontSmall);DeleteObject(fontTitle);DeleteObject(fontNumber);CoUninitialize();
 if(smoke){bool pass=unchangedUpdatesSuppressed>=99;std::ofstream(fs::path(exeDir())/L"smoke-risultato.txt")<<(pass?"PASS":"FAIL")<<": native window created, message loop and worker exited; unchanged UI updates suppressed: "<<unchangedUpdatesSuppressed<<"\n";return pass?0:1;}
 return 0;
}
