#pragma once
#include <charconv>

std::string utf8(const std::wstring& value){int n=WideCharToMultiByte(CP_UTF8,0,value.data(),(int)value.size(),nullptr,0,nullptr,nullptr);std::string s(n,'\0');WideCharToMultiByte(CP_UTF8,0,value.data(),(int)value.size(),s.data(),n,nullptr,nullptr);return s;}
std::wstring wide(const std::string& value){int n=MultiByteToWideChar(CP_UTF8,0,value.data(),(int)value.size(),nullptr,0);std::wstring s(n,L'\0');MultiByteToWideChar(CP_UTF8,0,value.data(),(int)value.size(),s.data(),n);return s;}
std::string jsonText(const std::wstring& value){std::string out="\"";for(unsigned char c:utf8(value)){switch(c){case '"':out+="\\\"";break;case '\\':out+="\\\\";break;case '\n':out+="\\n";break;case '\r':out+="\\r";break;case '\t':out+="\\t";break;default:if(c<32){char buf[7];sprintf_s(buf,"\\u%04x",c);out+=buf;}else out+=(char)c;}}return out+'"';}
void savePreferences(const fs::path& filename=fs::path(exeDir())/L"streams.ini"){
 if (preview) return;
 State s;
 std::array<std::wstring, 3> routes;
 {
  std::lock_guard<std::mutex> lock(mutexState);
  s = shared;
  routes = streamSetup.selected;
 }
 std::wstring data;
 auto add=[&](const wchar_t* name,int value){data+=name;data+=L"="+std::to_wstring(value);data.push_back(0);};
 add(L"accent",s.accent);add(L"gentleMix",s.gentleMix);add(L"overlay",s.overlay);add(L"keyboard",s.keyboardWanted);add(L"oled",s.oledHints);add(L"oledMix",s.oledMix);add(L"master",s.master);add(L"balance",mix::position(s.levels));
 add(L"language",s.english);
 add(L"batteryPercent",s.trayBattery.showPercent);
 add(L"batteryBackground",s.trayBattery.background);
 add(L"batteryFontSize",s.trayBattery.fontSize);
 add(L"batteryNotify",s.trayBattery.notify);
 add(L"batteryThreshold",s.trayBattery.threshold);
 add(L"batteryLowLimit",s.trayBattery.lowLimit);
 add(L"batteryMediumLimit",s.trayBattery.mediumLimit);
 add(L"batteryLow",s.trayBattery.low);
 add(L"batteryMedium",s.trayBattery.medium);
 add(L"batteryNormal",s.trayBattery.normal);
 constexpr const wchar_t* keys[]{L"routePc", L"routeChat", L"routeOut"};
 for (size_t i = 0; i < routes.size(); ++i) {
  data += std::wstring(keys[i]) + L"=" + routes[i];
  data.push_back(0);
 }
 data.push_back(0);
 if(!WritePrivateProfileSectionW(L"NovaMix",data.c_str(),filename.c_str())){std::lock_guard<std::mutex> lock(mutexState);shared.notice=L"Impossibile salvare le preferenze in questa cartella.";}
}
void loadPreferences(const fs::path& filename=fs::path(exeDir())/L"streams.ini"){
 auto file=filename.wstring();
 shared.english=GetPrivateProfileIntW(L"NovaMix",L"language",shared.english,file.c_str())!=0;
 shared.accent=std::clamp((int)GetPrivateProfileIntW(L"NovaMix",L"accent",0,file.c_str()),0,2);
 shared.gentleMix=GetPrivateProfileIntW(L"NovaMix",L"gentleMix",1,file.c_str())!=0;
 shared.overlay=GetPrivateProfileIntW(L"NovaMix",L"overlay",1,file.c_str())!=0;
 shared.keyboardWanted=GetPrivateProfileIntW(L"NovaMix",L"keyboard",0,file.c_str())!=0;
 shared.oledMix=GetPrivateProfileIntW(L"NovaMix",L"oledMix",1,file.c_str())!=0;
 shared.oledHints=GetPrivateProfileIntW(L"NovaMix",L"oled",0,file.c_str())!=0;
 shared.master=std::clamp((int)GetPrivateProfileIntW(L"NovaMix",L"master",100,file.c_str()),0,100);
 shared.levels=mix::balance(std::clamp((int)GetPrivateProfileIntW(L"NovaMix",L"balance",0,file.c_str()),-100,100));
 const trayStatus::BatterySettings defaults;
 auto batteryPref=[&](const wchar_t* key,int fallback){
  return int(GetPrivateProfileIntW(L"NovaMix",key,fallback,file.c_str()));
 };
 shared.trayBattery.showPercent=batteryPref(L"batteryPercent",defaults.showPercent)!=0;
 shared.trayBattery.background=batteryPref(L"batteryBackground",defaults.background)!=0;
 shared.trayBattery.fontSize=std::clamp(batteryPref(L"batteryFontSize",defaults.fontSize),8,16);
 shared.trayBattery.notify=batteryPref(L"batteryNotify",defaults.notify)!=0;
 shared.trayBattery.threshold=std::clamp(batteryPref(L"batteryThreshold",defaults.threshold),1,99);
 int lowLimit=batteryPref(L"batteryLowLimit",defaults.lowLimit);
 int mediumLimit=batteryPref(L"batteryMediumLimit",defaults.mediumLimit);
 if (!trayStatus::BatterySettings::validLimits(lowLimit,mediumLimit)) {
  lowLimit=defaults.lowLimit;
  mediumLimit=defaults.mediumLimit;
 }
 shared.trayBattery.lowLimit=lowLimit;
 shared.trayBattery.mediumLimit=mediumLimit;
 shared.trayBattery.low=std::clamp(batteryPref(L"batteryLow",defaults.low),0,int(trayStatus::rgbMask));
 shared.trayBattery.medium=std::clamp(batteryPref(L"batteryMedium",defaults.medium),0,int(trayStatus::rgbMask));
 shared.trayBattery.normal=std::clamp(batteryPref(L"batteryNormal",defaults.normal),0,int(trayStatus::rgbMask));
 constexpr const wchar_t* keys[]{L"routePc", L"routeChat", L"routeOut"};
 for (size_t i = 0; i < streamSetup.selected.size(); ++i) {
  wchar_t id[512]{};
  GetPrivateProfileStringW(L"NovaMix", keys[i], L"", id, DWORD(std::size(id)), file.c_str());
  streamSetup.selected[i] = id;
 }
}
std::string stateJson(){
 std::lock_guard<std::mutex> lock(mutexState);
 const auto& s=shared;auto displayed=mix::volumeDisplay(s.physical,s.volumeControl,GetTickCount64());std::ostringstream j;
 j<<"{\"version\":\"0.20.10\",\"enabled\":"<<s.enabled<<",\"accent\":"<<s.accent<<",\"gentleMix\":"<<s.gentleMix<<",\"audio\":"<<s.audio<<",\"hid\":"<<s.hid<<",\"game\":"<<s.levels.pc<<",\"chat\":"<<s.levels.chat<<",\"balance\":"<<mix::position(s.levels)<<",\"baseGame\":"<<(s.baseLevels?s.baseLevels->pc:-1)<<",\"baseChat\":"<<(s.baseLevels?s.baseLevels->chat:-1)<<",\"master\":"<<s.master<<",\"physical\":"<<s.physical<<",\"physicalDisplay\":"<<displayed.percent<<",\"physicalPending\":"<<displayed.pending<<",\"mic\":"<<s.mic<<",\"battery\":"<<s.battery<<",\"chargerBattery\":"<<s.chargerBattery<<",\"charging\":"<<s.charging<<",\"overlay\":"<<s.overlay<<",\"keyboard\":"<<s.keyboardWanted<<",\"keyboardActive\":"<<s.keyboardActive<<",\"oled\":"<<s.oledHints<<",\"oledMix\":"<<s.oledMix<<",\"preview\":"<<preview<<",\"device\":"<<jsonText(s.endpoint)<<",\"notice\":"<<jsonText(s.notice)<<",\"error\":"<<jsonText(s.audioError)<<",\"hidError\":"<<jsonText(s.hidError)<<",\"syncError\":"<<jsonText(s.mixSyncError)<<",\"volumeSyncError\":"<<jsonText(s.volumeSyncError)<<"}";
 j.seekp(-1, std::ios_base::end);
 j << ",\"language\":" << s.english << ",\"batteryPercent\":" << s.trayBattery.showPercent
   << ",\"batteryBackground\":" << s.trayBattery.background
   << ",\"batteryFontSize\":" << s.trayBattery.fontSize
   << ",\"batteryNotify\":" << s.trayBattery.notify
   << ",\"batteryThreshold\":" << s.trayBattery.threshold
   << ",\"batteryLowLimit\":" << s.trayBattery.lowLimit
   << ",\"batteryMediumLimit\":" << s.trayBattery.mediumLimit
   << ",\"batteryLow\":" << s.trayBattery.low
   << ",\"batteryMedium\":" << s.trayBattery.medium
   << ",\"batteryNormal\":" << s.trayBattery.normal
   << ",\"runningRequested\":" << s.wantEnabled
   << ",\"routeReady\":" << streamSetup.ready() << ",\"routes\":[";
 for (size_t i = 0; i < streamSetup.selected.size(); ++i) {
  if (i) j << ',';
  j << streamSetup.number(streamSetup.selected[i]);
 }
 j << "],\"signal\":[" << streamSetup.signal[0] << ',' << streamSetup.signal[1]
   << "],\"devices\":[";
 bool first = true;
 for (const auto& device : streamSetup.devices) {
  if (!first) j << ',';
  first = false;
  j << "{\"number\":" << device.number << ",\"name\":" << jsonText(device.name) << '}';
 }
 j << "]}";
 return j.str();
}
struct HttpResponse {int code=200;std::string type="application/json; charset=utf-8",body;};
class WebUi {
 SOCKET listener=INVALID_SOCKET;std::thread thread;bool wsa=false;unsigned short port=0;
 std::string token,host;
 static std::string lowercase(std::string s){for(char& c:s)c=(char)tolower((unsigned char)c);return s;}
 static bool integer(const std::string& s,int& n){auto result=std::from_chars(s.data(),s.data()+s.size(),n);return result.ec==std::errc()&&result.ptr==s.data()+s.size()&&!s.empty();}
 std::string asset(const wchar_t* filename){std::ifstream f(fs::path(exeDir())/L"ui"/filename,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
 HttpResponse action(const std::string& query){
  auto divider=query.find('&');if(divider==std::string::npos||query.rfind("name=",0)!=0||query.compare(divider+1,6,"value=")!=0)return {400,"text/plain","Richiesta non valida"};
  std::string name=query.substr(5,divider-5),value=query.substr(divider+7);int n=0;if(!integer(value,n))return {400,"text/plain","Valore non valido"};
  int route = name == "route-pc" ? 0 : name == "route-chat" ? 1 : name == "route-out" ? 2 : -1;
  if (route >= 0) {
   {
    std::lock_guard<std::mutex> lock(mutexState);
    if (shared.enabled || shared.wantEnabled) return {409,"text/plain","Ferma i flussi prima di cambiare le uscite."};
    auto selected = std::find_if(streamSetup.devices.begin(), streamSetup.devices.end(),
                                 [n](const StreamDevice& device) { return device.number == n; });
    if (n && selected == streamSetup.devices.end()) return {404,"text/plain","Dispositivo non disponibile."};
    streamSetup.selected[size_t(route)] = n ? selected->id : L"";
    shared.audioError.clear();
   }
   preferencesDirty = true;
   wakeAudio();
   return {202,"application/json","{}"};
  }
  bool flag=name=="language"||name=="gentleMix"||name=="enabled"||name=="overlay"||name=="keyboard"||name=="oled"||name=="oledMix"||name=="quit"||
   name=="batteryPercent"||name=="batteryNotify"||name=="batteryBackground";
  bool color=name=="batteryLow"||name=="batteryMedium"||name=="batteryNormal";
  bool range=name=="accent"?(n>=0&&n<=2):name=="balance"?(n>=-100&&n<=100):(n>=0&&n<=100);
  if (color) range=n>=0&&n<=int(trayStatus::rgbMask);
  if (name=="batteryThreshold") range=n>=1&&n<=99;
  if (name=="batteryFontSize") range=n>=8&&n<=16;
  if (name=="batteryBands") range=trayStatus::BatterySettings::validLimits(n/100,n%100);
  if(flag?(n!=0&&n!=1):!range)return {400,"text/plain","Valore fuori intervallo"};
  if(name=="quit"){PostMessageW(windowH,EXIT_APP,0,0);return {200,"application/json","{\"ok\":true}"};}
  else {
   std::lock_guard<std::mutex> lock(mutexState);
   if (name == "enabled") {
    if (n && !streamSetup.ready()) return {409,"text/plain","Seleziona tre uscite distinte nella scheda Flussi."};
    if (n && shared.enabled && !shared.wantEnabled) return {409,"text/plain","Attendi la chiusura dei flussi."};
    shared.wantEnabled = n != 0;
    if (n) shared.audioError.clear();
    if (preview) shared.enabled = shared.audio = shared.wantEnabled;
   }
   else if(name=="balance"){shared.levels=mix::balance(n);shared.wantMix=shared.levels;++shared.mixRevision;markAudioCommand();shared.fromHardware=false;shared.osdKind=1;shared.osdSerial++;if(preview)shared.wantMix.reset();}
   else if(name=="master"){shared.master=n;markAudioCommand();shared.osdKind=4;shared.osdSerial++;}
   else if(name=="physical"){shared.wantPhysical=n;shared.volumeControl.request(n,GetTickCount64());++shared.volumeRevision;if(preview){shared.physical=n;shared.wantPhysical=-1;shared.osdKind=2;shared.osdSerial++;}}
   else if(name=="language")shared.english=n!=0;
   else if(name=="accent")shared.accent=n;
   else if(name=="gentleMix"){shared.gentleMix=n!=0;if(shared.fromHardware&&shared.baseLevels){shared.levels=mix::wheelLevels(*shared.baseLevels,shared.gentleMix);markAudioCommand();shared.osdKind=1;++shared.osdSerial;}}
   else if(name=="overlay")shared.overlay=n!=0;
   else if(name=="keyboard")shared.keyboardWanted=n!=0;
   else if(name=="oledMix"){shared.oledMix=n!=0;shared.mixSyncError.clear();}
   else if(name=="oled"){if(shared.mic<0&&n)return {409,"text/plain","Stato microfono non disponibile"};shared.oledHints=n!=0;}
   else if(name=="batteryPercent")shared.trayBattery.showPercent=n!=0;
   else if(name=="batteryBackground")shared.trayBattery.background=n!=0;
   else if(name=="batteryFontSize")shared.trayBattery.fontSize=n;
   else if(name=="batteryNotify")shared.trayBattery.notify=n!=0;
   else if(name=="batteryThreshold")shared.trayBattery.threshold=n;
   else if(name=="batteryBands"){
    shared.trayBattery.lowLimit=n/100;
    shared.trayBattery.mediumLimit=n%100;
   }
   else if(name=="batteryLow")shared.trayBattery.low=n;
   else if(name=="batteryMedium")shared.trayBattery.medium=n;
   else if(name=="batteryNormal")shared.trayBattery.normal=n;
   else return {400,"text/plain","Comando sconosciuto"};
  }
  preferencesDirty=true;wakeAudio();PostMessageW(windowH,UPDATE,0,0);return {202,"application/json","{\"ok\":true}"};
 }
 void run(){
  while(WaitForSingleObject(stopEvent,0)!=WAIT_OBJECT_0){fd_set ready;FD_ZERO(&ready);FD_SET(listener,&ready);timeval timeout{0,200000};if(select(0,&ready,nullptr,nullptr,&timeout)<=0)continue;
   SOCKET client=accept(listener,nullptr,nullptr);if(client==INVALID_SOCKET)continue;DWORD ms=800;setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,(char*)&ms,sizeof(ms));setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,(char*)&ms,sizeof(ms));
   std::string request;char buffer[2048];while(request.size()<=8192&&request.find("\r\n\r\n")==std::string::npos){int n=recv(client,buffer,sizeof(buffer),0);if(n<=0)break;request.append(buffer,n);}
   HttpResponse response=handle(request);std::string status=response.code==200?"OK":response.code==202?"Accepted":response.code==403?"Forbidden":response.code==404?"Not Found":"Bad Request";
   std::string message="HTTP/1.1 "+std::to_string(response.code)+" "+status+"\r\nContent-Type: "+response.type+"\r\nContent-Length: "+std::to_string(response.body.size())+"\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nContent-Security-Policy: default-src 'self'; style-src 'self'; script-src 'self'; connect-src 'self'; img-src 'self' data:; frame-ancestors 'none'; base-uri 'none'\r\n\r\n"+response.body;
   size_t at=0;while(at<message.size()){int n=send(client,message.data()+at,(int)(message.size()-at),0);if(n<=0)break;at+=n;}closesocket(client);
  }
 }
public:
 WebUi(){BYTE random[32];if(BCryptGenRandom(nullptr,random,sizeof(random),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)throw Failure{L"Generazione chiave locale non riuscita"};std::ostringstream s;for(BYTE b:random)s<<std::hex<<std::setw(2)<<std::setfill('0')<<(int)b;token=s.str();}
 ~WebUi(){stop();}
 void stop(){if(thread.joinable()){SetEvent(stopEvent);thread.join();}if(listener!=INVALID_SOCKET){closesocket(listener);listener=INVALID_SOCKET;}if(wsa){WSACleanup();wsa=false;}}
 std::string url()const{return "http://"+host+"/ui?token="+token;}
 bool start(){
  WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))return false;wsa=true;listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(listener==INVALID_SOCKET)return false;
  BOOL exclusive=TRUE;setsockopt(listener,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,(const char*)&exclusive,sizeof(exclusive));sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  if(bind(listener,(sockaddr*)&address,sizeof(address))==SOCKET_ERROR||listen(listener,8)==SOCKET_ERROR)return false;int n=sizeof(address);if(getsockname(listener,(sockaddr*)&address,&n))return false;port=ntohs(address.sin_port);host="127.0.0.1:"+std::to_string(port);thread=std::thread([this]{run();});return true;
 }
 void open(){openDesktopUi(wide(url()));}
 HttpResponse handle(const std::string& request){
  auto end=request.find("\r\n\r\n"),line=request.find("\r\n");if(end==std::string::npos||request.size()>8192||line==std::string::npos)return {400,"text/plain","Richiesta non valida"};
  std::istringstream first(request.substr(0,line));std::string method,target,version;first>>method>>target>>version;if(version!="HTTP/1.1"||target.empty())return {400,"text/plain","Richiesta non valida"};
  std::map<std::string,std::string> headers;size_t offset=line+2;while(offset<end){size_t next=request.find("\r\n",offset),colon=request.find(':',offset);if(colon==std::string::npos||colon>next)return {400,"text/plain","Header non valido"};std::string key=lowercase(request.substr(offset,colon-offset)),value=request.substr(colon+1,next-colon-1);while(!value.empty()&&value.front()==' ')value.erase(value.begin());if(headers.count(key))return {400,"text/plain","Header duplicato"};headers[key]=value;offset=next+2;}
  if(headers["host"]!=host||(headers.count("origin")&&headers["origin"]!="http://"+host))return {403,"text/plain","Accesso locale non valido"};
  if(method=="GET"&&target=="/style.css")return {200,"text/css; charset=utf-8",asset(L"style.css")};
  if(method=="GET"&&target=="/app.js")return {200,"text/javascript; charset=utf-8",asset(L"app.js")};
  if(method=="GET"&&target=="/ui?token="+token){auto html=asset(L"index.html");auto at=html.find("__TOKEN__");if(at!=std::string::npos)html.replace(at,9,token);if(html.empty())return {404,"text/plain","Manca la cartella ui accanto a NovaMix.exe"};return {200,"text/html; charset=utf-8",html};}
  if(headers["x-novamix-token"]!=token)return {403,"text/plain","Chiave locale non valida"};
  if(method=="GET"&&target=="/api/state")return {200,"application/json; charset=utf-8",stateJson()};
  if (method == "GET" && target == "/api/diagnostics") {
   const auto s = snapshot();
   auto label = [&s](const wchar_t* italian, const wchar_t* english) { return s.english ? english : italian; };
   std::wostringstream f;
   f << L"NovaMix 0.20.10\nAudio: " << s.audio << L"\n" << s.audioError
     << L"\nHID: " << s.hid << L"\n" << s.hidError
     << label(L"\nMicrofono: ", L"\nMicrophone: ") << s.mic
     << label(L"\nVolume confermato/target raw/in attesa: ", L"\nConfirmed volume/raw target/pending: ")
     << s.physical << L"/" << s.volumeControl.targetRaw << L"/" << s.volumeControl.awaiting
     << label(L"\nBatteria cuffie/slot: ", L"\nHeadset/slot battery: ") << s.battery << L"/" << s.chargerBattery
     << label(L"\nRicarica cuffie (1=offline, 2=in carica, 4=cariche, 8=scarica): ",
              L"\nHeadset charging (1=offline, 2=charging, 4=charged, 8=discharging): ") << s.charging
     << label(L"\nChatMix flussi PC/Chat: ", L"\nPC/Chat streams: ") << s.levels.pc << L"/" << s.levels.chat
     << L"\nChatMix base PC/Chat: " << (s.baseLevels ? s.baseLevels->pc : -1)
     << L"/" << (s.baseLevels ? s.baseLevels->chat : -1)
     << label(L"\nSchermata NovaMix sulla base: ", L"\nNovaMix base display: ") << s.oledMix
     << L"\nRX: " << s.packets
     << label(L"\nMix ricevuti: ", L"\nMix reports received: ") << s.mixPackets
     << label(L"\nCurva ChatMix graduale: ", L"\nSmoother ChatMix curve: ") << s.gentleMix
     << L"\n" << s.audioDetails << wheelReport() << L"\n" << s.hidDetails;
   return {200, "text/plain; charset=utf-8", utf8(f.str())};
  }
  const std::string prefix="/api/action?";if(method=="POST"&&target.rfind(prefix,0)==0)return action(target.substr(prefix.size()));
  return {404,"text/plain","Non trovato"};
 }
 int selfTest(){
  host="127.0.0.1:12345";int failed=0;auto expect=[&](bool value){if(!value)failed++;};
  auto req=[&](const std::string& method,const std::string& target,const std::string& extra=""){return handle(method+" "+target+" HTTP/1.1\r\nHost: "+host+"\r\n"+extra+"\r\n");};
  expect(req("GET","/api/state").code==403);expect(req("GET","/api/state","X-NovaMix-Token: "+token+"\r\n").code==200);
  expect(req("GET","/api/state","X-NovaMix-Token: "+token+"\r\nOrigin: https://example.com\r\n").code==403);
  expect(req("POST","/api/action?name=master&value=101","X-NovaMix-Token: "+token+"\r\n").code==400);
  expect(req("POST","/api/action?name=language&value=2","X-NovaMix-Token: "+token+"\r\n").code==400);
  expect(req("POST","/api/action?name=language&value=1","X-NovaMix-Token: "+token+"\r\n").code==202&&snapshot().english);
  expect(req("POST","/api/action?name=enabled&value=2","X-NovaMix-Token: "+token+"\r\n").code==400);
  expect(req("POST","/api/action?name=bad&value=20","X-NovaMix-Token: "+token+"\r\n").code==400);
  expect(req("POST","/api/action?name=master&value=40","X-NovaMix-Token: "+token+"\r\n").code==202&&snapshot().master==40);
  expect(req("POST","/api/action?name=balance&value=-60","X-NovaMix-Token: "+token+"\r\n").code==202&&snapshot().wantMix&&mix::same(*snapshot().wantMix,{100,40}));
  expect(req("POST","/api/action?name=physical&value=35","X-NovaMix-Token: "+token+"\r\n").code==202&&snapshot().wantPhysical==35&&snapshot().physical==-1);
  expect(jsonText(L"a\n\"\\") == "\"a\\n\\\"\\\\\"");
  expect(req("POST","/api/action?name=oledMix&value=2","X-NovaMix-Token: "+token+"\r\n").code==400);
  expect(req("POST","/api/action?name=oledMix&value=0","X-NovaMix-Token: "+token+"\r\n").code==202&&!snapshot().oledMix);
  expect(req("POST","/api/action?name=oledMix&value=1","X-NovaMix-Token: "+token+"\r\n").code==202&&snapshot().oledMix);
  expect(req("POST","/api/action?name=gentleMix&value=2","X-NovaMix-Token: "+token+"\r\n").code==400);
  shared.fromHardware=false;auto keep=shared.levels;
  expect(req("POST","/api/action?name=gentleMix&value=0","X-NovaMix-Token: "+token+"\r\n").code==202&&!shared.gentleMix&&mix::same(shared.levels,keep));
  shared.fromHardware=true;shared.baseLevels=mix::Levels{100,8};
  expect(req("POST","/api/action?name=gentleMix&value=1","X-NovaMix-Token: "+token+"\r\n").code==202&&shared.gentleMix&&shared.levels.chat==2);
  expect(stateJson().find("\"gentleMix\":1")!=std::string::npos);
  expect(req("POST","/api/action?name=accent&value=3","X-NovaMix-Token: "+token+"\r\n").code==400);
  expect(req("POST","/api/action?name=accent&value=2","X-NovaMix-Token: "+token+"\r\n").code==202&&shared.accent==2);
  streamSetup.devices = {{1,L"pc",L"PC"},{2,L"chat",L"Chat"},{3,L"out",L"Cuffie"}};
  auto route = [&](const std::string& name, int value) {
   return req("POST", "/api/action?name=" + name + "&value=" + std::to_string(value),
              "X-NovaMix-Token: " + token + "\r\n");
  };
  expect(route("enabled",1).code == 409);
  expect(route("route-pc",1).code == 202 && route("route-chat",2).code == 202);
  expect(route("route-out",9).code == 404 && route("route-out",1).code == 202);
  expect(route("enabled",1).code == 409);
  expect(route("route-out",3).code == 202 && streamSetup.ready());
  expect(route("enabled",1).code == 202 && route("route-pc",0).code == 409);
  expect(route("enabled",0).code == 202);
  expect(route("batteryThreshold",0).code == 400 && route("batteryThreshold",100).code == 400);
  expect(route("batteryLow",16777216).code == 400 && route("batteryPercent",2).code == 400);
  expect(route("batteryBackground",2).code == 400 && route("batteryFontSize",7).code == 400 &&
         route("batteryFontSize",17).code == 400);
  expect(route("batteryBackground",0).code == 202 && !shared.trayBattery.background);
  expect(route("batteryFontSize",10).code == 202 && shared.trayBattery.fontSize == 10);
  expect(route("batteryPercent",1).code == 202 && route("batteryNotify",1).code == 202);
  expect(route("batteryThreshold",31).code == 202 && route("batteryLow",0x123456).code == 202);
  expect(route("batteryBands",4055).code == 202);
  expect(route("batteryBands",4040).code == 400 && route("batteryBands",5540).code == 400);
  auto settings = fs::path(exeDir()) / L"test-routes.ini";
  savePreferences(settings);
  const bool savedEnglish = shared.english;
  shared.english = !savedEnglish;
  shared.trayBattery = {};
  expect(route("route-pc",0).code == 202 && !streamSetup.ready());
  loadPreferences(settings);
  expect(shared.english == savedEnglish);
  expect(shared.trayBattery.showPercent && shared.trayBattery.notify &&
         !shared.trayBattery.background && shared.trayBattery.fontSize == 10 &&
         shared.trayBattery.threshold == 31 && shared.trayBattery.low == 0x123456 &&
         shared.trayBattery.lowLimit == 40 && shared.trayBattery.mediumLimit == 55);
  expect(stateJson().find("\"batteryThreshold\":31") != std::string::npos);
  expect(stateJson().find("\"batteryBackground\":0") != std::string::npos &&
         stateJson().find("\"batteryFontSize\":10") != std::string::npos);
  const auto legacy = fs::path(exeDir()) / L"test-legacy.ini";
  WritePrivateProfileStringW(L"NovaMix", L"batteryPercent", L"1", legacy.c_str());
  loadPreferences(legacy);
  expect(shared.trayBattery.showPercent && shared.trayBattery.background && shared.trayBattery.fontSize == 12);
  DeleteFileW(legacy.c_str());
  loadPreferences(settings);
  expect(streamSetup.ready() && streamSetup.selected == std::array<std::wstring,3>{L"pc",L"chat",L"out"});
  expect(!shared.wantEnabled);
  std::ofstream(fs::path(exeDir())/L"test-http.txt")<<(failed?"FAIL ":"PASS ")<<failed<<" failed; local host/origin/token checks, command validation, stream route availability, duplicate and active-route guards\n";return failed?1:0;
 }
};
