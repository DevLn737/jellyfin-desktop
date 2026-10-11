[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
$sourceRoot = Split-Path $PSScriptRoot -Parent
$manifestPath = Join-Path $sourceRoot 'build-manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$expectedCommit = (& git -C $sourceRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify the regression source commit' }
$checkoutStatus = @(& git -C $sourceRoot status --porcelain)
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify the regression checkout state' }
if ($checkoutStatus.Count) { $expectedCommit += '-dirty' }
$identity = @{
    appVersion = $manifest.appVersion
    packageVersion = $manifest.packageVersion
    qtVersion = $manifest.qt.version
    buildCommit = $expectedCommit
    buildManifestSha256 = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
    diagnostic = $true
}
$temporaryRoot = [IO.Path]::GetFullPath((Join-Path $sourceRoot 'build/diagnostic-symbols-tests'))
$temporaryDirectory = [IO.Path]::GetFullPath((Join-Path $temporaryRoot ([Guid]::NewGuid().ToString('N'))))
New-Item -ItemType Directory -Path $temporaryDirectory -Force | Out-Null
$passed = 0

function Test-SymbolArchive([string]$Name, [hashtable]$Changes, [bool]$ShouldPass, [bool]$DuplicateMetadata = $false) {
    $metadata = $identity.Clone()
    foreach ($key in $Changes.Keys) { $metadata[$key] = $Changes[$key] }
    $zipPath = Join-Path $temporaryDirectory "$Name.zip"
    $archive = [IO.Compression.ZipFile]::Open($zipPath, [IO.Compression.ZipArchiveMode]::Create)
    try {
        $pdb = $archive.CreateEntry('JellyfinMediaPlayer.pdb').Open()
        try {
            $bytes = New-Object byte[] 2048
            $header = [Text.Encoding]::ASCII.GetBytes("Microsoft C/C++ MSF 7.00`r`n")
            [Array]::Copy($header, $bytes, $header.Length)
            $pdb.Write($bytes, 0, $bytes.Length)
        } finally { $pdb.Dispose() }
        $copies = if ($DuplicateMetadata) { 2 } else { 1 }
        for ($copy = 0; $copy -lt $copies; $copy++) {
            $writer = New-Object IO.StreamWriter ($archive.CreateEntry('symbols-build.json').Open())
            try { $writer.Write(($metadata | ConvertTo-Json -Compress)) } finally { $writer.Dispose() }
        }
    } finally { $archive.Dispose() }
    $failure = $null
    try { & (Join-Path $PSScriptRoot 'test-diagnostic-symbols.ps1') -ZipPath $zipPath }
    catch { $failure = $_ }
    if ($ShouldPass -and $failure) { throw "$Name should match the current identity: $failure" }
    if (-not $ShouldPass -and -not $failure) { throw "$Name should reject an invalid diagnostic identity" }
    $script:passed++
}

try {
    Test-SymbolArchive 'matching-current-identity' @{} $true
    Test-SymbolArchive 'stale-commit' @{ buildCommit = ('0' * 40) } $false
    Test-SymbolArchive 'wrong-manifest' @{ buildManifestSha256 = ('0' * 64) } $false
    $wrongState = if ($expectedCommit.EndsWith('-dirty')) { $expectedCommit.Substring(0, 40) } else { "$expectedCommit-dirty" }
    Test-SymbolArchive 'wrong-checkout-state' @{ buildCommit = $wrongState } $false
    Test-SymbolArchive 'wrong-package-version' @{ packageVersion = '255.255.65535' } $false
    Test-SymbolArchive 'missing-package-version' @{ packageVersion = $null } $false
    Test-SymbolArchive 'duplicate-metadata' @{} $false $true
    Write-Host "Diagnostic symbol identity regressions passed: $passed"
} finally {
    # Remove only this invocation's specifically created ignored directory.
    $resolvedDirectory = (Get-Item -LiteralPath $temporaryDirectory).FullName
    if ($resolvedDirectory -cne $temporaryDirectory -or
        -not $resolvedDirectory.StartsWith($temporaryRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path $resolvedDirectory -Leaf) -notmatch '^[a-f0-9]{32}$') {
        throw 'Refusing to remove an unexpected regression directory'
    }
    Remove-Item -LiteralPath $resolvedDirectory -Recurse -Force
}
