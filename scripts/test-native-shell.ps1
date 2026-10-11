[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$StagePath,
    [string]$NodePath,
    [string]$ReportDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$sourceRoot = Split-Path $PSScriptRoot -Parent
$stage = [IO.Path]::GetFullPath($StagePath)
if (-not (Test-Path -LiteralPath (Join-Path $stage 'JellyfinMediaPlayer.exe'))) {
    throw 'StagePath must contain a complete deployed application.'
}
# Refuse an active application, since its single-instance socket is shared.
# Refuse profiles in the source to avoid copying account data into test reports.
if (Get-Process -Name JellyfinMediaPlayer -ErrorAction SilentlyContinue) {
    throw 'Close Jellyfin Media Player before this optional runtime test.'
}
foreach ($directory in @('data', 'cache')) {
    if (Test-Path -LiteralPath (Join-Path $stage $directory)) {
        throw 'Use a clean deployment stage without a user profile.'
    }
}
if (Get-NetTCPConnection -LocalPort 9222 -State Listen -ErrorAction SilentlyContinue) {
    throw 'Local port 9222 is busy; the test does not stop other applications.'
}
if (-not $NodePath) {
    $statePath = Join-Path $sourceRoot 'build/windows-toolchain.json'
    if (Test-Path -LiteralPath $statePath) {
        $NodePath = (Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json).nodeExe
    } else { $NodePath = (Get-Command node).Source }
}
$NodePath = [IO.Path]::GetFullPath((Get-Command $NodePath -CommandType Application -ErrorAction Stop).Source)
if (-not $ReportDirectory) {
    $ReportDirectory = Join-Path $sourceRoot ('build/native-shell-smoke-' + [guid]::NewGuid().ToString('N'))
}
$reportRoot = [IO.Path]::GetFullPath($ReportDirectory)
if (Test-Path -LiteralPath $reportRoot) { throw 'ReportDirectory must be a new directory.' }
New-Item -ItemType Directory -Path $reportRoot | Out-Null
$isolatedRoot = Join-Path $reportRoot 'portable Тест_日本語'
New-Item -ItemType Directory -Path $isolatedRoot | Out-Null
Get-ChildItem -LiteralPath $stage -Force | Copy-Item -Destination $isolatedRoot -Recurse
Set-Content -LiteralPath (Join-Path $isolatedRoot 'portable.txt') -Value 'Isolated NativeShell runtime test'
New-Item -ItemType Directory -Path (Join-Path $isolatedRoot 'data') | Out-Null
$settingsVersion = (Get-Content (Join-Path $sourceRoot 'resources/settings/settings_description.json') -Raw |
    ConvertFrom-Json | Where-Object { $_.section -eq '__meta__' }).version
$configuration = @{
    version = $settingsVersion
    sections = @{
        path = @{ startupurl_desktop = 'bundled'; startupurl_extension = 'bundled' }
        main = @{ checkForUpdates = $false; fullscreen = $false; webMode = 'desktop'; layout = 'desktop'; logLevel = 'debug' }
        system = @{ systemname = 'Тест — 日本語 ✓' }
    }
}
$configuration | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $isolatedRoot 'data/jellyfinmediaplayer.conf') -Encoding utf8
# Synthetic audio exercises actual libmpv readiness without an audio device.
# Hidden Qt windows do not render video; tests/render supplies an offscreen GL
# render pass for the production renderer instead.
Set-Content -LiteralPath (Join-Path $isolatedRoot 'data/mpv.conf') -Value 'ao=null' -Encoding ascii
Copy-Item -LiteralPath (Join-Path $sourceRoot 'tests/playback/fixtures/hls-silent-30') -Destination (Join-Path $isolatedRoot 'data/hls-silent-30') -Recurse
$application = $null
$probe = $null
$launchPhase = 'native-shell'
$debuggingEnvironment = [Environment]::GetEnvironmentVariable('QTWEBENGINE_REMOTE_DEBUGGING', 'Process')
$chromiumEnvironment = [Environment]::GetEnvironmentVariable('QTWEBENGINE_CHROMIUM_FLAGS', 'Process')
$pathEnvironment = [Environment]::GetEnvironmentVariable('PATH', 'Process')
function Stop-OwnedApplication {
    if ($script:application) {
        $script:application.Refresh()
        if (-not $script:application.HasExited) {
            if (-not $script:application.CloseMainWindow()) {
                Stop-Process -Id $script:application.Id
            } elseif (-not $script:application.WaitForExit(5000)) {
                Stop-Process -Id $script:application.Id
            }
        }
        $script:application = $null
    }
}
function Get-OwnedProcessState([Diagnostics.Process]$Process) {
    if (-not $Process) { return [ordered]@{ started = $false } }
    $Process.Refresh()
    $exited = $Process.HasExited
    $state = [ordered]@{ started = $true; processId = $Process.Id; hasExited = $exited }
    if ($exited) {
        $state.exitCode = $Process.ExitCode
        $state.exitCodeHex = '0x' + [BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$Process.ExitCode), 0).ToString('X8')
    }
    return $state
}
try {
    # Both app launches inherit only packaged and Windows system dependencies.
    # The probe executable was resolved before removing developer tool paths.
    $env:PATH = @($isolatedRoot, $env:SystemRoot, (Join-Path $env:SystemRoot 'System32')) -join [IO.Path]::PathSeparator
    $probeArguments = @(
        '"' + (Join-Path $PSScriptRoot 'native-shell-smoke.mjs') + '"',
        '"' + (Join-Path $reportRoot 'native-shell.json') + '"',
        '"' + $isolatedRoot + '"'
    )
    $probe = Start-Process -FilePath $NodePath -ArgumentList $probeArguments -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $reportRoot 'probe.stdout') -RedirectStandardError (Join-Path $reportRoot 'probe.stderr')
    $application = Start-Process -FilePath (Join-Path $isolatedRoot 'JellyfinMediaPlayer.exe') `
        -ArgumentList @('--webengine-devtools', '--desktop', '--windowed', '--terminal') -WorkingDirectory $isolatedRoot `
        -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $reportRoot 'app.stdout') `
        -RedirectStandardError (Join-Path $reportRoot 'app.stderr')
    $probeDeadline = [DateTime]::UtcNow.AddSeconds(55)
    do {
        $application.Refresh()
        if ($application.HasExited) { throw 'Application exited before the NativeShell probe completed.' }
        $probe.Refresh()
        if ($probe.HasExited) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $probeDeadline)
    if (-not $probe.HasExited) { throw 'NativeShell runtime probe timed out after 55 seconds.' }
    if ($probe.ExitCode -ne 0) { throw "NativeShell probe failed; inspect $reportRoot" }
    $application.Refresh()
    if ($application.HasExited) { throw 'Application exited during the NativeShell check.' }
    $listeners = @(Get-NetTCPConnection -LocalPort 9222 -State Listen -ErrorAction SilentlyContinue)
    if ($listeners.Count -ne 1 -or $listeners[0].LocalAddress -ne '127.0.0.1' -or
        $listeners[0].OwningProcess -ne $application.Id) { throw 'Explicit DevTools launch did not bind only localhost.' }
    Stop-OwnedApplication
    $launchPhase = 'normal-launch'
    $env:QTWEBENGINE_REMOTE_DEBUGGING = '0.0.0.0:9222'
    $env:QTWEBENGINE_CHROMIUM_FLAGS = '--remote-debugging-address=0.0.0.0 --remote-debugging-port=9222 --remote-debugging-pipe'
    $application = Start-Process -FilePath (Join-Path $isolatedRoot 'JellyfinMediaPlayer.exe') `
        -ArgumentList @('--desktop', '--windowed', '--terminal') -WorkingDirectory $isolatedRoot `
        -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $reportRoot 'normal.stdout') `
        -RedirectStandardError (Join-Path $reportRoot 'normal.stderr')
    $normalDeadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        $application.Refresh()
        if ($application.HasExited) { throw 'Normal application launch exited unexpectedly.' }
        $loaded = Test-Path -LiteralPath (Join-Path $reportRoot 'normal.stderr')
        if ($loaded) { $loaded = [bool](Select-String -LiteralPath (Join-Path $reportRoot 'normal.stderr') -Pattern 'WebEngineLoadRequest success:' -Quiet) }
        if ($loaded) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $normalDeadline)
    if (-not $loaded) { throw 'Normal application launch did not load its bundled page within 15 seconds.' }
    if (Get-NetTCPConnection -LocalPort 9222 -State Listen -ErrorAction SilentlyContinue) {
        throw 'Normal launch exposed a debugger despite the explicit CLI opt-in requirement.'
    }
    [ordered]@{ passed = $true; explicitAddress = '127.0.0.1'; normalListenerCount = 0 } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $reportRoot 'devtools.json') -Encoding utf8
    Write-Host "NativeShell runtime checks passed. Reports: $reportRoot"
} catch {
    $originalFailure = $_
    try {
        $failure = [ordered]@{
            passed = $false
            phase = $launchPhase
            reason = $originalFailure.Exception.Message
            application = Get-OwnedProcessState $application
            probe = Get-OwnedProcessState $probe
        }
        $failure | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $reportRoot 'startup-failure.json') -Encoding utf8
        Write-Host "NativeShell failure report: $(Join-Path $reportRoot 'startup-failure.json')"
    } catch {
        Write-Host 'Could not record the owned test process state.'
    }
    throw $originalFailure
} finally {
    # Restore first, even if owned-process cleanup below throws.
    [Environment]::SetEnvironmentVariable('PATH', $pathEnvironment, 'Process')
    Stop-OwnedApplication
    if ($probe) {
        $probe.Refresh()
        if (-not $probe.HasExited) { Stop-Process -Id $probe.Id }
    }
    [Environment]::SetEnvironmentVariable('QTWEBENGINE_REMOTE_DEBUGGING', $debuggingEnvironment, 'Process')
    [Environment]::SetEnvironmentVariable('QTWEBENGINE_CHROMIUM_FLAGS', $chromiumEnvironment, 'Process')
}
