#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$ElectronArchive,
    [string]$CableArchive,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'dist')
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$build = Join-Path $PSScriptRoot '.build'
New-Item -ItemType Directory -Path $build, $OutputDirectory -Force | Out-Null
$release = Join-Path (Resolve-Path -LiteralPath $OutputDirectory).ProviderPath 'NovaMix-Setup-x64.exe'
$cableName = 'VBCABLE_Driver_Pack45.zip'
$cableHash = 'B950E39F01AF1D04EA623C8F6D8EB9B6EA5C477C637295FABF20631C85116BFB'
if (!$CableArchive) {
    $CableArchive = Join-Path $build $cableName
    if (!(Test-Path -LiteralPath $CableArchive -PathType Leaf)) {
        Invoke-WebRequest -Uri "https://download.vb-audio.com/Download_CABLE/$cableName" -OutFile ($CableArchive + '.partial') -UseBasicParsing
        if ((Get-FileHash -LiteralPath ($CableArchive + '.partial')).Hash -ne $cableHash) {
            throw 'VB-CABLE download checksum mismatch.'
        }
        Move-Item -LiteralPath ($CableArchive + '.partial') -Destination $CableArchive
    }
}
$CableArchive = (Resolve-Path -LiteralPath $CableArchive).ProviderPath
if ((Get-FileHash -LiteralPath $CableArchive).Hash -ne $cableHash) {
    throw "VB-CABLE checksum mismatch. Expected the unmodified $cableName package."
}

& (Join-Path $PSScriptRoot 'build.ps1') -OutputName '.build\NovaMix.exe'
# Prepare the runtime from a checksum-verified archive on every release build.
& (Join-Path $PSScriptRoot 'build-ui.ps1') -ArchivePath $ElectronArchive

$framework = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319'
$csc = Join-Path $framework 'csc.exe'
$icon = Join-Path $PSScriptRoot 'assets/NovaMix.ico'
$guide = Join-Path $build 'Configura audio NovaMix.exe'
$appZip = Join-Path $build 'app.zip'
$options = @('/nologo','/target:winexe','/platform:x64','/optimize+','/warn:4','/warnaserror',"/win32icon:$icon")
$setupSource = Join-Path $PSScriptRoot 'installer/Setup.cs'
$devicesSource = Join-Path $PSScriptRoot 'installer/AudioDevices.cs'
& $csc @options '/define:GUIDE_ONLY' "/out:$guide" $setupSource $devicesSource
if ($LASTEXITCODE) { throw 'Audio Setup build failed.' }

Add-Type -AssemblyName System.IO.Compression.FileSystem
$target = [IO.Compression.ZipArchive]::new([IO.File]::Create($appZip), [IO.Compression.ZipArchiveMode]::Create)
try {
    $appFiles = @{
        'LICENSE' = Join-Path $PSScriptRoot 'LICENSE'
        'NovaMix.exe' = Join-Path $build 'NovaMix.exe'
        'Configura audio NovaMix.exe' = $guide
    }
    foreach ($name in @('ui/index.html','ui/app.js','ui/style.css','assets/NovaMix.ico')) {
        $appFiles[$name] = Join-Path $PSScriptRoot $name
    }
    $frontend = Join-Path $PSScriptRoot 'frontend'
    foreach ($file in Get-ChildItem -LiteralPath $frontend -File -Recurse) {
        $name = 'frontend/' + $file.FullName.Substring($frontend.Length + 1).Replace('\', '/')
        $appFiles[$name] = $file.FullName
    }
    foreach ($shellFile in @('main.cjs','preload.cjs','package.json')) {
        $appFiles["frontend/resources/app/$shellFile"] = Join-Path $PSScriptRoot "electron-src/$shellFile"
    }
    foreach ($name in ($appFiles.Keys | Sort-Object)) {
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($target, $appFiles[$name], $name) | Out-Null
    }
}
finally { $target.Dispose() }

$refs = @('/reference:System.IO.Compression.dll','/reference:System.IO.Compression.FileSystem.dll')
& $csc @options @refs "/out:$release" "/resource:$appZip,NovaMix.App.zip" "/resource:$CableArchive,NovaMix.VBCable.zip" $setupSource $devicesSource
if ($LASTEXITCODE) { throw 'Installer build failed.' }
$hash = (Get-FileHash -LiteralPath $release -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath ($release + '.sha256') -Value "$hash  NovaMix-Setup-x64.exe" -Encoding Ascii
Get-Item -LiteralPath $release, ($release + '.sha256') | Select-Object Name,Length
