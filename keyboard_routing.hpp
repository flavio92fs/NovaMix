#pragma once

namespace keyboardRouting {
// Match stable endpoint IDs, not the user-editable "NovaMix PC/Chat" labels.
inline bool matches(const std::wstring& current, bool directHeadset,
                    const std::array<std::wstring,3>& routes, bool outputHeadset) {
 if(current.empty())return false;
 return directHeadset || (outputHeadset &&
  ((!routes[0].empty()&&current==routes[0]) || (!routes[1].empty()&&current==routes[1])));
}

// The notification callback invalidates the gate immediately. The UI thread
// can refresh/unhook later; the keyboard hook never performs COM or audio I/O.
class Gate {
 std::atomic<uint64_t> revision{1},published{0};
 std::atomic<bool> route{false};
public:
 uint64_t epoch()const{return revision.load();}
 void invalidate(){++revision;}
 void publish(bool allowed,uint64_t epoch){route=allowed;published=epoch;}
 bool allows(uint64_t epoch)const{
  return epoch==revision.load()&&published.load()==epoch&&route.load()&&epoch==revision.load();
 }
 bool allows()const{return allows(epoch());}
};
Gate gate;

class Client final:public IMMNotificationClient {
 std::atomic<ULONG> references{1};
 std::atomic<bool>& dirty;
 void changed(){dirty=true;gate.invalidate();notify();}
public:
 explicit Client(std::atomic<bool>& target):dirty(target){}
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** object)override{
  if(!object)return E_POINTER;
  *object=nullptr;
  if(iid==__uuidof(IUnknown)||iid==__uuidof(IMMNotificationClient)){
   *object=static_cast<IMMNotificationClient*>(this);AddRef();return S_OK;
  }
  return E_NOINTERFACE;
 }
 ULONG STDMETHODCALLTYPE AddRef()override{return ++references;}
 ULONG STDMETHODCALLTYPE Release()override{auto count=--references;if(!count)delete this;return count;}
 HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow,ERole role,LPCWSTR)override{
  // Changing only Discord's communications device must not change system keys.
  if(flow==eRender&&role==eConsole)changed();return S_OK;
 }
 HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR,DWORD)override{changed();return S_OK;}
 HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR)override{changed();return S_OK;}
 HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR)override{changed();return S_OK;}
 HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR,const PROPERTYKEY)override{return S_OK;}
};

inline bool isHeadset(IMMDevice* device){
 if(!device)return false;
 DWORD state=0;if(FAILED(device->GetState(&state))||!(state&DEVICE_STATE_ACTIVE))return false;
 ComPtr<IPropertyStore> properties;
 if(FAILED(device->OpenPropertyStore(STGM_READ,&properties)))return false;
 PROPVARIANT adapter;PropVariantInit(&adapter);
 const auto result=properties->GetValue(PKEY_DeviceInterface_FriendlyName,&adapter);
 const bool headset=SUCCEEDED(result)&&adapter.vt==VT_LPWSTR&&adapter.pwszVal&&
  lower(adapter.pwszVal).find(L"arctis nova pro wireless")!=std::wstring::npos;
 PropVariantClear(&adapter);return headset;
}

class Monitor {
 ComPtr<IMMDeviceEnumerator> enumerator;
 ComPtr<Client> client;
 std::atomic<bool> dirty{true};
 std::array<std::wstring,3> previousRoutes;
 bool registered=false;
public:
 ~Monitor(){stop();}
 bool start(){
  stop();
  if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,
                             IID_PPV_ARGS(&enumerator))))return false;
  client.Attach(new Client(dirty));
  registered=SUCCEEDED(enumerator->RegisterEndpointNotificationCallback(client.Get()));
  if(!registered){client.Reset();enumerator.Reset();}
  dirty=true;return registered;
 }
 void stop(){
  gate.invalidate();
  if(registered&&enumerator)enumerator->UnregisterEndpointNotificationCallback(client.Get());
  registered=false;client.Reset();enumerator.Reset();
 }
 bool refresh(const std::array<std::wstring,3>& routes){
  if(!registered)return false;
  if(routes!=previousRoutes){previousRoutes=routes;dirty=true;gate.invalidate();}
  const bool changed=dirty.exchange(false);
  if(!changed)return gate.allows();
  const auto epoch=gate.epoch();
  bool allowed=false;
  ComPtr<IMMDevice> current,output;
  if(SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender,eConsole,&current))){
   LPWSTR id=nullptr;
   if(SUCCEEDED(current->GetId(&id))){
    if(!routes[2].empty())enumerator->GetDevice(routes[2].c_str(),&output);
    allowed=matches(id,isHeadset(current.Get()),routes,isHeadset(output.Get()));
    CoTaskMemFree(id);
   }
  }
  // An event arriving during the queries leaves this publication stale, so it
  // cannot re-enable capture until the next refresh completes.
  gate.publish(allowed,epoch);return gate.allows();
 }
};
Monitor monitor;

