[CmdletBinding()]
param(
    [string]$StagePath = 'build/windows/qt6113/release/portable-stage',
    [string]$BuildDirectory = 'build/threaded-renderer-tests',
    [ValidateRange(30,45)][int]$DurationSeconds = 40,
    [switch]$GuiSync,
    [switch]$CaptureWindow,
    [switch]$HiddenStartup,
    [switch]$BuildOnly,
    [string]$RendererSourceDirectory,
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$state = Get-Content -LiteralPath (Join-Path $sourceRoot 'build/windows-toolchain.json') -Raw | ConvertFrom-Json
$manifest = Get-Content -LiteralPath (Join-Path $sourceRoot 'build-manifest.json') -Raw | ConvertFrom-Json
function Resolve-WorkspacePath([string]$Value) {
    $resolved = if ([IO.Path]::IsPathRooted($Value)) { [IO.Path]::GetFullPath($Value) }
                else { [IO.Path]::GetFullPath((Join-Path $sourceRoot $Value)) }
    if (-not $resolved.StartsWith($sourceRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Test paths must remain in this checkout.'
    }
    return $resolved
}
$testBuild = Resolve-WorkspacePath $BuildDirectory
$stage = Resolve-WorkspacePath $StagePath
foreach ($dependency in @(
    @{ Name='libmpv-2.dll'; Reference=$state.mpvRuntime },
    @{ Name='Qt6Core.dll'; Reference=(Join-Path $state.qtRoot 'bin/Qt6Core.dll') },
    @{ Name='vulkan-1.dll'; Reference=$state.vulkanRuntime }
)) {
    if ((Get-FileHash -LiteralPath (Join-Path $stage $dependency.Name)).Hash -ne
        (Get-FileHash -LiteralPath $dependency.Reference).Hash) { throw 'Stage differs from pinned runtime.' }
}
function Invoke-Checked([string]$Program,[string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw 'Standalone renderer command failed.' }
}
if (-not $SkipBuild) {
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $installation=& $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    Import-Module (Join-Path $installation 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $installation -SkipAutomaticLocation -Arch amd64 -HostArch amd64 `
        -DevCmdArguments "-vcvars_ver=$($manifest.windows.msvcToolsVersion) -winsdk=$($manifest.windows.windowsSdkVersion)" | Out-Null
    if ($env:VCToolsVersion.TrimEnd('\') -ne $manifest.windows.msvcToolsVersion -or
        $env:WindowsSDKVersion.TrimEnd('\') -ne $manifest.windows.windowsSdkVersion) { throw 'MSVC/SDK are not pinned.' }
    $configure=@('-S',$PSScriptRoot,'-B',$testBuild,'-G','Ninja',
        '-DCMAKE_BUILD_TYPE=RelWithDebInfo',"-DCMAKE_MAKE_PROGRAM=$($state.ninjaExe)",
        "-DCMAKE_PREFIX_PATH=$($state.qtRoot)","-DMPV_INCLUDE_DIR=$($state.mpvRoot)/include", "-DMPV_LIBRARY=$($state.mpvLibrary)",
        '-DJMP_BUILD_THREADED_RENDERER_TEST=ON')
    if ($RendererSourceDirectory) { $configure+="-DJMP_THREADED_RENDERER_SOURCE_DIRECTORY=$(Resolve-WorkspacePath $RendererSourceDirectory)" }
    Invoke-Checked $state.cmakeExe $configure
    Invoke-Checked $state.cmakeExe @('--build',$testBuild,'--target','threaded-renderer-tests','--parallel','2')
}
Get-ChildItem -LiteralPath $stage -File -Filter '*.dll' | Copy-Item -Destination $testBuild -Force
New-Item -ItemType Directory -Force -Path (Join-Path $testBuild 'platforms') | Out-Null
Copy-Item -LiteralPath (Join-Path $stage 'platforms/qwindows.dll') -Destination (Join-Path $testBuild 'platforms') -Force
foreach ($module in @('QtQuick','QtQml')) {
    New-Item -ItemType Directory -Force -Path (Join-Path $testBuild 'qml') | Out-Null
    Copy-Item -LiteralPath (Join-Path $stage "qml/$module") -Destination (Join-Path $testBuild 'qml') -Recurse -Force
}
[IO.File]::WriteAllText((Join-Path $testBuild 'qt.conf'),"[Paths]`nPrefix=.`nPlugins=.`nQmlImports=qml`nQml2Imports=qml`n")
Invoke-Checked 'python' @((Join-Path $sourceRoot 'scripts/validate-windows-runtime.py'),$testBuild,
    '--report',(Join-Path $testBuild 'runtime-imports.json'))
if ($BuildOnly) { Write-Host 'Threaded renderer baseline executable and runtime are ready.'; return }
$reportDirectory=Join-Path $testBuild ('observation-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $reportDirectory | Out-Null
$report=Join-Path $reportDirectory 'threaded-renderer-report.json'
$fixture=Join-Path $sourceRoot 'tests/playback/fixtures/hls-video-30/index.m3u8'
$executable=Join-Path $testBuild 'threaded-renderer-tests.exe'
$arguments=@(('"'+$fixture+'"'),('"'+$report+'"'),[string]$DurationSeconds)
if ($GuiSync) { $arguments+='--gui-sync' }
if ($CaptureWindow) { $arguments+='--capture-window' }
if ($HiddenStartup) { $arguments+='--hidden-startup' }
$savedEnvironment=@{}
foreach ($name in @('PATH','QT_OPENGL','QSG_RENDER_LOOP','QSG_RHI_BACKEND','QT_SCALE_FACTOR','QSG_INFO')) {
    $savedEnvironment[$name]=[Environment]::GetEnvironmentVariable($name,'Process')
}
$process=$null; $observations=@(); $reason='completed'; $exitCode=$null
try {
    $env:PATH=$testBuild+';'+(Join-Path $env:SystemRoot 'System32')+';'+$env:SystemRoot
    $env:QT_OPENGL='desktop'; $env:QSG_RHI_BACKEND='opengl'; $env:QSG_RENDER_LOOP='threaded'; $env:QSG_INFO='1'
    $env:QT_SCALE_FACTOR=$null
    # This one explicitly authorized test must be exposed; SW_HIDE would turn
    # it into the earlier non-rendering smoke. No Desktop exe is launched.
    $process=Start-Process -FilePath $executable -ArgumentList $arguments -WorkingDirectory $testBuild `
        -WindowStyle Normal -PassThru -RedirectStandardOutput (Join-Path $reportDirectory 'test.stdout') `
        -RedirectStandardError (Join-Path $reportDirectory 'test.stderr')
    $clock=[Diagnostics.Stopwatch]::StartNew()
    while ($clock.Elapsed.TotalSeconds -lt 60) {
        $process.Refresh()
        if ($process.HasExited) { $exitCode=$process.ExitCode; break }
        $observations+=@{elapsedSeconds=$clock.Elapsed.TotalSeconds;rssBytes=$process.WorkingSet64;privateBytes=$process.PrivateMemorySize64}
        if ($process.WorkingSet64 -gt 2147483648 -or $process.PrivateMemorySize64 -gt 2147483648) {
            $reason='owned-memory-guard'; break
        }
        Start-Sleep -Milliseconds 250
    }
    $process.Refresh()
    if (-not $process.HasExited) {
        if ($reason -eq 'completed') { $reason='owned-60-second-timeout' }
        $process.Kill(); $null=$process.WaitForExit(5000)
    }
    [IO.File]::WriteAllText((Join-Path $reportDirectory 'owner-outcome.json'),
        ((@{schemaVersion=1;qualificationEligible=$false;scope='owned-exposed-threaded-renderer';reason=$reason;exitCode=$exitCode;
            guiSynchronousMpvProbe=$GuiSync.IsPresent;durationSeconds=$DurationSeconds;samples=$observations}|ConvertTo-Json -Depth 8)+"`n"),
        (New-Object Text.UTF8Encoding $false))
    Write-Host "Threaded renderer observation: $reportDirectory"
    if ($reason -ne 'completed' -or $exitCode -ne 0) { throw 'Bounded threaded renderer observation failed; preserve partial report.' }
} finally {
    if ($process) { $process.Refresh(); if (-not $process.HasExited) { $process.Kill(); $null=$process.WaitForExit(5000) }; $process.Dispose() }
    foreach ($name in $savedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name,$savedEnvironment[$name],'Process') }
}
