[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$StagePath,
    [string]$ReportDirectory,
    [string]$NodePath,
    [ValidatePattern('^[a-f0-9]{40}$')][string]$SourceCommit,
    [ValidateRange(1024,65535)][int]$DevToolsPort = 9224,
    [switch]$WebEngineGpu
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$sourceRoot = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
function Resolve-WorkspacePath([string]$Value) {
    $resolved = if ([IO.Path]::IsPathRooted($Value)) { [IO.Path]::GetFullPath($Value) }
                else { [IO.Path]::GetFullPath((Join-Path $sourceRoot $Value)) }
    if (-not $resolved.StartsWith($sourceRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Playback test paths must remain in this checkout.'
    }
    return $resolved
}
function Write-Json($Value,[string]$Path) {
    [IO.File]::WriteAllText($Path,(($Value | ConvertTo-Json -Depth 12)+"`n"),(New-Object Text.UTF8Encoding $false))
}
function Sha256([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }

$stage = Resolve-WorkspacePath $StagePath
$sourceExe = Join-Path $stage 'JellyfinMediaPlayer.exe'
if (-not (Test-Path -LiteralPath $sourceExe)) { throw 'StagePath must contain a deployed Desktop application.' }
foreach ($directory in @('data','cache')) {
    if (Test-Path -LiteralPath (Join-Path $stage $directory)) { throw 'Use a deployment stage without user profiles.' }
}
if (Get-NetTCPConnection -LocalPort $DevToolsPort -State Listen -ErrorAction SilentlyContinue) {
    throw 'The localhost test port is busy; no other process will be stopped.'
}
if (-not $NodePath) { $NodePath = (Get-Content -LiteralPath (Join-Path $sourceRoot 'build/windows-toolchain.json') -Raw | ConvertFrom-Json).nodeExe }
$node = [IO.Path]::GetFullPath((Get-Command $NodePath -CommandType Application).Source)
if (-not $ReportDirectory) { $ReportDirectory = Join-Path $sourceRoot ('build/live-playback-'+[guid]::NewGuid().ToString('N')) }
$reportRoot = Resolve-WorkspacePath $ReportDirectory
$buildRoot = Join-Path $sourceRoot 'build'
if (-not $reportRoot.StartsWith($buildRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or
    (Test-Path -LiteralPath $reportRoot)) { throw 'ReportDirectory must be a new directory under this checkout build directory.' }
$manifestPath = Join-Path $stage 'build-manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$embeddedSourceVerified = $false
if ($SourceCommit) {
    $exeBytes = [IO.File]::ReadAllBytes($sourceExe)
    $ascii = [Text.Encoding]::UTF8.GetString($exeBytes)
    $wide = [Text.Encoding]::Unicode.GetString($exeBytes)
    if ((-not $ascii.Contains($SourceCommit) -and -not $wide.Contains($SourceCommit)) -or
        $ascii.Contains($SourceCommit+'-dirty') -or $wide.Contains($SourceCommit+'-dirty')) {
        throw 'The Desktop executable does not embed the supplied clean source commit.'
    }
    $embeddedSourceVerified = $true
    $exeBytes = $null; $ascii = $null; $wide = $null
}
$fixtureRoot = Join-Path $sourceRoot 'tests/playback/fixtures/hls-video-30'
$fixtureInfoPath = Join-Path $fixtureRoot 'fixture-info.json'
$fixtureInfo = Get-Content -LiteralPath $fixtureInfoPath -Raw | ConvertFrom-Json
$allowedFixtureNames = @('index.m3u8','segment-000.ts','segment-001.ts','segment-002.ts')
if (@($fixtureInfo.sha256.PSObject.Properties).Count -ne 4) { throw 'Unexpected playback fixture.' }
foreach ($payload in $fixtureInfo.sha256.PSObject.Properties) {
    if ($payload.Name -cnotin $allowedFixtureNames -or (Sha256 (Join-Path $fixtureRoot $payload.Name)) -cne $payload.Value) {
        throw 'The synthetic playback fixture differs from its recorded hashes.'
    }
}

New-Item -ItemType Directory -Path $reportRoot | Out-Null
$appRoot = Join-Path $reportRoot 'app'
New-Item -ItemType Directory -Path $appRoot | Out-Null
Get-ChildItem -LiteralPath $stage -Force | Copy-Item -Destination $appRoot -Recurse
[IO.File]::WriteAllText((Join-Path $appRoot 'portable.txt'),"Owned isolated live playback regression`n")
New-Item -ItemType Directory -Path (Join-Path $appRoot 'data') | Out-Null
$settingsVersion = (Get-Content -LiteralPath (Join-Path $sourceRoot 'resources/settings/settings_description.json') -Raw |
    ConvertFrom-Json | Where-Object { $_.section -eq '__meta__' }).version
Write-Json @{version=$settingsVersion;sections=@{
    path=@{startupurl_desktop='bundled';startupurl_extension='bundled'}
    main=@{checkForUpdates=$false;fullscreen=$false;webMode='desktop';layout='desktop';logLevel='debug';userWebClient=''}
    video=@{hardwareDecoding='copy'}
    state=@{geometry=@{x=40;y=40;width=640;height=480};maximized=$true}
    system=@{systemname='Owned synthetic playback regression'}
}} (Join-Path $appRoot 'data/jellyfinmediaplayer.conf')
[IO.File]::WriteAllText((Join-Path $appRoot 'data/mpv.conf'),"ao=null`nloop-file=inf`n",[Text.Encoding]::ASCII)
Copy-Item -LiteralPath $fixtureRoot -Destination (Join-Path $appRoot 'data/hls-video-30') -Recurse

if (-not ('JmpLivePlaybackWindow' -as [type])) {
    # PowerShell 7 forwards Bitmap/Color to Drawing.Common/Primitives, with
    # additional Windows GDI interfaces on newer .NET. Resolve the runtime's
    # actual references; Windows PowerShell 5 uses System.Drawing instead.
    $drawingAssemblies=@([Drawing.Bitmap].Assembly.Location,[Drawing.Color].Assembly.Location)+
        @([Drawing.Bitmap].Assembly.GetReferencedAssemblies() | ForEach-Object { [Reflection.Assembly]::Load($_).Location })
    $drawingAssemblies=@($drawingAssemblies | Select-Object -Unique)
    Add-Type -ReferencedAssemblies $drawingAssemblies -TypeDefinition @'
using System;
using System.Text;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class JmpLivePlaybackWindow {
    public delegate bool EnumProc(IntPtr hwnd, IntPtr parameter);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    public sealed class Pixels {
        public bool PrintSucceeded, Red, Cyan, Blue;
        public int Width, Height;
    }
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr parameter);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int command);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr hwnd, out Rect rectangle);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr hwnd, IntPtr dc, uint flags);
    public static IntPtr Find(uint wantedPid) {
        IntPtr result=IntPtr.Zero;
        EnumWindows((hwnd,p)=> {
            uint pid; GetWindowThreadProcessId(hwnd,out pid);
            var title=new StringBuilder(256); GetWindowText(hwnd,title,256);
            if(pid==wantedPid && title.ToString()=="Jellyfin Media Player") { result=hwnd;return false; }
            return true;
        },IntPtr.Zero);
        return result;
    }
    public static Pixels Capture(IntPtr hwnd,string path) {
        var result=new Pixels(); Rect rect;
        if(hwnd==IntPtr.Zero || !GetClientRect(hwnd,out rect) || rect.Right<100 || rect.Bottom<100) return result;
        result.Width=rect.Right;result.Height=rect.Bottom;
        using(var bitmap=new Bitmap(rect.Right,rect.Bottom,PixelFormat.Format32bppArgb)) {
            using(var graphics=Graphics.FromImage(bitmap)) {
                var dc=graphics.GetHdc();
                try { result.PrintSucceeded=PrintWindow(hwnd,dc,3); } finally { graphics.ReleaseHdc(dc); }
            }
            var red=bitmap.GetPixel(rect.Right/2,rect.Bottom/4);
            var cyan=bitmap.GetPixel(rect.Right/2,3*rect.Bottom/4);
            var blue=bitmap.GetPixel(25,25);
            result.Red=red.R>180&&red.G<70&&red.B<70;
            result.Cyan=cyan.R<70&&cyan.G>180&&cyan.B>180;
            result.Blue=blue.R<70&&blue.G<70&&blue.B>180;
            bitmap.Save(path,ImageFormat.Png);
        }
        return result;
    }
}
'@
}

