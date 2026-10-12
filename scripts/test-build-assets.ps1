[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$sourceRoot = Split-Path $PSScriptRoot -Parent
$parseTokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot 'build-windows.ps1'), [ref]$parseTokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw 'Build script does not parse' }
$definition = $ast.Find({ param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Get-CurrentWindowsAssetPaths'
}, $true)
if (-not $definition) { throw 'Current-version artifact selection helper is missing' }
# Load only the actual pure helper, never the bootstrap/build pipeline.
. ([scriptblock]::Create($definition.Extent.Text))
$copyCommands = @($ast.FindAll({ param($node)
    $node -is [System.Management.Automation.Language.CommandAst] -and
    $node.GetCommandName() -eq 'Copy-Item' -and
    $node.Extent.Text.Contains('-LiteralPath $currentAssetPaths')
}, $true))
if ($copyCommands.Count -ne 1) { throw 'Asset publishing must consume the exact selected paths once' }
$copyCurrentAssets = [scriptblock]::Create($copyCommands[0].Extent.Text)
$temporaryRoot = [IO.Path]::GetFullPath((Join-Path $sourceRoot 'build/build-assets-tests'))
$temporaryDirectory = [IO.Path]::GetFullPath((Join-Path $temporaryRoot ([Guid]::NewGuid().ToString('N'))))
New-Item -ItemType Directory -Path $temporaryDirectory -Force | Out-Null
$current = 'JellyfinMediaPlayer-2.0.0-rc.2-windows-x64'
$legacy = 'JellyfinMediaPlayer-2.0.0-rc.1-windows-x64'
$suffixes = @('-portable.zip', '.exe', '-diagnostic-tracy-portable.zip', '-diagnostic-symbols.zip')
function Assert-EqualNames([string[]]$Actual, [string[]]$Expected) {
    if (@(Compare-Object ($Actual | Sort-Object) ($Expected | Sort-Object)).Count) {
        throw 'Selected artifact set differs from the current-version expected outputs'
    }
}
function Assert-MissingCurrent([string]$Directory, [bool]$Diagnostic, [bool]$SkipInstallerTools) {
    $failed = $false
    try { Get-CurrentWindowsAssetPaths $Directory $current $Diagnostic $SkipInstallerTools | Out-Null }
    catch { $failed = $_.Exception.Message.StartsWith('Missing current-version build artifact:') }
    if (-not $failed) { throw 'Missing current output must fail even when old artifacts are cached' }
}
try {
    foreach ($base in @($legacy, $current)) {
        foreach ($suffix in $suffixes) {
            [IO.File]::WriteAllText((Join-Path $temporaryDirectory "$base$suffix"), $base)
        }
    }
    [IO.File]::WriteAllText((Join-Path $temporaryDirectory "$current-unrelated.zip"), 'must not publish')
    foreach ($case in @(
        @{ Diagnostic = $false; SkipInstallerTools = $false; Expected = @("$current-portable.zip", "$current.exe") },
        @{ Diagnostic = $false; SkipInstallerTools = $true; Expected = @("$current-portable.zip") },
        @{ Diagnostic = $true; SkipInstallerTools = $true; Expected = @("$current-diagnostic-tracy-portable.zip", "$current-diagnostic-symbols.zip") }
    )) {
        $currentAssetPaths = @(Get-CurrentWindowsAssetPaths $temporaryDirectory $current $case.Diagnostic $case.SkipInstallerTools)
        Assert-EqualNames @($currentAssetPaths | ForEach-Object { Split-Path $_ -Leaf }) $case.Expected
        $assetDirectory = Join-Path $temporaryDirectory ([Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $assetDirectory | Out-Null
        $kept = Join-Path $assetDirectory "$legacy-portable.zip"
        [IO.File]::WriteAllText($kept, 'retained old artifact')
        # Execute the real pipeline's Copy-Item command against this fixture.
        & $copyCurrentAssets
        Assert-EqualNames @((Get-ChildItem -LiteralPath $assetDirectory -File).Name) @($case.Expected + "$legacy-portable.zip")
        foreach ($name in $case.Expected) {
            if ([IO.File]::ReadAllText((Join-Path $assetDirectory $name)) -ne $current) { throw 'Wrong-version bytes copied' }
        }
        if ([IO.File]::ReadAllText($kept) -ne 'retained old artifact') { throw 'Existing historical asset was changed' }
    }
    $oldOnly = Join-Path $temporaryDirectory 'old-only'
    New-Item -ItemType Directory -Path $oldOnly | Out-Null
    foreach ($suffix in $suffixes) { [IO.File]::WriteAllText((Join-Path $oldOnly "$legacy$suffix"), $legacy) }
    Assert-MissingCurrent $oldOnly $false $false
    Assert-MissingCurrent $oldOnly $true $true
    Assert-MissingCurrent $oldOnly $false $true
    # A present current portable must not hide missing current installer/symbols.
    [IO.File]::WriteAllText((Join-Path $oldOnly "$current-portable.zip"), $current)
    Assert-MissingCurrent $oldOnly $false $false
    [IO.File]::WriteAllText((Join-Path $oldOnly "$current-diagnostic-tracy-portable.zip"), $current)
    Assert-MissingCurrent $oldOnly $true $true
    Write-Host 'Current-version artifact selection, cached old RC, exact copy and missing-output regressions passed'
} finally {
    $resolvedDirectory = (Get-Item -LiteralPath $temporaryDirectory).FullName
    if ($resolvedDirectory -cne $temporaryDirectory -or
        -not $resolvedDirectory.StartsWith($temporaryRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path $resolvedDirectory -Leaf) -notmatch '^[a-f0-9]{32}$') {
        throw 'Refusing to remove an unexpected artifact fixture directory'
    }
    Remove-Item -LiteralPath $resolvedDirectory -Recurse -Force
}
