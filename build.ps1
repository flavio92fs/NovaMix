param([string]$OutputName = 'NovaMix.exe')
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Build Tools C++ non trovato.' }
$install = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$install) { throw 'Installa il componente MSVC C++ nei Build Tools.' }
$toolset = Get-ChildItem -LiteralPath (Join-Path $install 'VC\Tools\MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$sdk = Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'Include') -Directory | Sort-Object Name -Descending | Select-Object -First 1
$compiler = Join-Path $toolset.FullName 'bin\Hostx64\x64\cl.exe'
$buildDir = Join-Path $PSScriptRoot '.build\native'
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
$resourceCompiler=Join-Path $sdkRoot "bin\$($sdk.Name)\x64\rc.exe"
Push-Location $PSScriptRoot
try { & $resourceCompiler /nologo "/I$($sdk.FullName)\um" "/I$($sdk.FullName)\shared" "/fo$buildDir\NovaMix.res" 'app.rc'; if($LASTEXITCODE -ne 0){throw 'Compilazione risorse fallita.'} } finally { Pop-Location }
$params = @('/nologo','/std:c++17','/EHsc','/O2','/MT','/W4','/utf-8','/D_WIN32_WINNT=0x0A00',"/I$($toolset.FullName)\include", "/I$($sdk.FullName)\ucrt", "/I$($sdk.FullName)\shared", "/I$($sdk.FullName)\um", "/I$($sdk.FullName)\winrt", "/Fo$buildDir\NovaMix.obj", "/Fe$PSScriptRoot\NovaMix.exe", "$PSScriptRoot\main.cpp", '/link','/SUBSYSTEM:WINDOWS','/DYNAMICBASE','/NXCOMPAT','/OPT:REF','/OPT:ICF', "/LIBPATH:$($toolset.FullName)\lib\x64", "/LIBPATH:$sdkRoot\Lib\$($sdk.Name)\um\x64", "/LIBPATH:$sdkRoot\Lib\$($sdk.Name)\ucrt\x64", 'user32.lib','gdi32.lib','gdiplus.lib','comctl32.lib','shell32.lib','ole32.lib','oleaut32.lib','uuid.lib','setupapi.lib','hid.lib','propsys.lib','advapi32.lib')
$params = $params | ForEach-Object { $_.Replace("/Fe$PSScriptRoot\NovaMix.exe", "/Fe$PSScriptRoot\$OutputName") }
$params += 'dwmapi.lib','ws2_32.lib','bcrypt.lib','avrt.lib'
$params += "$buildDir\NovaMix.res",'shlwapi.lib','version.lib'
& $compiler @params
if ($LASTEXITCODE -ne 0) { throw 'Compilazione C++ fallita.' }
Get-Item -LiteralPath (Join-Path $PSScriptRoot $OutputName) | Select-Object Name,Length
