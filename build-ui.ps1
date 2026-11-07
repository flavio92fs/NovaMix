#requires -Version 5.1
[CmdletBinding()]
param(
    # Optional already-downloaded archive; its pinned SHA256 is still verified.
    [string]$ArchivePath
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'Questa distribuzione Electron richiede Windows x64.'
}

$electronVersion = '45.2.3'
$archiveName = "electron-v$electronVersion-win32-x64.zip"
$archiveUrl = "https://github.com/electron/electron/releases/download/v$electronVersion/$archiveName"
$expectedSha256 = '489cd061908e697394b004298bf5ffd345c935d10e8ca197ba819f019ab2dec0'
$packageRoot = (Resolve-Path -LiteralPath $PSScriptRoot).ProviderPath.TrimEnd('\', '/')
$packagePrefix = $packageRoot + [IO.Path]::DirectorySeparatorChar

# Every directory renamed below must remain within this package. Reparse points
# are rejected so a pre-existing junction cannot redirect a replacement elsewhere.
function Assert-PackageChild([string]$Candidate) {
    $absolute = [IO.Path]::GetFullPath($Candidate)
    if (-not $absolute.StartsWith($packagePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Percorso esterno al pacchetto: $absolute"
    }
    $cursor = $absolute
    while ($cursor -and $cursor -ne $packageRoot) {
        if (Test-Path -LiteralPath $cursor) {
            $entry = Get-Item -LiteralPath $cursor -Force
            if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Collegamento o junction non supportato durante la build: $cursor"
            }
        }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
    return $absolute
}

function Assert-FrontendStopped([string]$Directory) {
    $targetExe = Join-Path $Directory 'NovaMixUi.exe'
    foreach ($process in @(Get-Process -Name NovaMixUi, electron -ErrorAction SilentlyContinue)) {
        try { $processPath = $process.Path }
        catch { throw 'Impossibile verificare un processo Electron. Chiudi NovaMix e riprova.' }
        if (-not $processPath) {
            throw 'Impossibile verificare un processo Electron. Chiudi NovaMix e riprova.'
        }
        if ([IO.Path]::GetFullPath($processPath).StartsWith($Directory + '\', [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Il pannello NovaMix e ancora aperto. Chiudilo con la X prima di ricostruire la grafica.'
        }
    }
    if (Test-Path -LiteralPath $targetExe) {
        $lockCheck = $null
        try {
            $lockCheck = [IO.File]::Open($targetExe, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        } catch {
            throw 'NovaMixUi.exe e in uso o non scrivibile. Chiudi il pannello e verifica i permessi della cartella.'
        } finally {
            if ($null -ne $lockCheck) { $lockCheck.Dispose() }
        }
    }
}

$frontend = Assert-PackageChild (Join-Path $packageRoot 'frontend')
$sourceDirectory = Assert-PackageChild (Join-Path $packageRoot 'electron-src')
foreach ($sourceName in @('package.json', 'main.cjs', 'preload.cjs')) {
    $sourceFile = Assert-PackageChild (Join-Path $sourceDirectory $sourceName)
    if (-not (Test-Path -LiteralPath $sourceFile -PathType Leaf)) {
        throw "Sorgente dell'interfaccia mancante: $sourceFile"
    }
}
$manifest = Get-Content -LiteralPath (Join-Path $sourceDirectory 'package.json') -Raw | ConvertFrom-Json
if ($manifest.main -ne 'main.cjs' -or $manifest.devDependencies.electron -ne $electronVersion) {
    throw 'package.json e build-ui.ps1 devono indicare la stessa versione Electron fissata.'
}
Assert-FrontendStopped $frontend

if ($ArchivePath) {
    if (-not (Test-Path -LiteralPath $ArchivePath -PathType Leaf)) {
        throw "Archivio Electron non trovato: $ArchivePath"
    }
    $runtimeArchive = (Resolve-Path -LiteralPath $ArchivePath).ProviderPath
} else {
    $cacheDirectory = Assert-PackageChild (Join-Path $packageRoot '.ui-build-cache')
    New-Item -ItemType Directory -Path $cacheDirectory -Force | Out-Null
    $runtimeArchive = Assert-PackageChild (Join-Path $cacheDirectory $archiveName)
    if (-not (Test-Path -LiteralPath $runtimeArchive -PathType Leaf)) {
        $downloadPath = Assert-PackageChild (Join-Path $cacheDirectory ($archiveName + '.' + [Guid]::NewGuid().ToString('N') + '.partial'))
        Write-Host "Download Electron $electronVersion dal repository ufficiale..."
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
        try { Invoke-WebRequest -Uri $archiveUrl -OutFile $downloadPath -UseBasicParsing }
        catch { throw "Download Electron fallito. Il runtime esistente e intatto. Dettaglio: $($_.Exception.Message)" }
        if ((Get-FileHash -LiteralPath $downloadPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedSha256) {
            throw "Checksum Electron errato: il download non verra usato. File: $downloadPath"
        }
        Move-Item -LiteralPath $downloadPath -Destination $runtimeArchive
    }
}
if ((Get-FileHash -LiteralPath $runtimeArchive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedSha256) {
    throw "Checksum SHA256 Electron errato. Archivio non utilizzato: $runtimeArchive"
}

$stageDirectory = Assert-PackageChild (Join-Path $packageRoot ('.ui-build-stage-' + [Guid]::NewGuid().ToString('N')))
New-Item -ItemType Directory -Path $stageDirectory | Out-Null
$stagedRuntime = Assert-PackageChild (Join-Path $stageDirectory 'frontend')
Write-Host "Checksum verificato. Preparazione Electron $electronVersion..."
try {
    Expand-Archive -LiteralPath $runtimeArchive -DestinationPath $stagedRuntime
    foreach ($required in @('electron.exe', 'electron_wer.dll', 'LICENSE', 'LICENSES.chromium.html', 'version')) {
        if (-not (Test-Path -LiteralPath (Join-Path $stagedRuntime $required) -PathType Leaf)) {
            throw "File Electron mancante nell'archivio: $required"
        }
    }
    if ((Get-Content -LiteralPath (Join-Path $stagedRuntime 'version') -Raw).Trim() -ne $electronVersion) {
        throw 'Versione del runtime estratto non corrispondente.'
    }
    Rename-Item -LiteralPath (Join-Path $stagedRuntime 'electron.exe') -NewName 'NovaMixUi.exe'
    # Electron 45 uses an executable-matched DLL name for fail-fast crash capture.
    Rename-Item -LiteralPath (Join-Path $stagedRuntime 'electron_wer.dll') -NewName 'NovaMixUi_wer.dll'
    if (-not (Test-Path -LiteralPath (Join-Path $stagedRuntime 'NovaMixUi_wer.dll') -PathType Leaf) -or
        (Test-Path -LiteralPath (Join-Path $stagedRuntime 'electron_wer.dll'))) {
        throw 'Rinomina del modulo WER Electron non completata.'
    }
    $defaultApp = Assert-PackageChild (Join-Path $stagedRuntime 'resources\default_app.asar')
    if (Test-Path -LiteralPath $defaultApp -PathType Leaf) { Remove-Item -LiteralPath $defaultApp }
    $appDirectory = Assert-PackageChild (Join-Path $stagedRuntime 'resources\app')
    New-Item -ItemType Directory -Path $appDirectory -Force | Out-Null
    foreach ($sourceName in @('package.json', 'main.cjs', 'preload.cjs')) {
        Copy-Item -LiteralPath (Join-Path $sourceDirectory $sourceName) -Destination (Join-Path $appDirectory $sourceName)
    }

    # Check again after the download/extraction: the user may have reopened the UI.
    Assert-FrontendStopped $frontend
    $backupDirectory = $null
    if (Test-Path -LiteralPath $frontend) {
        $backupDirectory = Assert-PackageChild (Join-Path $packageRoot ('frontend.previous-' + [Guid]::NewGuid().ToString('N')))
        $null = Assert-PackageChild $frontend
        Move-Item -LiteralPath $frontend -Destination $backupDirectory
    }
    try {
        $null = Assert-PackageChild $stagedRuntime
        $null = Assert-PackageChild $frontend
        Move-Item -LiteralPath $stagedRuntime -Destination $frontend
    } catch {
        if ($backupDirectory -and -not (Test-Path -LiteralPath $frontend)) {
            $null = Assert-PackageChild $backupDirectory
            Move-Item -LiteralPath $backupDirectory -Destination $frontend
        }
        throw
    }
    # The staging directory is now empty; no recursive deletion is needed.
    Remove-Item -LiteralPath $stageDirectory
    Write-Host "Interfaccia pronta: $(Join-Path $frontend 'NovaMixUi.exe')"
    if ($backupDirectory) { Write-Host "Runtime precedente conservato in: $backupDirectory" }
} catch {
    throw "Build dell'interfaccia non completata. I file di preparazione restano in $stageDirectory. Dettaglio: $($_.Exception.Message)"
}
