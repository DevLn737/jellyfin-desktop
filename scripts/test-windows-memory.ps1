[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$StagePath,
    [string]$ReportDirectory,
    [switch]$Smoke,
    [int]$SmokeSeconds = 15
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$sourceRoot = Split-Path $PSScriptRoot -Parent
$stage = [IO.Path]::GetFullPath($StagePath)
$manifestPath = Join-Path $sourceRoot 'build-manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$protocolPath = Join-Path $sourceRoot $manifest.qt.qualification.memoryProtocolPath
$protocol = Get-Content -LiteralPath $protocolPath -Raw | ConvertFrom-Json
$state = Get-Content -LiteralPath (Join-Path $sourceRoot 'build/windows-toolchain.json') -Raw | ConvertFrom-Json
$node = [IO.Path]::GetFullPath($state.nodeExe)
$python = (Get-Command python -CommandType Application).Source
$commit = (& git -C $sourceRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $commit -notmatch '^[a-f0-9]{40}$' -or @(& git -C $sourceRoot status --porcelain).Count) {
    throw 'Memory measurements require a clean immutable source commit.'
}
foreach ($directory in @('data', 'cache')) {
    if (Test-Path -LiteralPath (Join-Path $stage $directory)) { throw 'Use a deployment stage without a user profile.' }
}
$manifestSha = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ((Get-FileHash -LiteralPath (Join-Path $stage 'build-manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant() -ne $manifestSha) {
    throw 'Deployment manifest differs from the locked source.'
}
$exe = Join-Path $stage 'JellyfinMediaPlayer.exe'
$bytes = [IO.File]::ReadAllBytes($exe)
$ascii = [Text.Encoding]::UTF8.GetString($bytes)
$wide = [Text.Encoding]::Unicode.GetString($bytes)
if ((-not $ascii.Contains($commit) -and -not $wide.Contains($commit)) -or
    $ascii.Contains($commit + '-dirty') -or $wide.Contains($commit + '-dirty')) {
    throw 'Deployment executable differs from this clean source commit.'
}
$bytes = $null; $ascii = $null; $wide = $null
foreach ($name in @('Qt6Core.dll', 'Qt6WebEngineCore.dll', 'Qt6WebEngineQuick.dll', 'QtWebEngineProcess.exe')) {
    $payload = $manifest.qt.packageSourceEvidence.runtimePayloads | Where-Object { $_.name -ceq $name }
    if (-not $payload -or (Get-FileHash -LiteralPath (Join-Path $stage $name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $payload.sha256) {
        throw 'Deployed Qt payload differs from its reviewed package.'
    }
}
if ($Smoke -and ($SmokeSeconds -lt 5 -or $SmokeSeconds -gt 300)) { throw 'SmokeSeconds must be 5..300.' }
$duration = if ($Smoke) { $SmokeSeconds } else { [int]$protocol.minimumDurationSeconds }
$warmup = if ($Smoke) { 0 } else { [int]$protocol.warmupSeconds }
if (-not $ReportDirectory) { $ReportDirectory = Join-Path $sourceRoot ('build/memory-' + [guid]::NewGuid().ToString('N')) }
$reportRoot = [IO.Path]::GetFullPath($ReportDirectory)
if (-not $reportRoot.StartsWith((Join-Path $sourceRoot 'build') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
    (Test-Path -LiteralPath $reportRoot)) { throw 'ReportDirectory must be a new directory within this checkout build directory.' }
foreach ($port in @(9222, 9223)) {
    if (Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue) { throw 'An owned test port is busy; no other application will be stopped.' }
}
New-Item -ItemType Directory -Path $reportRoot | Out-Null
& $python (Join-Path $PSScriptRoot 'validate-windows-runtime.py') $stage '--report' (Join-Path $reportRoot 'imports.json')
if ($LASTEXITCODE -ne 0) { throw 'Deployed import closure failed.' }
$settingsVersion = (Get-Content (Join-Path $sourceRoot 'resources/settings/settings_description.json') -Raw |
    ConvertFrom-Json | Where-Object { $_.section -eq '__meta__' }).version
$owned = @()
$scenarios = @()
$savedPath = $env:PATH
$savedFlags = [Environment]::GetEnvironmentVariable('QTWEBENGINE_CHROMIUM_FLAGS', 'Process')
$savedDebug = [Environment]::GetEnvironmentVariable('QTWEBENGINE_REMOTE_DEBUGGING', 'Process')
function Write-Json($Value, [string]$Path) {
    [IO.File]::WriteAllText($Path, (($Value | ConvertTo-Json -Depth 15) + "`n"), (New-Object Text.UTF8Encoding $false))
}
function Get-TreeSample($Scenario, [double]$Elapsed, $ProcessInventory) {
    $Scenario.app.Refresh()
    if ($Scenario.app.HasExited) { throw 'Owned Desktop exited during measurement.' }
    $members = New-Object 'System.Collections.Generic.HashSet[int]'
    $null = $members.Add($Scenario.app.Id)
    do {
        $added = $false
        foreach ($item in $ProcessInventory) {
            if ($members.Contains([int]$item.ParentProcessId) -and $members.Add([int]$item.ProcessId)) { $added = $true }
        }
    } while ($added)
    [long]$rss = 0; $count = 0
    foreach ($processNumber in $members) {
        $process = $null
        try {
            $observed = $ProcessInventory | Where-Object { [int]$_.ProcessId -eq $processNumber } | Select-Object -First 1
            if (-not $observed -or -not $observed.CreationDate) { continue }
            $process = [Diagnostics.Process]::GetProcessById($processNumber)
            # A terminated PID reused by another process is outside this run.
            $ticks = $process.StartTime.ToUniversalTime().Ticks
            if ($ticks -lt $Scenario.startedUtcTicks -or
                [Math]::Abs($ticks - $observed.CreationDate.ToUniversalTime().Ticks) -gt 9) { continue }
            $rss += $process.WorkingSet64; ++$count
            $Scenario.children[$processNumber] = $ticks
        } catch [ArgumentException] {} catch [InvalidOperationException] {} finally { if ($process) { $process.Dispose() } }
    }
    if ($rss -le 0 -or $count -lt 2) { throw 'Native/WebEngine process tree was not observed.' }
    return [ordered]@{ elapsedSeconds = [Math]::Round($Elapsed, 3); rssBytes = $rss; processCount = $count }
}
function Stop-Owned {
    # Initialization can fail before the first sample. Discover current owned
    # descendants before killing parents, then retain creation-time identity.
    try {
        $inventory = @(Get-CimInstance Win32_Process -Property ProcessId, ParentProcessId, ExecutablePath, CreationDate)
        foreach ($scenario in $script:scenarios) {
            $members = New-Object 'System.Collections.Generic.HashSet[int]'
            $null = $members.Add($scenario.app.Id)
            do {
                $added = $false
                foreach ($item in $inventory) {
                    if ($members.Contains([int]$item.ParentProcessId) -and $item.ExecutablePath -and
                        $item.ExecutablePath.StartsWith($scenario.profileRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -and
                        $members.Add([int]$item.ProcessId)) { $added = $true }
                }
            } while ($added)
            foreach ($processNumber in $members) {
                $process = $null
                try {
                    $observed = $inventory | Where-Object { [int]$_.ProcessId -eq $processNumber } | Select-Object -First 1
                    if (-not $observed -or -not $observed.CreationDate) { continue }
                    $process = [Diagnostics.Process]::GetProcessById($processNumber)
                    if ([Math]::Abs($process.StartTime.ToUniversalTime().Ticks - $observed.CreationDate.ToUniversalTime().Ticks) -gt 9) { continue }
                    if (($processNumber -eq $scenario.app.Id -and $process.StartTime.ToUniversalTime().Ticks -eq $scenario.startedUtcTicks) -or
                        ($processNumber -ne $scenario.app.Id -and $process.StartTime.ToUniversalTime().Ticks -ge $scenario.startedUtcTicks)) {
                        $scenario.children[$processNumber] = $process.StartTime.ToUniversalTime().Ticks
                    }
                } catch {} finally { if ($process) { $process.Dispose() } }
            }
        }
    } catch {}
    foreach ($process in $script:owned) {
        try { $process.Refresh(); if (-not $process.HasExited) { $process.Kill() } } catch {}
    }
    foreach ($scenario in $script:scenarios) {
        foreach ($processNumber in @($scenario.children.Keys)) {
            $process = $null
            try {
                $process = [Diagnostics.Process]::GetProcessById($processNumber)
                if ($process.StartTime.ToUniversalTime().Ticks -eq $scenario.children[$processNumber]) { $process.Kill() }
            } catch {} finally { if ($process) { $process.Dispose() } }
        }
    }
}
$report = [ordered]@{
    schemaVersion = 1; appVersion = $manifest.appVersion; qtVersion = $manifest.qt.version
    buildCommit = $commit; buildManifestSha256 = $manifestSha; issue = 'QTBUG-141377'
    passed = $false; qualificationEligible = -not $Smoke; processTreeIncluded = $true
    protocolSha256 = (Get-FileHash -LiteralPath $protocolPath -Algorithm SHA256).Hash.ToLowerInvariant()
    rawSamplesPath = 'quality/qt6113-memory-samples.json'; rawSamplesSha256 = ''; scenarios = @()
    scope = $protocol.scope; workload = 'hidden full Desktop, local SVG/XHR and native silent music'
    forcedGarbageCollection = $false
    startedUtc = [DateTime]::UtcNow.ToString('o')
    windowsBuild = [Environment]::OSVersion.Version.Build
    limitations = $protocol.limitations
}
try {
    $env:QTWEBENGINE_REMOTE_DEBUGGING = $null
    # Test-only workload fidelity: a hidden page must still issue 10 XHR/s.
    # No GC option or workaround is used, and these flags are not release flags.
    $env:QTWEBENGINE_CHROMIUM_FLAGS = '--disable-background-timer-throttling --disable-renderer-backgrounding'
    $port = 9222
    foreach ($name in $protocol.requiredScenarios) {
        $profileRoot = Join-Path $reportRoot $name
        New-Item -ItemType Directory -Path $profileRoot | Out-Null
        Get-ChildItem -LiteralPath $stage -Force | Copy-Item -Destination $profileRoot -Recurse
        Set-Content -LiteralPath (Join-Path $profileRoot 'portable.txt') -Value 'Explicit isolated memory test' -Encoding ascii
        New-Item -ItemType Directory -Path (Join-Path $profileRoot 'data') | Out-Null
        $configuration = @{
            version = $settingsVersion; sections = @{
                path = @{ startupurl_desktop = 'bundled'; startupurl_extension = 'bundled' }
                main = @{ checkForUpdates = $false; fullscreen = $false; webMode = 'desktop'; layout = 'desktop'; logLevel = 'error' }
                system = @{ systemname = 'Local memory test' }
            }
        }
        Write-Json $configuration (Join-Path $profileRoot 'data/jellyfinmediaplayer.conf')
        Set-Content -LiteralPath (Join-Path $profileRoot 'data/mpv.conf') -Value 'ao=null' -Encoding ascii
        $env:PATH = @($profileRoot, $env:SystemRoot, (Join-Path $env:SystemRoot 'System32')) -join [IO.Path]::PathSeparator
        $app = Start-Process -FilePath (Join-Path $profileRoot 'JellyfinMediaPlayer.exe') -WorkingDirectory $profileRoot `
            -ArgumentList @('--webengine-devtools', '--webengine-devtools-port', $port, '--desktop', '--windowed', '--terminal') `
            -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $profileRoot 'app.stdout') -RedirectStandardError (Join-Path $profileRoot 'app.stderr')
        $owned += $app
        $statusPath = Join-Path $profileRoot 'workload.json'
        $arguments = @('"' + (Join-Path $PSScriptRoot 'memory-stress.mjs') + '"', '"' + $profileRoot + '"', '"' + $statusPath + '"', $port, $name, $duration, $warmup)
        $driver = Start-Process -FilePath $node -WorkingDirectory $profileRoot -ArgumentList $arguments -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput (Join-Path $profileRoot 'driver.stdout') -RedirectStandardError (Join-Path $profileRoot 'driver.stderr')
        $owned += $driver
        $scenarios += @{ name = $name; app = $app; driver = $driver; statusPath = $statusPath
            samples = New-Object 'System.Collections.Generic.List[object]'; children = @{}; port = $port
            profileRoot = $profileRoot; startedUtcTicks = $app.StartTime.ToUniversalTime().Ticks }
        ++$port
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(50)
    do {
        $ready = $true
        foreach ($scenario in $scenarios) {
            $scenario.driver.Refresh(); $scenario.app.Refresh()
            if ($scenario.driver.HasExited -or $scenario.app.HasExited) { throw 'Owned workload exited before initialization.' }
            if (-not (Test-Path -LiteralPath $scenario.statusPath)) { $ready = $false; continue }
            try { $status = Get-Content -LiteralPath $scenario.statusPath -Raw | ConvertFrom-Json } catch { $ready = $false; continue }
            if (-not $status.initialized) { $ready = $false }
        }
        if ($ready) { break }; Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if (-not $ready) { throw 'Owned workloads failed to initialize within 50 seconds.' }
    foreach ($scenario in $scenarios) {
        $listeners = @(Get-NetTCPConnection -LocalPort $scenario.port -State Listen -ErrorAction SilentlyContinue)
        if ($listeners.Count -ne 1 -or $listeners[0].LocalAddress -ne '127.0.0.1' -or $listeners[0].OwningProcess -ne $scenario.app.Id) {
            throw 'Isolated app did not expose its own localhost-only opt-in debugger.'
        }
    }
    foreach ($scenario in $scenarios) {
        Write-Json @{ start = $true } (Join-Path $scenario.profileRoot 'memory-owner-start.json')
    }
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $nextSample = [double]$warmup
    $baselineTime = $null
    while ($true) {
        foreach ($scenario in $scenarios) {
            $scenario.driver.Refresh(); $scenario.app.Refresh()
            if ($scenario.app.HasExited -or $scenario.driver.HasExited) { throw 'Owned workload stopped before the final memory sample.' }
        }
        if ($clock.Elapsed.TotalSeconds -lt $nextSample) { Start-Sleep -Milliseconds 100; continue }
        $inventory = @(Get-CimInstance Win32_Process -Property ProcessId, ParentProcessId, CreationDate)
        $elapsed = $clock.Elapsed.TotalSeconds
        if ($null -eq $baselineTime) { $baselineTime = $elapsed }
        $measured = $elapsed - $baselineTime
        foreach ($scenario in $scenarios) {
            $sample = Get-TreeSample $scenario $measured $inventory
            $scenario.samples.Add($sample)
            if ($sample.rssBytes -gt $protocol.absoluteRssLimitBytes) { throw 'Owned process-tree RSS exceeded the predeclared ceiling.' }
        }
        $raw = [ordered]@{ schemaVersion = 1; scope = $protocol.scope; scenarios = @($scenarios | ForEach-Object {
            [ordered]@{ name = $_.name; samples = @($_.samples.ToArray()) }
        }) }
        Write-Json $raw (Join-Path $reportRoot 'memory-samples.json')
        Write-Host ('Memory sample {0:N0}/{1}s; RSS MiB: {2}' -f $measured, $duration,
            (($scenarios | ForEach-Object { '{0}={1:N1}' -f $_.name, ($_.samples[$_.samples.Count - 1].rssBytes / 1MB) }) -join ', '))
        if ($measured -ge $duration) { break }
        $nextSample = $clock.Elapsed.TotalSeconds + $protocol.sampleIntervalSeconds
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(45)
    do {
        $complete = $true
        foreach ($scenario in $scenarios) { $scenario.driver.Refresh(); if (-not $scenario.driver.HasExited) { $complete = $false } }
        if ($complete) { break }; Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if (-not $complete) { throw 'Workload drivers did not complete their final cadence checks.' }
    $workloads = @()
    foreach ($scenario in $scenarios) {
        $status = Get-Content -LiteralPath $scenario.statusPath -Raw | ConvertFrom-Json
        if ($scenario.driver.ExitCode -ne 0 -or -not $status.passed) { throw 'A real workload cadence or native playback check failed.' }
        $samples = $scenario.samples
        $peak = ($samples | Measure-Object rssBytes -Maximum).Maximum
        $baseline = $samples[0].rssBytes; $final = $samples[$samples.Count - 1].rssBytes
        if ($final - $baseline -gt $protocol.finalGrowthLimitBytes) { throw 'Final memory growth exceeded the predeclared ceiling.' }
        $report.scenarios += [ordered]@{ name = $scenario.name; durationSeconds = $samples[$samples.Count - 1].elapsedSeconds
            rssBaselineBytes = $baseline; rssPeakBytes = $peak; rssFinalBytes = $final; limitBytes = $protocol.absoluteRssLimitBytes }
        $workloads += $status
    }
    $report.workloads = $workloads
    $report.rawSamplesSha256 = (Get-FileHash -LiteralPath (Join-Path $reportRoot 'memory-samples.json') -Algorithm SHA256).Hash.ToLowerInvariant()
    $report.passed = $true
    Write-Host 'Measured memory checks passed. Smoke runs remain ineligible for qualification.'
} catch {
    $report.failure = $_.Exception.Message
    throw
} finally {
    $report.finishedUtc = [DateTime]::UtcNow.ToString('o')
    try { Write-Json $report (Join-Path $reportRoot 'memory-report.json') } finally {
        [Environment]::SetEnvironmentVariable('PATH', $savedPath, 'Process')
        [Environment]::SetEnvironmentVariable('QTWEBENGINE_CHROMIUM_FLAGS', $savedFlags, 'Process')
        [Environment]::SetEnvironmentVariable('QTWEBENGINE_REMOTE_DEBUGGING', $savedDebug, 'Process')
        Stop-Owned
    }
}
