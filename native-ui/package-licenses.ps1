param([string]$CargoPath = 'cargo', [Parameter(Mandatory=$true)][string]$Destination)
$ErrorActionPreference = 'Stop'
$manifest = Join-Path $PSScriptRoot 'Cargo.toml'
$metadataText = & $CargoPath metadata --locked --format-version 1 --manifest-path $manifest --filter-platform x86_64-pc-windows-msvc
if ($LASTEXITCODE) { throw 'Could not read locked Rust dependency metadata.' }
$metadata = $metadataText | ConvertFrom-Json
$tree = & $CargoPath tree --locked --manifest-path $manifest --target x86_64-pc-windows-msvc --edges normal --prefix none --format '{p}'
if ($LASTEXITCODE) { throw 'Could not read Rust runtime dependencies.' }
$runtime = @{}
foreach ($line in $tree) { $runtime[($line -replace ' \(.*$', '')] = $true }
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$index = @('NovaMix native interface - third-party licenses', '', 'Only Windows runtime dependencies are listed. The original license texts follow in this directory.', '')
foreach ($package in ($metadata.packages | Sort-Object name,version)) {
    $key = $package.name + ' v' + $package.version
    if ($package.name -eq 'novamix-native-ui' -or !$runtime.ContainsKey($key)) { continue }
    $root = Split-Path -Parent $package.manifest_path
    $texts = @(Get-ChildItem -LiteralPath $root -File | Where-Object { $_.Name -match '^(LICENSE|LICENCE|COPYING|NOTICE)([-._].*)?$' })
    if ($package.license_file) {
        $extra = Join-Path $root $package.license_file
        if (Test-Path -LiteralPath $extra -PathType Leaf) { $texts += Get-Item -LiteralPath $extra }
    }
    if (!$texts.Count) {
        # Some crates keep the full text in an explicit licenses subdirectory.
        $subdirectory = Join-Path $root 'licenses'
        if (Test-Path -LiteralPath $subdirectory -PathType Container) { $texts = @(Get-ChildItem -LiteralPath $subdirectory -File) }
    }
    if (!$texts.Count -and $package.repository -eq 'https://github.com/iced-rs/iced' -and $package.license -eq 'MIT') {
        $iced = $metadata.packages | Where-Object { $_.name -eq 'iced' } | Select-Object -First 1
        $texts = @(Get-Item -LiteralPath (Join-Path (Split-Path -Parent $iced.manifest_path) 'LICENSE'))
    }
    if (!$texts.Count -and $package.name -eq 'clipboard-win' -and $package.license -eq 'BSL-1.0') {
        $texts = @(Get-Item -LiteralPath (Join-Path $PSScriptRoot 'licenses\BSL-1.0.txt'))
    }
    if (!$texts.Count) { throw ('Missing upstream license text: ' + $key) }
    $filename = $package.name + '-' + $package.version + '.txt'
    $content = @($key, ('SPDX: ' + $package.license), ('Source: ' + $package.repository), '')
    foreach ($file in ($texts | Sort-Object FullName -Unique)) {
        $content += @('----- ' + $file.Name + ' -----', [IO.File]::ReadAllText($file.FullName), '')
    }
    [IO.File]::WriteAllLines((Join-Path $Destination $filename), $content, [Text.UTF8Encoding]::new($false))
    $index += ($key + ' | ' + $package.license + ' | ' + $filename)
}
[IO.File]::WriteAllLines((Join-Path $Destination 'INDEX.txt'), $index, [Text.UTF8Encoding]::new($false))
Write-Output ('Packaged license texts for ' + ($index.Count - 4) + ' Windows runtime dependencies.')
