[CmdletBinding()]
param([Parameter(Mandatory)][string]$ZipPath)
$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path $PSScriptRoot -Parent
$manifestPath = Join-Path $sourceRoot 'build-manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$manifestSha = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
$expectedCommit = (& git -C $sourceRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $expectedCommit -notmatch '^[a-f0-9]{40}$') { throw 'Cannot identify the current source commit' }
$checkoutStatus = @(& git -C $sourceRoot status --porcelain)
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify the current checkout state' }
if ($checkoutStatus.Count) { $expectedCommit += '-dirty' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($ZipPath))
try {
    $pdb = @($archive.Entries | Where-Object { $_.FullName.TrimStart('./') -eq 'JellyfinMediaPlayer.pdb' })
    if ($pdb.Count -ne 1 -or $pdb[0].Length -lt 1024) { throw 'Diagnostic application PDB is missing or empty' }
    $stream = $pdb[0].Open()
    try {
        $header = New-Object byte[] 24
        $null = $stream.Read($header, 0, $header.Length)
        if ([Text.Encoding]::ASCII.GetString($header) -notmatch '^Microsoft C/C\+\+ MSF') { throw 'Diagnostic symbol asset is not a Windows PDB' }
    } finally { $stream.Dispose() }
    $metadata = @($archive.Entries | Where-Object { $_.FullName.TrimStart('./') -eq 'symbols-build.json' })
    if ($metadata.Count -ne 1) { throw 'Diagnostic symbols require exactly one build identity' }
    $reader = New-Object IO.StreamReader ($metadata[0].Open())
    try { $info = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
    if ($info.diagnostic -isnot [bool] -or -not $info.diagnostic -or
        $info.appVersion -cne $manifest.appVersion -or $info.qtVersion -cne $manifest.qt.version) {
        throw 'Diagnostic PDB belongs to a different application version'
    }
    if ($info.buildCommit -cne $expectedCommit) { throw 'Diagnostic symbols do not match the current source commit and checkout state' }
    if ($info.buildManifestSha256 -cne $manifestSha) { throw 'Diagnostic symbols do not match the current build manifest SHA256' }
    $packageVersion = $info.PSObject.Properties['packageVersion']
    if (-not $packageVersion -or $packageVersion.Value -cne $manifest.packageVersion) { throw 'Diagnostic symbols do not match the current package version' }
} finally { $archive.Dispose() }
Write-Host 'Diagnostic symbols and build identity passed'
