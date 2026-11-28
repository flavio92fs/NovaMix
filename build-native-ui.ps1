#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$CargoPath = 'cargo',
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '.build\iced-frontend'),
    [switch]$RunTests,
    [switch]$CheckOnly
)
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$install = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$install) { throw 'Install Visual Studio C++ Build Tools and the Windows 11 SDK.' }
$toolset = Get-ChildItem -LiteralPath (Join-Path $install 'VC\Tools\MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$sdk = Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'Include') -Directory | Sort-Object Name -Descending | Select-Object -First 1
$savedEnvironment = @{}
foreach ($name in @('PATH','LIB','INCLUDE','RUSTFLAGS','CARGO_ENCODED_RUSTFLAGS')) { $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    $env:PATH = (Join-Path $toolset.FullName 'bin\Hostx64\x64') + ';' + (Join-Path $sdkRoot "bin\$($sdk.Name)\x64") + ';' + $env:PATH
    $env:LIB = (Join-Path $toolset.FullName 'lib\x64') + ';' + (Join-Path $sdkRoot "Lib\$($sdk.Name)\um\x64") + ';' + (Join-Path $sdkRoot "Lib\$($sdk.Name)\ucrt\x64")
    $env:INCLUDE = (Join-Path $toolset.FullName 'include') + ';' + (Join-Path $sdk.FullName 'ucrt') + ';' + (Join-Path $sdk.FullName 'shared') + ';' + (Join-Path $sdk.FullName 'um')
    $rustArguments = @('-C','target-feature=+crt-static','--remap-path-prefix',($PSScriptRoot + '=NovaMix'))
    if ($env:CARGO_HOME) { $rustArguments += @('--remap-path-prefix', ($env:CARGO_HOME + '=rust-dependencies')) }
    if ($env:RUSTUP_HOME) { $rustArguments += @('--remap-path-prefix', ($env:RUSTUP_HOME + '=rust-toolchain')) }
    $rustArguments += @('--remap-path-prefix', ([Environment]::GetFolderPath('UserProfile') + '=build-user'))
    $env:CARGO_ENCODED_RUSTFLAGS = [string]::Join([char]31, $rustArguments)
    $manifest = Join-Path $PSScriptRoot 'native-ui\Cargo.toml'
    if ($RunTests) {
        & $CargoPath test --locked --manifest-path $manifest
        if ($LASTEXITCODE) { throw 'Native UI tests failed.' }
    }
    if ($CheckOnly) {
        & $CargoPath check --locked --manifest-path $manifest
        if ($LASTEXITCODE) { throw 'Native UI check failed.' }
    } else {
        & $CargoPath build --locked --release --manifest-path $manifest
        if ($LASTEXITCODE) { throw 'Native UI build failed.' }
        New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'native-ui\target\release\NovaMixUi.exe') -Destination (Join-Path $OutputDirectory 'NovaMixUi.exe') -Force
        Set-Content -LiteralPath (Join-Path $OutputDirectory 'version') -Value 'iced-0.21.0-alpha.3' -Encoding Ascii
        & (Join-Path $PSScriptRoot 'native-ui\package-licenses.ps1') -CargoPath $CargoPath -Destination (Join-Path $OutputDirectory 'licenses')
        Get-Item -LiteralPath (Join-Path $OutputDirectory 'NovaMixUi.exe') | Select-Object Name,Length
    }
} finally {
    foreach ($name in $savedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process') }
}
