[CmdletBinding()]
param([Parameter(Mandatory)][string]$ZipPath)
$ErrorActionPreference = 'Stop'
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
    $metadata = $archive.Entries | Where-Object { $_.FullName.TrimStart('./') -eq 'symbols-build.json' }
    if (-not $metadata) { throw 'Diagnostic symbols have no build identity' }
    $reader = New-Object IO.StreamReader ($metadata.Open())
    try { $info = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
    $manifest = Get-Content (Join-Path (Split-Path $PSScriptRoot -Parent) 'build-manifest.json') -Raw | ConvertFrom-Json
    if (-not $info.diagnostic -or $info.appVersion -ne $manifest.appVersion -or $info.qtVersion -ne $manifest.qt.version) {
        throw 'Diagnostic PDB belongs to a different application version'
    }
    if ($info.buildCommit -notmatch '^[a-f0-9]{40}(-dirty)?$') { throw 'Diagnostic symbol source commit is missing' }
} finally { $archive.Dispose() }
Write-Host 'Diagnostic symbols and build identity passed'
