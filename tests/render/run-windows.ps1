[CmdletBinding()]
param(
    [string]$StagePath = 'build/windows/release/portable-stage',
    [string]$BuildDirectory = 'build/render-tests',
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$state = Get-Content (Join-Path $sourceRoot 'build/windows-toolchain.json') -Raw | ConvertFrom-Json
$manifest = Get-Content (Join-Path $sourceRoot 'build-manifest.json') -Raw | ConvertFrom-Json
$testBuild = if ([IO.Path]::IsPathRooted($BuildDirectory)) {
    [IO.Path]::GetFullPath($BuildDirectory)
} else { [IO.Path]::GetFullPath((Join-Path $sourceRoot $BuildDirectory)) }
if (-not $testBuild.StartsWith($sourceRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'BuildDirectory must stay within this checkout.'
}
$runtimeStage = if ([IO.Path]::IsPathRooted($StagePath)) {
    [IO.Path]::GetFullPath($StagePath)
} else { [IO.Path]::GetFullPath((Join-Path $sourceRoot $StagePath)) }
if (-not (Test-Path -LiteralPath (Join-Path $runtimeStage 'opengl32sw.dll'))) { throw 'A complete locked portable-stage is required.' }
foreach ($dependency in @(
    @{Name='libmpv-2.dll'; Reference=$state.mpvRuntime},
    @{Name='Qt6Core.dll'; Reference=(Join-Path $state.qtRoot 'bin/Qt6Core.dll')},
    @{Name='opengl32sw.dll'; Reference=(Join-Path $state.qtRoot 'bin/opengl32sw.dll')},
    @{Name='vulkan-1.dll'; Reference=$state.vulkanRuntime}
)) {
    $actual = (Get-FileHash -LiteralPath (Join-Path $runtimeStage $dependency.Name) -Algorithm SHA256).Hash
    $locked = (Get-FileHash -LiteralPath $dependency.Reference -Algorithm SHA256).Hash
    if ($actual -ne $locked) { throw "Stage dependency differs from the locked bootstrap: $($dependency.Name)" }
}
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
if (-not $SkipBuild) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $installation = & $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installation) { throw 'MSVC2022 C++ Build Tools missing' }
    Import-Module (Join-Path $installation 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $installation -SkipAutomaticLocation -Arch amd64 -HostArch amd64 `
        -DevCmdArguments "-vcvars_ver=$($manifest.windows.msvcToolsVersion) -winsdk=$($manifest.windows.windowsSdkVersion)" | Out-Null
    if ($env:VCToolsVersion.TrimEnd('\') -ne $manifest.windows.msvcToolsVersion -or
        $env:WindowsSDKVersion.TrimEnd('\') -ne $manifest.windows.windowsSdkVersion) {
        throw 'MSVC/SDK differ from build-manifest.json.'
    }
    Invoke-Checked $state.cmakeExe @('-S', $PSScriptRoot, '-B', $testBuild, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=RelWithDebInfo', "-DCMAKE_MAKE_PROGRAM=$($state.ninjaExe)",
        "-DCMAKE_PREFIX_PATH=$($state.qtRoot)", "-DMPV_INCLUDE_DIR=$($state.mpvRoot)/include", "-DMPV_LIBRARY=$($state.mpvLibrary)")
    Invoke-Checked $state.cmakeExe @('--build', $testBuild, '--parallel', '2')
}
# Only the standalone test executable is launched. Stage the pinned runtime,
# without the desktop executable, native plugins, profiles or user data.
Get-ChildItem -LiteralPath $runtimeStage -File -Filter '*.dll' | Copy-Item -Destination $testBuild -Force
New-Item -ItemType Directory -Force -Path (Join-Path $testBuild 'platforms') | Out-Null
Copy-Item -LiteralPath (Join-Path $runtimeStage 'platforms/qwindows.dll') -Destination (Join-Path $testBuild 'platforms') -Force
foreach ($module in @('QtQuick', 'QtQml')) {
    $destination = Join-Path $testBuild "qml/$module"
    New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
    Copy-Item -LiteralPath (Join-Path $runtimeStage "qml/$module") -Destination (Join-Path $testBuild 'qml') -Recurse -Force
}
[IO.File]::WriteAllText((Join-Path $testBuild 'qt.conf'), "[Paths]`nPrefix=.`nPlugins=.`nQmlImports=qml`nQml2Imports=qml`n")
Invoke-Checked 'python' @((Join-Path $sourceRoot 'scripts/validate-windows-runtime.py'), $testBuild,
    '--report', (Join-Path $testBuild 'runtime-imports.json'))
$ctest = Join-Path (Split-Path $state.cmakeExe) 'ctest.exe'
$originalPath = $env:PATH
try {
    # System32 remains for genuine Windows components. The static import guard
    # rejects third-party DLLs there; SDK/driver/toolchain PATH entries are gone.
    $env:PATH = $testBuild + ';' + (Join-Path $env:SystemRoot 'System32') + ';' + $env:SystemRoot
    Invoke-Checked $ctest @('--test-dir', $testBuild, '--output-on-failure')
} finally { $env:PATH = $originalPath }
