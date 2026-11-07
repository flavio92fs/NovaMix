#pragma once

// The audio engine owns the tray and OSD. The Electron frontend is disposable:
// closing its window releases its runtime while the native mixer keeps running.
class DesktopUi {
 HANDLE process=nullptr;
 DWORD processId=0;

 struct WindowSearch {DWORD processId;HWND window=nullptr;};
 static BOOL CALLBACK findWindow(HWND window,LPARAM parameter){
  auto search=reinterpret_cast<WindowSearch*>(parameter);
  DWORD owner=0;GetWindowThreadProcessId(window,&owner);
  if(owner==search->processId&&IsWindowVisible(window)&&!GetWindow(window,GW_OWNER)&&
     !(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOOLWINDOW)){
   search->window=window;return FALSE;
  }
  return TRUE;
 }
 static BOOL CALLBACK closeWindow(HWND window,LPARAM parameter){
  DWORD owner=0;GetWindowThreadProcessId(window,&owner);
  if(owner==static_cast<DWORD>(parameter)&&IsWindowVisible(window)&&!GetWindow(window,GW_OWNER))
   PostMessageW(window,WM_CLOSE,0,0);
  return TRUE;
 }
 HWND window()const{
  WindowSearch search{processId};EnumWindows(findWindow,reinterpret_cast<LPARAM>(&search));return search.window;
 }
 void release(){if(process)CloseHandle(process);process=nullptr;processId=0;}
 void fail(DWORD error){
  std::ofstream(fs::path(exeDir())/L"ui-launch-error.txt")<<"Electron CreateProcess failed: "<<error<<"\n";
  if(preview)std::ofstream(fs::path(exeDir())/L"test-desktop.txt")<<"FAIL Electron launch "<<error<<"\n";
  else {
   std::wstring message=L"Impossibile aprire l'interfaccia NovaMix: "+errorText(HRESULT_FROM_WIN32(error))+
    L"\n\nEstrai tutta la cartella dell'applicazione, inclusa frontend. Il mixer resta nell'area di notifica.";
   MessageBoxW(nullptr,message.c_str(),L"NovaMix",MB_OK|MB_ICONINFORMATION);
  }
 }
 // Quote for Windows argv parsing, including trailing backslashes. No shell is involved.
 static std::wstring quote(const std::wstring& value){
  std::wstring result=L"\"";size_t slashes=0;
  for(wchar_t character:value){
   if(character==L'\\'){++slashes;continue;}
   if(character==L'\"'){result.append(slashes*2+1,L'\\');result+=character;}
   else {result.append(slashes,L'\\');result+=character;}
   slashes=0;
  }
  result.append(slashes*2,L'\\');result+=L'\"';return result;
 }
 static std::wstring commandLine(const std::wstring& executable,const std::wstring& address,DWORD parent,bool previewMode){
  // Electron's Windows URL-injection check runs before JavaScript. After Chromium
  // groups switches, the authenticated URL must still be the last argument.
  std::wstring command=quote(executable)+L" --parent "+std::to_wstring(parent);
  if(previewMode)command+=L" --preview";
  return command+L" --url "+quote(address);
 }
public:
 DesktopUi()=default;
 DesktopUi(const DesktopUi&)=delete;
 DesktopUi& operator=(const DesktopUi&)=delete;
 ~DesktopUi(){release();}
 static int selfTest();
 void poll(){
  if(!process||WaitForSingleObject(process,0)!=WAIT_OBJECT_0)return;
  DWORD exitCode=0;bool known=GetExitCodeProcess(process,&exitCode)!=FALSE;
  // Release before reporting: timer ticks and a later open cannot report twice.
  release();
  if(!known||exitCode==0)return;
  std::ofstream(fs::path(exeDir())/L"ui-launch-error.txt")
   <<"Electron exited with code "<<static_cast<LONG>(exitCode)
   <<" (unsigned "<<exitCode<<", hex 0x"<<std::hex<<std::uppercase<<exitCode<<").\n";
  if(preview||(stopEvent&&WaitForSingleObject(stopEvent,0)==WAIT_OBJECT_0))return;
  std::wstring message=L"L'interfaccia grafica NovaMix si e chiusa con errore (codice "+
   std::to_wstring(static_cast<LONG>(exitCode))+L").\n\n"
   L"Il mixer continua nell'area di notifica. Puoi riprovare con Apri NovaMix.\n"
   L"I dettagli sono in ui-launch-error.txt nella cartella del programma.";
  MessageBoxW(nullptr,message.c_str(),L"NovaMix - interfaccia",MB_OK|MB_ICONINFORMATION);
 }
 void open(const std::wstring& address){
  poll();
  if(opened()){
   if(auto existing=window()){ShowWindow(existing,IsIconic(existing)?SW_RESTORE:SW_SHOW);SetForegroundWindow(existing);}
   // An active child may still be creating its window. Do not start a second UI.
   return;
  }
  release();
  auto executable=(fs::path(exeDir())/L"frontend"/L"NovaMixUi.exe").wstring();
  auto directory=fs::path(executable).parent_path().wstring();
  std::wstring command=commandLine(executable,address,GetCurrentProcessId(),preview);
  STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_SHOWNORMAL;
  PROCESS_INFORMATION child{};
  if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,directory.c_str(),&startup,&child)){
   fail(GetLastError());return;
  }
  process=child.hProcess;processId=child.dwProcessId;CloseHandle(child.hThread);
  AllowSetForegroundWindow(processId);
 }
 void close(){
  if(opened())EnumWindows(closeWindow,static_cast<LPARAM>(processId));
  // The frontend also watches --parent, covering startup races and engine exit.
  release();
 }
 bool opened()const{return process&&WaitForSingleObject(process,0)==WAIT_TIMEOUT;}
};
#include "desktop_tests.hpp"
DesktopUi desktop;
void openDesktopUi(const std::wstring& address){desktop.open(address);}