inline bool queueVolume(int command,uint64_t epoch){
 static int beforeMute=50;
 if(!gate.allows(epoch))return false;
 std::lock_guard<std::mutex> lock(mutexState);
 if(!shared.keyboardWanted||!shared.enabled||!shared.hid||shared.physical<0||!gate.allows(epoch))return false;
 auto now=GetTickCount64();
 int target=shared.volumeControl.baseline(shared.physical,now);
 if(command==1)target=shared.volumeControl.step(shared.physical,1,now);
 else if(command==2)target=shared.volumeControl.step(shared.physical,-1,now);
 else if(command==3){if(target){beforeMute=target;target=0;}else target=beforeMute;shared.volumeControl.request(target,now);}
 else return false;
 shared.wantPhysical=target;++shared.volumeRevision;return true;
}

inline int selfTest(){
 std::ofstream report(fs::path(exeDir())/L"test-keyboard-routing.txt");int failed=0;
 auto expect=[&](bool ok,const char* name){report<<(ok?"PASS ":"FAIL ")<<name<<'\n';if(!ok)++failed;};
 const std::array<std::wstring,3> routes{L"pc-id",L"chat-id",L"headset-id"};
 expect(matches(L"pc-id",false,routes,true)&&matches(L"chat-id",false,routes,true),"PC and Chat routed to Nova use physical keys");
 expect(matches(L"headset-id",true,routes,true),"direct Nova endpoint uses physical keys");
 for(const auto* other:{L"bluetooth-id",L"hdmi-id",L"speakers-id",L""})
  expect(!matches(other,false,routes,true),"external or missing Windows default passes through");
 expect(!matches(L"pc-id",false,routes,false),"NovaMix playing to another device does not take headset keys");
 expect(!matches(L"pc-id",false,{L"",L"",L""},true),"unconfigured routes pass through");
 shared.enabled=shared.hid=shared.keyboardWanted=true;shared.physical=60;shared.master=73;
 auto old=gate.epoch();gate.publish(true,old);
 expect(queueVolume(2,old)&&shared.wantPhysical<60&&shared.master==73,"Nova command preserves software master");
 auto queued=shared.volumeRevision;auto target=shared.wantPhysical;
 std::atomic<bool> dirty{false};ComPtr<Client> callback;callback.Attach(new Client(dirty));
 callback->OnDefaultDeviceChanged(eCapture,eConsole,L"microphone-id");
 callback->OnDefaultDeviceChanged(eRender,eCommunications,L"chat-only-id");
 expect(gate.allows(old)&&!dirty.load(),"microphone and communications roles leave system keys unchanged");
 callback->OnDefaultDeviceChanged(eRender,eConsole,L"bluetooth-id");
 expect(!gate.allows()&&dirty.load(),"default output notification immediately releases capture");
 expect(!queueVolume(1,old)&&!queueVolume(2,old)&&!queueVolume(3,old)&&
        shared.volumeRevision==queued&&shared.wantPhysical==target,"queued up down and mute cannot alter Nova after switching output");
 gate.publish(true,old);expect(!gate.allows(),"late pre-switch result cannot enable capture");
 gate.publish(false,gate.epoch());expect(!queueVolume(1,gate.epoch()),"speaker mode leaves volume up to Windows");
 gate.publish(true,gate.epoch());expect(queueVolume(1,gate.epoch()),"returning to Nova restores capture automatically");
 shared.keyboardWanted=false;expect(!queueVolume(1,gate.epoch()),"disabled keyboard preference passes through");
 shared.keyboardWanted=true;shared.enabled=false;expect(!queueVolume(1,gate.epoch()),"stopped mixer passes through");
 report<<(failed?"FAIL":"PASS")<<": "<<failed<<" keyboard routing failures\n";return failed?1:0;
}
}
