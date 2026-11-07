#pragma once

namespace desktopTests {
inline std::vector<std::wstring> parseWindows(const std::wstring& command){
 int count=0;auto raw=CommandLineToArgvW(command.c_str(),&count);
 if(!raw)return {};
 std::vector<std::wstring> arguments(raw,raw+count);LocalFree(raw);return arguments;
}

// External launch contract, independently checked after Windows tokenization.
// Chromium groups switches before positionals; Electron then rejects anything
// after a URL. Sources: base/command_line.cc in Chromium and Electron v45.0.0
// shell/app/command_line_args.cc (CVE-2018-1000006 protection).
inline bool electronWindowsAccepts(const std::vector<std::wstring>& arguments){
 if(arguments.empty())return false;
 std::vector<std::wstring> canonical{arguments.front()},positionals;
 bool switches=true;
 for(size_t i=1;i<arguments.size();++i){
  const auto& argument=arguments[i];
  if(argument==L"--")switches=false;
  bool isSwitch=switches&&argument.size()>1&&(argument.front()==L'-'||argument.front()==L'/');
  (isSwitch?canonical:positionals).push_back(argument);
 }
 canonical.insert(canonical.end(),positionals.begin(),positionals.end());
 bool sawUrl=false;
 for(const auto& argument:canonical){
  if(argument==L"--")return true;
  if(sawUrl)return false;
  auto colon=argument.find(L':');
  if(colon==std::wstring::npos||colon<2)continue;
  wchar_t first=argument.front();
  bool alphabetic=(first>=L'A'&&first<=L'Z')||(first>=L'a'&&first<=L'z');
  sawUrl=alphabetic&&argument.substr(0,colon).find_first_of(L" \t\r\n\f\v")==std::wstring::npos;
 }
 return true;
}
}

inline int DesktopUi::selfTest(){
 std::ofstream report(fs::path(exeDir())/L"test-desktop-launcher.txt");
 bool passed=true;
 auto expect=[&](bool result,const char* name){report<<(result?"PASS ":"FAIL ")<<name<<"\n";passed=passed&&result;};
 const std::wstring address=L"http://127.0.0.1:45678/ui?token="+std::wstring(64,L'a');
 const DWORD parent=123456;
 const std::vector<std::wstring> executablePaths={
  L"C:\\NovaMix\\frontend\\NovaMixUi.exe",
  L"C:\\NovaMix with spaces\\frontend\\NovaMixUi.exe",
  L"C:\\Utenti\\Fl\u00e0vio\\NovaMix\\frontend\\NovaMixUi.exe"
 };
 for(const auto& executable:executablePaths){
  for(bool previewMode:{false,true}){
   auto actual=desktopTests::parseWindows(commandLine(executable,address,parent,previewMode));
   std::vector<std::wstring> expected{executable,L"--parent",std::to_wstring(parent)};
   if(previewMode)expected.push_back(L"--preview");
   expected.push_back(L"--url");expected.push_back(address);
   expect(actual==expected,previewMode?"preview argv round-trip":"normal argv round-trip");
   expect(!actual.empty()&&actual.back()==address,"authenticated URL is final");
   expect(desktopTests::electronWindowsAccepts(actual),"passes Electron Windows URL protection");
   auto previous=desktopTests::parseWindows(quote(executable)+L" --url "+quote(address)+L" --parent "+std::to_wstring(parent)+(previewMode?L" --preview":L""));
   expect(!desktopTests::electronWindowsAccepts(previous),"regression: previous URL-first command is rejected");
  }
 }
 // Exercise escaping as a regular argv value, including trailing backslashes.
 for(const auto& value:std::vector<std::wstring>{L"",L"C:\\path with spaces\\",L"C:\\two trailing\\\\",L"quote\"inside",L"slash\\\"quote"}){
  auto parsed=desktopTests::parseWindows(L"probe.exe "+quote(value));
  expect(parsed.size()==2&&parsed[1]==value,"Windows quote round-trip");
 }
 report<<(passed?"PASS":"FAIL")<<": production launcher preserves argv and places URL last without bypassing Electron security.\n";
 return passed?0:1;
}