$outcome = [ordered]@{
    schemaVersion=1;scope='owned-full-desktop-live-video-and-animated-transparent-web-composition'
    qualificationEligible=$false;forcedGC=$false;oneHourMemoryQualification=$false;performanceQualification=$false
    authentication=$false;serverConnection=$false;userMedia=$false;syncPlay=$false
    appVersion=$manifest.appVersion;packageVersion=$manifest.packageVersion;sourceCommit=$SourceCommit
    embeddedSourceCommitVerified=$embeddedSourceVerified;manifestSha256=(Sha256 $manifestPath)
    exeSha256=(Sha256 $sourceExe);ownerScriptSha256=(Sha256 $PSCommandPath)
    driverScriptSha256=(Sha256 (Join-Path $PSScriptRoot 'live-playback-probe.mjs'))
    fixtureManifestSha256=(Sha256 $fixtureInfoPath);fixtureBytes=$fixtureInfo.generatedMediaBytes
    ownPortableProfile=$true;showWithoutActivation=$true;webEngineGpuOptIn=$WebEngineGpu.IsPresent
    sampledParentPrivateLimitBytes=2147483648;sampledChildrenPrivateLimitBytes=2147483648
    sampleIntervalMilliseconds=250;deadlineSeconds=55;closeLimitMilliseconds=2000;closeTimeoutMilliseconds=10000
    nativeChildExitGraceMilliseconds=500;orphanedOwnedChildrenBeforeCleanup=0
    completed=$false;passed=$false;failureCode=$null;probeExitCode=$null;applicationExitCode=$null
    samples=@();pixels=@();close=@{requested=$false;graceful=$false;duringActiveVideo=$false;elapsedMs=$null}
}
$savedEnvironment=@{}
foreach ($name in @('PATH','QTWEBENGINE_CHROMIUM_FLAGS','QTWEBENGINE_REMOTE_DEBUGGING','QT_OPENGL','QT_SCALE_FACTOR','QSG_RENDER_LOOP','QSG_RHI_BACKEND','QSG_INFO')) {
    $savedEnvironment[$name]=[Environment]::GetEnvironmentVariable($name,'Process')
}
$application=$null;$probe=$null;$appBirthTicks=0L;$ownedChildren=@{}
$failure=$null;$failureCode='owned-startup';$memoryGuard=$false
$clock=[Diagnostics.Stopwatch]::StartNew()
$samples=New-Object 'System.Collections.Generic.List[object]'
$pixels=New-Object 'System.Collections.Generic.List[object]'
$childState=@{count=0;privateBytes=0L;rssBytes=0L}

