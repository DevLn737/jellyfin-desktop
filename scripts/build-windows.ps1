[CmdletBinding()]
param([switch]$Diagnostic, [switch]$IsolatedPortable, [switch]$SkipInstallerTools, [string]$BuildDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$sourceRoot = Split-Path $PSScriptRoot -Parent
if (-not $BuildDirectory) {
    $BuildDirectory = if ($Diagnostic) { 'build/windows/diagnostic' } else { 'build/windows/release' }
}
$nativeBuildDirectory = [IO.Path]::GetFullPath((Join-Path $sourceRoot $BuildDirectory))
if (-not $nativeBuildDirectory.StartsWith($sourceRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'BuildDirectory must be a subdirectory of this checkout.'
}
. (Join-Path $PSScriptRoot 'bootstrap-windows.ps1') -SkipInstallerTools:($SkipInstallerTools -or $Diagnostic)
$state = Get-Content (Join-Path $sourceRoot 'build/windows-toolchain.json') -Raw | ConvertFrom-Json
$assetBaseName = "JellyfinMediaPlayer-$($state.appVersion)-windows-x64"
$cmake = $state.cmakeExe
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
function Get-CurrentWindowsAssetPaths([string]$BuildDirectory, [string]$AssetBaseName, [bool]$Diagnostic, [bool]$SkipInstallerTools) {
    [string[]]$names = if ($Diagnostic) {
        @("$AssetBaseName-diagnostic-tracy-portable.zip", "$AssetBaseName-diagnostic-symbols.zip")
    } else {
        @("$AssetBaseName-portable.zip")
    }
    if (-not $Diagnostic -and -not $SkipInstallerTools) { $names += "$AssetBaseName.exe" }
    $paths = @()
    foreach ($name in $names) {
        $path = Join-Path $BuildDirectory $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Missing current-version build artifact: $name"
        }
        $paths += $path
    }
    return $paths
}
Push-Location $sourceRoot
try {
    Invoke-Checked 'python' @('scripts/validate-build-manifest.py')
    Invoke-Checked 'python' @('scripts/test-release-validation.py')
    Invoke-Checked 'python' @('scripts/test-windows-runtime-validation.py')
    & (Join-Path $PSScriptRoot 'test-build-assets.ps1')
    & (Join-Path $PSScriptRoot 'test-diagnostic-symbols-validation.ps1')
    foreach ($test in @('test-external-audio.js', 'test-playback-values.js', 'test-player-cancellation.js', 'test-video-ui-background.js', 'test-updater.js', 'test-diagnostics-plugin.js', 'test-syncplay-native.js', 'test-find-webclient.js')) {
        Invoke-Checked 'node' @("scripts/$test")
    }
    Invoke-Checked 'node' @('scripts/live-playback-probe.mjs', '--self-test')
    & (Join-Path $PSScriptRoot 'test-live-playback-composition.ps1')
    $configure = @('-S', $sourceRoot, '-B', $nativeBuildDirectory, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=RelWithDebInfo', "-DCMAKE_MAKE_PROGRAM=$($state.ninjaExe)",
        "-DCMAKE_INSTALL_PREFIX=$nativeBuildDirectory/output", "-DQTROOT=$($state.qtRoot)",
        "-DMPV_INCLUDE_DIR=$($state.mpvRoot)/include", "-DMPV_LIBRARY=$($state.mpvLibrary)",
        "-DMPV_RUNTIME=$($state.mpvRuntime)", "-DJMP_VCRUNTIME_DIR=$($state.runtimeDirectory)",
        "-DJMP_VULKAN_RUNTIME=$($state.vulkanRuntime)", "-DJMP_VULKAN_LICENSE=$($state.vulkanLicense)",
        "-DVCREDIST_EXE=$($state.vcRedistExe)", "-DWIX_EXECUTABLE=$($state.wixExe)",
        "-DJMP_TRACY_ARCHIVE=$($state.tracyArchive)",
        "-DWIX_UTIL_EXTENSION=$($state.wixUtilExtension)", "-DWIX_BAL_EXTENSION=$($state.wixBalExtension)",
        '-DWIX_ACCEPT_EULA=ON', "-DJMP_DIAGNOSTIC=$($Diagnostic.IsPresent)", "-DJMP_PORTABLE_PROFILE=$($IsolatedPortable.IsPresent)")
    Invoke-Checked $cmake $configure
    Invoke-Checked $cmake @('--build', $nativeBuildDirectory, '--parallel', '2')
    foreach ($test in @('discord', 'diagnostics', 'playback', 'application', 'ui', 'logging')) {
        $testBuild = Join-Path $nativeBuildDirectory "tests/$test"
        Invoke-Checked $cmake @('-S', "tests/$test", '-B', $testBuild, '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=RelWithDebInfo', "-DCMAKE_MAKE_PROGRAM=$($state.ninjaExe)", "-DCMAKE_PREFIX_PATH=$($state.qtRoot)")
        Invoke-Checked $cmake @('--build', $testBuild, '--parallel', '2')
        Invoke-Checked $ctest @('--test-dir', $testBuild, '--output-on-failure')
    }
    Invoke-Checked $cmake @('--build', $nativeBuildDirectory, '--target', 'windows_zip')
    Invoke-Checked 'python' @('scripts/validate-windows-runtime.py', (Join-Path $nativeBuildDirectory 'portable-stage'),
        '--report', (Join-Path $nativeBuildDirectory 'windows-runtime-imports.json'))
    if (-not $Diagnostic) {
        & (Join-Path $sourceRoot 'tests/render/run-windows.ps1') `
            -StagePath (Join-Path $nativeBuildDirectory 'portable-stage') `
            -BuildDirectory (Join-Path $nativeBuildDirectory 'tests/render')
    }
    if ($Diagnostic) {
        $symbols = Join-Path $nativeBuildDirectory "$assetBaseName-diagnostic-symbols.zip"
        & (Join-Path $PSScriptRoot 'test-diagnostic-symbols.ps1') -ZipPath $symbols
    }
    if (-not $Diagnostic -and -not $SkipInstallerTools) {
        Invoke-Checked $cmake @('--build', $nativeBuildDirectory, '--target', 'windows_package')
        $zip = Join-Path $nativeBuildDirectory "$assetBaseName-portable.zip"
        & (Join-Path $PSScriptRoot 'test-windows-packaging.ps1') -MsiPath (Join-Path $nativeBuildDirectory 'JellyfinMediaPlayer.msi') -PortableZip $zip -IsolatedPortable:$IsolatedPortable
    }
    $currentAssetPaths = @(Get-CurrentWindowsAssetPaths $nativeBuildDirectory $assetBaseName $Diagnostic.IsPresent $SkipInstallerTools.IsPresent)
    $assetDirectory = Join-Path $nativeBuildDirectory 'assets'
    New-Item -ItemType Directory -Force -Path $assetDirectory | Out-Null
    Copy-Item -LiteralPath $currentAssetPaths -Destination $assetDirectory
    if (-not $Diagnostic) { Copy-Item -LiteralPath (Join-Path $sourceRoot 'build-manifest.json') -Destination $assetDirectory }
    Write-Host "Build assets ready: $assetDirectory"
} finally { Pop-Location }
