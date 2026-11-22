# This check is safe to run alongside real NovaMix: the engine uses --preview,
# synthetic devices, a separate mutex, no HID/audio workers and no real tray icons.
param([Parameter(Mandatory=$true)][string]$AppDirectory)
$ErrorActionPreference = 'Stop'
$folder = (Resolve-Path -LiteralPath $AppDirectory).ProviderPath
$enginePath = Join-Path $folder 'NovaMix.exe'
$uiPath = Join-Path $folder 'frontend\NovaMixUi.exe'
$resultPath = Join-Path $folder 'test-iced-integration.txt'
foreach ($flag in @('--self-test','--api-self-test','--desktop-self-test','--stream-self-test')) {
    $test = Start-Process -FilePath $enginePath -ArgumentList $flag -WorkingDirectory $folder -PassThru -WindowStyle Hidden
    if (!$test.WaitForExit(30000)) { Stop-Process -Id $test.Id; throw ('Engine test timed out: ' + $flag) }
    if ($test.ExitCode) { throw ('Engine test failed: ' + $flag) }
}
$urlPath = Join-Path $folder 'preview-url.txt'
if (Test-Path -LiteralPath $urlPath) { Remove-Item -LiteralPath $urlPath }
$engine = Start-Process -FilePath $enginePath -ArgumentList @('--preview','--background') -WorkingDirectory $folder -PassThru -WindowStyle Hidden
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while (!(Test-Path -LiteralPath $urlPath)) {
        if ($engine.HasExited -or [DateTime]::UtcNow -gt $deadline) { throw 'Preview engine failed to start.' }
        Start-Sleep -Milliseconds 100
    }
    # Never print this URL: it contains the process-local session token.
    $address = [IO.File]::ReadAllText($urlPath).Trim()
    $test = Start-Process -FilePath $uiPath -ArgumentList @('--verify-engine','--parent',$engine.Id,'--url',$address,'--result',('"'+$resultPath+'"')) -WorkingDirectory $folder -PassThru -WindowStyle Hidden
    if (!$test.WaitForExit(30000)) { Stop-Process -Id $test.Id; throw 'Native client integration timed out.' }
    if ($test.ExitCode) { throw ('Native client integration failed. See ' + [IO.Path]::GetFileName($resultPath)) }
    Get-Content -LiteralPath $resultPath
    # Exercise the production launcher as well, including its parent-process watcher.
    $launcherFolder = Join-Path $folder 'launcher-check'
    New-Item -ItemType Directory -Path (Join-Path $launcherFolder 'frontend') -Force | Out-Null
    Copy-Item -LiteralPath $enginePath -Destination (Join-Path $launcherFolder 'NovaMix.exe') -Force
    Copy-Item -LiteralPath $uiPath -Destination (Join-Path $launcherFolder 'frontend\NovaMixUi.exe') -Force
    $launcher = Start-Process -FilePath (Join-Path $launcherFolder 'NovaMix.exe') -ArgumentList '--preview' -WorkingDirectory $launcherFolder -PassThru -WindowStyle Hidden
    try {
        Start-Sleep -Milliseconds 1800
        $native = @(Get-Process -Name NovaMixUi -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $launcherFolder 'frontend\NovaMixUi.exe') })
        if ($native.Count -ne 1) { throw 'Production launcher did not open exactly one native panel.' }
        $startCpu = $native[0].TotalProcessorTime.TotalMilliseconds
        Start-Sleep -Seconds 3
        $native[0].Refresh()
        $measurement = [ordered]@{
            frontendBytes = (Get-Item -LiteralPath $uiPath).Length
            workingSetMiB = [Math]::Round($native[0].WorkingSet64 / 1MB, 1)
            privateMiB = [Math]::Round($native[0].PrivateMemorySize64 / 1MB, 1)
            idleCpuMillisecondsOver3Seconds = [Math]::Round($native[0].TotalProcessorTime.TotalMilliseconds-$startCpu, 1)
        }
        $measurement | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $folder 'test-iced-resources.json') -Encoding UTF8
        $measurement | ConvertTo-Json
    } finally {
        if (!$launcher.HasExited) { Stop-Process -Id $launcher.Id; $launcher.WaitForExit(3000) | Out-Null }
    }
    if (!$native[0].WaitForExit(5000)) { Stop-Process -Id $native[0].Id; throw 'Native panel did not exit after its engine closed.' }
    Write-Output 'PASS production launcher, single native panel and parent-exit lifecycle.'
} finally {
    if (!$engine.HasExited) { Stop-Process -Id $engine.Id; $engine.WaitForExit(3000) | Out-Null }
}
