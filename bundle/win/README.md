Windows x64 distributions preserve the existing Jellyfin Media Player identity:
`JellyfinMediaPlayer.exe`, MSI/Burn UpgradeCode
`abea6faf-13ea-465f-b2bd-a24b5524e98b`, the installation directory and the
`Software\Jellyfin\Jellyfin Media Player` registry key. Installed settings and
Qt WebEngine profiles retain their existing AppData paths. Windows 10 1903 and
Windows 11 are supported; x86 and old Windows prerequisites are removed.

Run `./scripts/build-windows.ps1` from a PowerShell terminal with Visual Studio
2022 C++ Build Tools 14.44.35207 and Windows SDK 10.0.26100.0 installed. The script
imports the x64 compiler environment, verifies every downloaded archive against
`build-manifest.json`, builds, runs automated feature checks, and writes the
installer and portable ZIP under `build/windows/release/assets`. It extracts
the WiX CLI locally with MSI administrative extraction. Package builds explicitly
pass `-acceptEula wix7`; CMake's `WIX_ACCEPT_EULA` option defaults to OFF, and the
reviewed build script/CI opts in. WiX's published binary EULA is available at
https://github.com/wixtoolset/wix/blob/v7.0.0/OSMFEULA.txt.

`./scripts/build-windows.ps1 -Diagnostic` creates a separate RelWithDebInfo
portable Tracy distribution and a matching `diagnostic-symbols.zip` with its
PDB, source commit and manifest checksum. The diagnostic executable never
replaces the normal installer.
Portable ZIPs include the Microsoft x64 CRT and retain existing AppData by
default. Add `-IsolatedPortable` to include an explicit `portable` marker; that
archive stores its profile in `data/` and cache in `cache/` beside the executable.
Old unmarked ZIPs continue using installed AppData. Keep the `data/`
directory when replacing a portable version; extract the new archive over the
old application files. Removing the marker restores the installed profile mode.

The application uses its SemVer, initially `2.0.0-rc.1`. Windows Installer uses a
separate three-part `packageVersion`, initially `2.0.1`; increment it for every
published RC and stable package, including promotion of an RC to stable. MSI
major upgrades remove the previous package after `InstallInitialize`, inside
the rollback transaction. Installer repair, downgrade, old-version upgrade,
rollback and preserved login/settings remain part of final acceptance on a clean
Windows test machine.

CI builds branches and pull requests. Only a pushed `v<appVersion>` tag may
publish direct EXE, ordinary portable ZIP, diagnostic portable ZIP, the manifest
and SHA256SUMS. RCs are prereleases and never GitHub Latest. Stable tags must be
on `master` and require committed final user acceptance. Published packages are
immutable; the monotonic package-version check rejects replacing a release.

Release qualification is intentionally incomplete for the development Qt
6.11.2 lock. A tag is rejected until the manifest records a released Qt version
with official release/fix evidence for QTBUG-141377 and a committed memory report
for the same app version, manifest SHA256 and tested source commit. The report
requires at least two scenarios (navigation and playback), each at least 3600
seconds, with finite positive `rssBaselineBytes`, `rssPeakBytes`, `rssFinalBytes`
and `limitBytes`; observed peak/final RSS must stay within the agreed limit.
The tested commit must be an ancestor of the release commit, with subsequent
changes restricted to `docs/` and `quality/` evidence. Release tags also require
`contracts/desktop-api-contract.json` with the final Server/Web source pins and
explicit Desktop contract revision. Source-derived synthetic fixtures are
accepted as protocol evidence when their revision, source commits and byte
checksum match the contract; they must retain `capturedFromLiveServer: false`.
For this verified server-agent handoff, the recorded handoff hashes and receipt
flags must also remain consistent. An independently source-reviewed contract
can serve as a fallback without claiming receipt of a server-agent handoff.
Server/Web browser acceptance establishes its browser scope only. RC packages
remain complete candidates for the user's single native application acceptance.
Stable
acceptance is recorded in `quality/desktop-acceptance.json` with
`accepted: true`, `acceptedBy: "user"`, `appVersion`, `qtVersion`, `buildCommit`
and `buildManifestSha256`, after the actual user test. That receipt also records
`nativeAcceptancePerformed: true`, `apiRevision`, `serverCommit` and `webCommit`
matching the final contract. Its `syncPlay` object identifies
`composition: "windows-desktop-and-web"`, `correctionEnabled: true`,
`coldSecondClient: true`, and `mixedClientCases` containing `pause-seek`,
`rejoin-reconnect`, `legacy-409`, `personal-speed-restoration`,
`independent-audio-subtitles`, `direct-play` and `hls`. Record actual
`driftSamples` with `baseRate`, `afterSeconds` and `absoluteDriftMilliseconds`:
the final contract requires all 1x/1.25x/1.5x combinations at 10/15/20 seconds,
each strictly below 400 ms. A browser-only receipt or synthetic passing fixture
does not satisfy this native acceptance gate. A tested and accepted RC
can be promoted to the same stable core version with only `appVersion` and the
monotonically increasing `packageVersion` changed. The gate verifies the tested
commit's original manifest checksum and all other manifest fields, and rejects
any intervening application, dependency or API-contract change. This preserves
one final user acceptance while rebuilding the stable version resource. A code
or contract change requires new qualification and acceptance evidence.

To review new official archives, run
`python scripts/lock-windows-dependencies.py --qt-version 6.11.X`. This verifies
official Qt SHA1 metadata before calculating SHA256 and writes an ignored
candidate manifest. Review the candidate and qualify the released patch before
replacing the canonical lock. The generator does not assert a passing release.
