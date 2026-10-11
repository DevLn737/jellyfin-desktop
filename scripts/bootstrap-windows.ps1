[CmdletBinding()]
param(
    [string]$BuildRoot = 'build',
    [switch]$SkipInstallerTools,
    [switch]$SkipMsvcImport
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$sourceRoot = Split-Path $PSScriptRoot -Parent
$manifest = Get-Content (Join-Path $sourceRoot 'build-manifest.json') -Raw | ConvertFrom-Json
$buildDirectory = [IO.Path]::GetFullPath((Join-Path $sourceRoot $BuildRoot))
if (-not $buildDirectory.StartsWith($sourceRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'BuildRoot must be a subdirectory of this checkout.'
}
$archiveDirectory = Join-Path $buildDirectory 'dependency-archives'
$toolchainDirectory = Join-Path $buildDirectory 'toolchain'
New-Item -ItemType Directory -Force -Path $archiveDirectory, $toolchainDirectory | Out-Null
$sevenZip = (Get-Command 7z.exe -ErrorAction Stop).Source

function Get-LockedArchive($archive) {
    if ($archive.sha256 -notmatch '^[a-f0-9]{64}$' -or ([uri]$archive.url).Scheme -ne 'https') {
        throw 'Every dependency must have an HTTPS URL and a SHA256 lock.'
    }
    if ([IO.Path]::GetFileName($archive.name) -ne $archive.name) { throw 'Invalid archive filename' }
    $destination = Join-Path $archiveDirectory $archive.name
    if (-not (Test-Path -LiteralPath $destination)) {
        Write-Host "Downloading $($archive.name)"
        Invoke-WebRequest -Uri $archive.url -OutFile ($destination + '.partial') -UseBasicParsing
        Move-Item -LiteralPath ($destination + '.partial') -Destination $destination -Force
    }
    $actual = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $archive.sha256) { throw "SHA256 mismatch: $($archive.name). Remove that cached file and retry." }
    return $destination
}

function Expand-LockedArchive($archive, [string]$destination) {
    $path = Get-LockedArchive $archive
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    $stamp = Join-Path $destination ('.' + $archive.name + '.sha256')
    if ((Test-Path -LiteralPath $stamp) -and ([IO.File]::ReadAllText($stamp).Trim() -eq $archive.sha256)) { return }
    & $sevenZip x -y -bso0 -bsp0 "-o$destination" $path
    if ($LASTEXITCODE -ne 0) { throw "7-Zip extraction failed for $($archive.name)" }
    [IO.File]::WriteAllText($stamp, $archive.sha256)
}

function Copy-ChangedDlls([string]$directory, [string]$destination) {
    foreach ($dll in (Get-ChildItem -LiteralPath $directory -Filter '*.dll' -File)) {
        $target = Join-Path $destination $dll.Name
        if ((Test-Path -LiteralPath $target) -and
            (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -eq (Get-FileHash -LiteralPath $dll.FullName -Algorithm SHA256).Hash) {
            continue
        }
        Copy-Item -LiteralPath $dll.FullName -Destination $target -Force
    }
}

function Import-MsvcEnvironment {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio 2022 C++ Build Tools with the Windows SDK first.' }
    $vsInstallation = & $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsInstallation) { throw 'Visual Studio 2022 x64 C++ tools were not found.' }
    Import-Module (Join-Path $vsInstallation 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    $developerArguments = "-vcvars_ver=$($manifest.windows.msvcToolsVersion) -winsdk=$($manifest.windows.windowsSdkVersion)"
    Enter-VsDevShell -VsInstallPath $vsInstallation -SkipAutomaticLocation -Arch amd64 -HostArch amd64 -DevCmdArguments $developerArguments | Out-Null
    if ($env:VCToolsVersion.TrimEnd('\') -ne $manifest.windows.msvcToolsVersion -or
        $env:WindowsSDKVersion.TrimEnd('\') -ne $manifest.windows.windowsSdkVersion) {
        throw 'The installed MSVC or Windows SDK does not match build-manifest.json.'
    }
    return $vsInstallation
}

$vsDirectory = $null
if (-not $SkipMsvcImport) { $vsDirectory = Import-MsvcEnvironment }
$qtRoot = Join-Path $toolchainDirectory ("qt\" + $manifest.qt.version + '\msvc2022_64')
foreach ($archive in $manifest.qt.archives) { Expand-LockedArchive $archive $qtRoot }
Copy-ChangedDlls $qtRoot (Join-Path $qtRoot 'bin')
# Qt 6.11 uses separate nested desktop and WebEngine repositories. Archives are
# laid out directly under the prefix; qt.conf makes qmake/windeployqt relocatable.
[IO.File]::WriteAllText((Join-Path $qtRoot 'bin\qt.conf'), "[Paths]`nPrefix=..`n")
if (-not (Test-Path (Join-Path $qtRoot 'lib\cmake\Qt6WebEngineQuick\Qt6WebEngineQuickConfig.cmake'))) {
    throw 'The locked Qt deployment is missing Qt WebEngine Quick.'
}
$cmakeRoot = Join-Path $toolchainDirectory 'cmake'
Expand-LockedArchive $manifest.tools.cmake $cmakeRoot
$cmakeExe = Join-Path $cmakeRoot ("cmake-" + $manifest.tools.cmake.version + '-windows-x86_64\bin\cmake.exe')
$ninjaRoot = Join-Path $toolchainDirectory 'ninja'
Expand-LockedArchive $manifest.tools.ninja $ninjaRoot
$nodeRoot = Join-Path $toolchainDirectory 'node'
Expand-LockedArchive $manifest.tools.node $nodeRoot
$nodeExe = Join-Path $nodeRoot ("node-v" + $manifest.tools.node.version + '-win-x64\node.exe')
$mpvRoot = Join-Path $toolchainDirectory 'mpv'
Expand-LockedArchive $manifest.mpv $mpvRoot
$vulkanRoot = Join-Path (Join-Path $toolchainDirectory 'vulkan') $manifest.tools.vulkanRuntime.version
Expand-LockedArchive $manifest.tools.vulkanRuntime $vulkanRoot
foreach ($payload in @($manifest.tools.vulkanRuntime.runtimeDll, $manifest.tools.vulkanRuntime.license)) {
    $payloadPath = [IO.Path]::GetFullPath((Join-Path $vulkanRoot $payload.path))
    if (-not $payloadPath.StartsWith($vulkanRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $payloadPath) -or
        (Get-Item -LiteralPath $payloadPath).Length -ne $payload.bytes -or
        (Get-FileHash -LiteralPath $payloadPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $payload.sha256) {
        throw "The official Vulkan runtime payload differs from its lock: $($payload.name)"
    }
}
$vulkanRuntime = Join-Path $vulkanRoot $manifest.tools.vulkanRuntime.runtimeDll.path
$vulkanLicense = Join-Path $vulkanRoot $manifest.tools.vulkanRuntime.license.path
$mpvImportLibrary = Join-Path $mpvRoot 'libmpv-2.lib'
if (-not $SkipMsvcImport) {
    $exportLines = & dumpbin.exe /nologo /exports (Join-Path $mpvRoot 'libmpv-2.dll')
    if ($LASTEXITCODE -ne 0) { throw 'dumpbin could not read libmpv exports' }
    $exports = @($exportLines | ForEach-Object { if ($_ -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(mpv_[A-Za-z0-9_]+)\s*$') { $Matches[1] } })
    if ($exports.Count -lt 20) { throw 'Unexpected libmpv export table' }
    $definition = Join-Path $mpvRoot 'libmpv-2.def'
    [IO.File]::WriteAllText($definition, "LIBRARY libmpv-2.dll`r`nEXPORTS`r`n" + ($exports -join "`r`n") + "`r`n", [Text.Encoding]::ASCII)
    & lib.exe /nologo "/def:$definition" "/out:$mpvImportLibrary" /MACHINE:X64
    if ($LASTEXITCODE -ne 0) { throw 'MSVC import library creation failed' }
}
$runtimeDirectory = Join-Path $toolchainDirectory 'vcruntime'
New-Item -ItemType Directory -Force -Path $runtimeDirectory | Out-Null
$redistExe = Get-LockedArchive $manifest.tools.vcRedist
$tracyArchive = Get-LockedArchive $manifest.tracy
if ($vsDirectory) {
    $crtDirectory = Get-ChildItem -LiteralPath (Join-Path $vsDirectory 'VC\Redist\MSVC') -Directory |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+' } |
        Sort-Object { [version]$_.Name } -Descending |
        ForEach-Object { Join-Path $_.FullName 'x64\Microsoft.VC143.CRT' } |
        Where-Object {
            $candidateRuntime = Join-Path $_ 'vcruntime140.dll'
            (Test-Path -LiteralPath $candidateRuntime) -and
                [version][Diagnostics.FileVersionInfo]::GetVersionInfo($candidateRuntime).FileVersion -eq [version]$manifest.tools.vcRedist.version
        } | Select-Object -First 1
    if (-not $crtDirectory) { throw 'The exact pinned MSVC x64 redistributable DLLs were not found in Visual Studio.' }
    Copy-ChangedDlls $crtDirectory $runtimeDirectory
    $crtVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $runtimeDirectory 'vcruntime140.dll')).FileVersion
    foreach ($dll in (Get-ChildItem -LiteralPath $runtimeDirectory -Filter '*.dll' -File)) {
        $crtVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($dll.FullName).FileVersion
        if ([version]$crtVersion -ne [version]$manifest.tools.vcRedist.version) {
            throw "The MSVC runtime does not match the pinned redistributable: $($dll.Name) $crtVersion"
        }
        $lock = $manifest.tools.vcRedist.runtimeDlls.PSObject.Properties[$dll.Name]
        if (-not $lock -or (Get-FileHash -LiteralPath $dll.FullName -Algorithm SHA256).Hash.ToLowerInvariant() -ne $lock.Value) {
            throw "The MSVC runtime DLL differs from the verified Microsoft redistributable payload: $($dll.Name)"
        }
    }
    foreach ($lock in $manifest.tools.vcRedist.runtimeDlls.PSObject.Properties) {
        if (-not (Test-Path -LiteralPath (Join-Path $runtimeDirectory $lock.Name))) {
            throw "The pinned MSVC runtime DLL is missing: $($lock.Name)"
        }
    }
    # Qt tools must find the matching CRT beside their executable. Windows can
    # choose an older System32 CRT before PATH, which breaks QML tools at startup.
    Copy-ChangedDlls $runtimeDirectory (Join-Path $qtRoot 'bin')
}
$wixRoot = Join-Path $toolchainDirectory 'wix'
$wixExe = ''
$wixUtil = ''
$wixBal = ''
if (-not $SkipInstallerTools) {
    $wixMsi = Get-LockedArchive $manifest.tools.wix
    $found = @(Get-ChildItem -LiteralPath $wixRoot -Recurse -Filter wix.exe -ErrorAction SilentlyContinue)
    if ($found.Count -eq 0) {
        New-Item -ItemType Directory -Force -Path $wixRoot | Out-Null
        # Administrative extraction writes only this build directory, without
        # registering a system-wide WiX installation or accepting its EULA.
        $wixLog = Join-Path $wixRoot 'extract.log'
        $process = Start-Process -FilePath 'msiexec.exe' -WindowStyle Hidden -Wait -PassThru -ArgumentList @('/a', "`"$wixMsi`"", '/qn', "TARGETDIR=`"$wixRoot`"", '/L*v', "`"$wixLog`"")
        if ($process.ExitCode -notin @(0, 3010)) { throw "WiX administrative extraction failed ($($process.ExitCode)); see $wixLog" }
        $found = @(Get-ChildItem -LiteralPath $wixRoot -Recurse -Filter wix.exe)
        if ($found.Count -ne 1) { throw 'Expected exactly one extracted wix.exe' }
    }
    if ($found.Count -ne 1) { throw 'Expected exactly one extracted wix.exe' }
    $wixExe = $found[0].FullName
    foreach ($extension in $manifest.wix.extensions) {
        $extensionRoot = Join-Path $wixRoot ('extensions\' + $extension.id)
        Expand-LockedArchive $extension $extensionRoot
        $extensionDll = Join-Path $extensionRoot ("wixext7\" + $extension.id + '.dll')
        if ($extension.id -eq 'WixToolset.Util.wixext') { $wixUtil = $extensionDll }
        if ($extension.id -eq 'WixToolset.BootstrapperApplications.wixext') { $wixBal = $extensionDll }
    }
}

$state = [ordered]@{
    qtRoot = $qtRoot; cmakeExe = $cmakeExe; ninjaExe = Join-Path $ninjaRoot 'ninja.exe';
    nodeExe = $nodeExe;
    tracyArchive = $tracyArchive;
    mpvRoot = $mpvRoot; mpvLibrary = $mpvImportLibrary; mpvRuntime = Join-Path $mpvRoot 'libmpv-2.dll';
    vulkanRuntime = $vulkanRuntime; vulkanLicense = $vulkanLicense;
    vcRedistExe = $redistExe; runtimeDirectory = $runtimeDirectory; wixExe = $wixExe;
    wixUtilExtension = $wixUtil; wixBalExtension = $wixBal;
    appVersion = $manifest.appVersion; packageVersion = $manifest.packageVersion
}
foreach ($key in @($state.Keys)) {
    if ($state[$key] -is [string] -and [IO.Path]::IsPathRooted($state[$key])) {
        $state[$key] = $state[$key].Replace('\', '/')
    }
}
$stateFile = Join-Path $buildDirectory 'windows-toolchain.json'
$state | ConvertTo-Json | Set-Content -LiteralPath $stateFile -Encoding utf8
$env:PATH = (Join-Path $qtRoot 'bin') + ';' + (Split-Path $cmakeExe) + ';' + $ninjaRoot + ';' + (Split-Path $nodeExe) + ';' + $env:PATH
Write-Host "Locked toolchain ready: $stateFile"
if ($env:GITHUB_ENV) {
    foreach ($entry in $state.GetEnumerator()) { "$($entry.Key)=$($entry.Value)" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8 }
}