function Read-OwnedChildren {
    # Only descendants of the same root birth, whose executable is in this new
    # isolated copy, may enter observations or cleanup. Never export their IDs.
    $application.Refresh()
    if ($application.HasExited -or $application.StartTime.ToUniversalTime().Ticks -ne $appBirthTicks) { return @{count=0;privateBytes=0L;rssBytes=0L} }
    $inventory=@(Get-CimInstance Win32_Process -Property ProcessId,ParentProcessId,ExecutablePath,CreationDate)
    $members=New-Object 'System.Collections.Generic.HashSet[int]'
    $null=$members.Add($application.Id)
    do {
        $added=$false
        foreach ($item in $inventory) {
            if ($members.Contains([int]$item.ParentProcessId) -and $item.CreationDate -and $item.ExecutablePath -and
                $item.CreationDate.ToUniversalTime().Ticks -ge $appBirthTicks -and
                $item.ExecutablePath.StartsWith($appRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -and
                $members.Add([int]$item.ProcessId)) { $added=$true }
        }
    } while ($added)
    $result=@{count=0;privateBytes=0L;rssBytes=0L}
    foreach ($number in $members) {
        if ($number -eq $application.Id) { continue }
        $item=$inventory | Where-Object { $_.ProcessId -eq $number } | Select-Object -First 1
        try {
            $child=Get-Process -Id $number -ErrorAction Stop
            try {
                $birth=$child.StartTime.ToUniversalTime().Ticks
                if ([Math]::Abs($birth-$item.CreationDate.ToUniversalTime().Ticks) -gt 9 -or
                    -not $child.Path.StartsWith($appRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { continue }
                $ownedChildren[$number]=$birth
                ++$result.count;$result.privateBytes+=$child.PrivateMemorySize64;$result.rssBytes+=$child.WorkingSet64
            } finally { $child.Dispose() }
        } catch {} # A child may exit between snapshot and observation.
    }
    return $result
}

function Count-KnownOwnedChildren {
    $count=0
    foreach ($number in @($ownedChildren.Keys)) {
        try {
            $child=Get-Process -Id $number -ErrorAction Stop
            try {
                if (-not $child.HasExited -and $child.StartTime.ToUniversalTime().Ticks -eq $ownedChildren[$number] -and
                    $child.Path.StartsWith($appRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
                    ++$count
                }
            } finally { $child.Dispose() }
        } catch {}
    }
    return $count
}

try {
    $env:PATH=@($appRoot,$env:SystemRoot,(Join-Path $env:SystemRoot 'System32')) -join [IO.Path]::PathSeparator
    foreach ($name in @('QTWEBENGINE_CHROMIUM_FLAGS','QTWEBENGINE_REMOTE_DEBUGGING','QT_OPENGL','QT_SCALE_FACTOR','QSG_RENDER_LOOP','QSG_RHI_BACKEND','QSG_INFO')) {
        [Environment]::SetEnvironmentVariable($name,$null,'Process')
    }
    $arguments=@('--webengine-devtools',"--webengine-devtools-port=$DevToolsPort",'--desktop','--windowed','--terminal','--scale-factor=1')
    # Default exercises the application's own production WebEngine policy.
    # This explicit frozen ablation is unsuitable for an old binary lacking it.
    if ($WebEngineGpu) { $arguments+='--webengine-gpu' }
    $application=Start-Process -FilePath (Join-Path $appRoot 'JellyfinMediaPlayer.exe') -ArgumentList $arguments `
        -WorkingDirectory $appRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $reportRoot 'app.stdout') `
        -RedirectStandardError (Join-Path $reportRoot 'app.stderr')
    $null=$application.Handle
    $appBirthTicks=$application.StartTime.ToUniversalTime().Ticks
    $startupDeadline=[DateTime]::UtcNow.AddSeconds(12)
    do {
        $application.Refresh()
        if ($application.HasExited) { throw 'Owned app exited during initialization.' }
        $window=[JmpLivePlaybackWindow]::Find($application.Id)
        if ($window -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $startupDeadline)
    if ($window -eq [IntPtr]::Zero) { throw 'Owned exact-title window did not appear.' }
    $null=[JmpLivePlaybackWindow]::ShowWindow($window,4)
    $probeArguments=@(
        ('"'+(Join-Path $PSScriptRoot 'live-playback-probe.mjs')+'"'),
        ('"'+$appRoot+'"'),
        ('"'+(Join-Path $reportRoot 'probe.json')+'"'),
        [string]$DevToolsPort
    )
    $probe=Start-Process -FilePath $node -ArgumentList $probeArguments -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $reportRoot 'probe.stdout') -RedirectStandardError (Join-Path $reportRoot 'probe.stderr')
    $null=$probe.Handle # Cache the handle before PowerShell's process-exit race.
    $failureCode='owned-playback';$samplingClock=[Diagnostics.Stopwatch]::StartNew()
    $nextChild=0.0;$captureTimes=@(6.0,10.0,14.0);$nextCapture=0
    while ($clock.Elapsed.TotalSeconds -lt 55) {
        $application.Refresh();$probe.Refresh()
        if ($application.HasExited) { throw 'Owned app exited during playback.' }
        if ($samplingClock.Elapsed.TotalSeconds -ge $nextChild) {
            $childState=Read-OwnedChildren;$nextChild=$samplingClock.Elapsed.TotalSeconds+1.0
        }
        $sample=@{elapsedSeconds=$samplingClock.Elapsed.TotalSeconds;parentPrivateBytes=$application.PrivateMemorySize64;
            parentRssBytes=$application.WorkingSet64;childCount=$childState.count;
            childrenPrivateBytes=$childState.privateBytes;childrenRssBytes=$childState.rssBytes}
        $samples.Add($sample)
        if ($sample.parentPrivateBytes -gt 2147483648 -or $sample.childrenPrivateBytes -gt 2147483648) {
            $memoryGuard=$true;$failureCode='owned-private-memory-ceiling';throw 'Owned private memory ceiling exceeded.'
        }
        if ($nextCapture -lt $captureTimes.Count -and $samplingClock.Elapsed.TotalSeconds -ge $captureTimes[$nextCapture]) {
            $capture=[JmpLivePlaybackWindow]::Capture([JmpLivePlaybackWindow]::Find($application.Id),
                (Join-Path $reportRoot ('frame-'+$nextCapture+'.png')))
            $pixels.Add(@{elapsedSeconds=$samplingClock.Elapsed.TotalSeconds;width=$capture.Width;height=$capture.Height;
                printSucceeded=$capture.PrintSucceeded;red=$capture.Red;cyan=$capture.Cyan;blue=$capture.Blue})
            ++$nextCapture
        }
        if ($probe.HasExited) { break }
        Start-Sleep -Milliseconds 250
    }
    $probe.Refresh()
    if (-not $probe.HasExited) { $failureCode='owned-deadline';throw 'Owned playback deadline exceeded.' }
    $probe.WaitForExit();$outcome.probeExitCode=$probe.ExitCode
    if ($probe.ExitCode -ne 0) { $failureCode='owned-probe-failed';throw 'Owned playback driver failed.' }
    $driver=Get-Content -LiteralPath (Join-Path $reportRoot 'probe.json') -Raw | ConvertFrom-Json
    if (-not $driver.completed -or -not $driver.passed -or -not $driver.closeDuringActiveVideoPrepared) {
        $failureCode='owned-driver-incomplete';throw 'Owned driver did not establish active playback for close.'
    }
    $listeners=@(Get-NetTCPConnection -LocalPort $DevToolsPort -State Listen -ErrorAction SilentlyContinue)
    if ($listeners.Count -ne 1 -or $listeners[0].OwningProcess -ne $application.Id -or $listeners[0].LocalAddress -ne '127.0.0.1') {
        $failureCode='owned-debugger-binding';throw 'Debugger binding does not belong solely to the owned localhost app.'
    }
    if ($pixels.Count -ne 3 -or @($pixels | Where-Object { -not $_.printSucceeded -or -not $_.red -or -not $_.cyan -or -not $_.blue }).Count) {
        $failureCode='owned-composed-pixels';throw 'Sparse window captures did not verify the composed video and blue overlay.'
    }
    $outcome.completed=$true;$outcome.close.duringActiveVideo=$true
} catch {
    $failure=$_;$outcome.failureCode=$failureCode
} finally {
    foreach ($name in $savedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name,$savedEnvironment[$name],'Process') }
    if ($probe) {
        try { $probe.Refresh();if (-not $probe.HasExited) { $probe.Kill();$null=$probe.WaitForExit(3000) } } catch {}
    }
    if ($application) {
        try {
            $application.Refresh()
            if (-not $application.HasExited) {
                if ($memoryGuard) { $application.Kill();$null=$application.WaitForExit(3000) }
                else {
                    $closeTimer=[Diagnostics.Stopwatch]::StartNew()
                    $outcome.close.requested=[JmpLivePlaybackWindow]::PostMessage([JmpLivePlaybackWindow]::Find($application.Id),0x0010,[IntPtr]::Zero,[IntPtr]::Zero)
                    $outcome.close.graceful=$application.WaitForExit(10000)
                    $outcome.close.elapsedMs=$closeTimer.Elapsed.TotalMilliseconds
                    if (-not $outcome.close.graceful) { $application.Kill();$null=$application.WaitForExit(3000) }
                }
            }
        } catch { if (-not $failure) { $failure=$_;$outcome.failureCode='owned-cleanup' } }
    }
    # Measure native exit before any helper kill, allowing a short fixed grace
    # for already-verified Chromium children to finish their own shutdown.
    $childExitClock=[Diagnostics.Stopwatch]::StartNew()
    do {
        $outcome.orphanedOwnedChildrenBeforeCleanup=Count-KnownOwnedChildren
        if (-not $outcome.close.graceful -or $outcome.orphanedOwnedChildrenBeforeCleanup -eq 0) { break }
        Start-Sleep -Milliseconds 50
    } while ($childExitClock.Elapsed.TotalMilliseconds -lt 500)
    $outcome.orphanedOwnedChildrenBeforeCleanup=Count-KnownOwnedChildren
    $outcome.nativeChildExitGraceElapsedMs=$childExitClock.Elapsed.TotalMilliseconds
    # The retained birth-time and executable-path receipts prevent PID reuse
    # from making cleanup target an unrelated process after root termination.
    foreach ($number in @($ownedChildren.Keys)) {
        try {
            $child=Get-Process -Id $number -ErrorAction Stop
            try {
                if ($child.StartTime.ToUniversalTime().Ticks -eq $ownedChildren[$number] -and
                    $child.Path.StartsWith($appRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
                    $child.Kill();$null=$child.WaitForExit(3000)
                }
            } finally { $child.Dispose() }
        } catch {}
    }
    $outcome.remainingKnownOwnedChildren=Count-KnownOwnedChildren
    $outcome.remainingOwnedListeners=0
    if ($application) {
        try {
            $application.Refresh()
            if ($application.HasExited) { $outcome.applicationExitCode=$application.ExitCode }
            $outcome.remainingOwnedListeners=@(Get-NetTCPConnection -LocalPort $DevToolsPort -State Listen -ErrorAction SilentlyContinue |
                Where-Object { $_.OwningProcess -eq $application.Id }).Count
        } catch { if (-not $failure) { $failure=$_;$outcome.failureCode='owned-exit-state' } }
    }
    $outcome.samples=@($samples.ToArray());$outcome.pixels=@($pixels.ToArray())
    $outcome.elapsedSeconds=$clock.Elapsed.TotalSeconds
    $outcome.peakParentPrivateBytes=if($samples.Count){($samples.ToArray() | Measure-Object parentPrivateBytes -Maximum).Maximum}else{0}
    $outcome.peakChildrenPrivateBytes=if($samples.Count){($samples.ToArray() | Measure-Object childrenPrivateBytes -Maximum).Maximum}else{0}
    $stderrPath=Join-Path $reportRoot 'app.stderr'
    $outcome.renderStuckWarningCount=if(Test-Path -LiteralPath $stderrPath){@(Select-String -LiteralPath $stderrPath -Pattern 'mpv_render_context_render\(\).*not being called|mpv_render_context_render\(\).*stuck').Count}else{0}
    $outcome.swapMissingWarningCount=if(Test-Path -LiteralPath $stderrPath){@(Select-String -LiteralPath $stderrPath -Pattern 'mpv_render_report_swap\(\) not being called').Count}else{0}
    $outcome.geometrySaveCount=if(Test-Path -LiteralPath $stderrPath){@(Select-String -LiteralPath $stderrPath -Pattern 'Window state when saving geometry:').Count}else{0}
    if (Test-Path -LiteralPath (Join-Path $reportRoot 'probe.json')) { $outcome.probeReportSha256=Sha256 (Join-Path $reportRoot 'probe.json') }
    if ($outcome.completed -and -not $failure -and $outcome.close.requested -and $outcome.close.graceful -and
        $outcome.close.duringActiveVideo -and $outcome.close.elapsedMs -lt 2000 -and
        $outcome.applicationExitCode -eq 0 -and $outcome.orphanedOwnedChildrenBeforeCleanup -eq 0 -and
        $outcome.remainingKnownOwnedChildren -eq 0 -and
        $outcome.remainingOwnedListeners -eq 0) { $outcome.passed=$true }
    elseif (-not $failure -and $outcome.completed) { $outcome.failureCode='owned-close-latency' }
    Write-Json $outcome (Join-Path $reportRoot 'live-playback.json')
    if ($probe) { $probe.Dispose() };if ($application) { $application.Dispose() }
}
Write-Host "Live Desktop playback evidence retained: $reportRoot"
if ($failure) { throw $failure }
if (-not $outcome.passed) { throw 'Live Desktop playback regression failed; inspect the retained numeric report and owned logs.' }
