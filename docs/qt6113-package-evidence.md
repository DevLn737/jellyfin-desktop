# Qt 6.11.3 internal candidate evidence

This lock is a **prepared, unreleased candidate for internal verification**.
It is not a published Desktop RC, a qualified Qt release, or user acceptance.
`build-manifest.json` keeps `qt.qualification.qualified=false`; the intended
memory report and protocol paths are not evidence of a passing run.

## Official release status at lock

At 2026-10-11 15:58:42 UTC, [Qt's release-version metadata](https://bugreports.qt.io/rest/api/2/version/16945)
reported `name=6.11.3`, `released=false`, and `releaseDate=2026-10-13`.
The raw 300-byte JSON had SHA256
`175ef37e4c8b49bb75071b5f68a44ab305383ee85c4daf148f78c693edac043a`.
[Release-team minutes](https://lists.qt-project.org/pipermail/development/2026-October/047502.html)
describe frozen release content, created packages, and ongoing testing. The
[official 6.11 release directory](https://download.qt.io/official_releases/qt/6.11/)
still lists 6.11.2 as its latest release.

The general Qt documentation already names 6.11.3, and the online SDK packages
are available. Those facts do not override the explicit unreleased status.
Recheck official release status and package identities before any release gate
is opened; these observations are historical evidence, not a live assertion.

## Downloaded package identity

The lock generator fetched only the twelve Qt archives used by this Desktop
build. It verified each against its official `.sha1` file, then recorded the
computed SHA256 and byte count in the manifest. Source and PDB archives were
not downloaded. Qt cache files are under
`build/dependency-archives/qt-6.11.3/`; the existing 6.11.2 cache is preserved.

| Official metadata | SHA256 of the downloaded metadata bytes |
| --- | --- |
| [Desktop MSVC x64 Updates.xml](https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6113/qt6_6113_msvc2022_64/Updates.xml) | `4545b858d3bc07285de11d8cdd0edb2e87753d94a6adf517730440e7bdac21c8` |
| [WebEngine MSVC x64 Updates.xml](https://download.qt.io/online/qtsdkrepository/windows_x86/extensions/qtwebengine/6113/msvc2022_64/Updates.xml) | `edc321ffae6ab81a88bb78fc7ca139ecc713b498344f5df10918147bf97730de` |
| [WebEngine common main Updates.xml](https://download.qt.io/online/qtsdkrepository/windows_x86/extensions/qtwebengine/6113/main/Updates.xml) | `561314b0bbb0810598ab121833b00746c0862cc423d3ed291fa15345a0a4af2b` |

The Desktop package version is `6.11.3-0-202609240253`; WebEngine is
`6.11.3-0-202609240437`. The latter archive's official SHA1 is
`a19c2dc15864fe042e93adc0e73d42f429a83c1d`, verified against the archive's
official `.sha1` sidecar and independently computed from the downloaded bytes.
Its computed SHA256 is
`d77346ed46dd42e173f1ca2682c896309645ee90adaf8f85911af6905ff5c17c`.
The `PackageUpdate/SHA1` metadata value is not used as the archive checksum.

Selected binaries were extracted only into ignored `build/qt6113/payload/`.
All four report FileVersion and ProductVersion `6.11.3.0`:

| Payload | Bytes | SHA256 |
| --- | ---: | --- |
| `bin/Qt6Core.dll` | 10370872 | `b20757be279b4e074c89be1f183a40975bf867b9a2a4e0ca4ea34a95fe2bb39e` |
| `bin/Qt6WebEngineCore.dll` | 203763000 | `7461c85b35347e86ad563f32bec83b503f5fefdc91898dab25a2db4504ca3b0b` |
| `bin/Qt6WebEngineQuick.dll` | 686904 | `ff3c4e088c9e8d4a953b550224d185c85e9d6c745f342560c14f9430fe66b0ac` |
| `bin/QtWebEngineProcess.exe` | 802104 | `e6590c760fd9792081b627df71704c15acda4b19d581eb3efce921cda93b608f` |

All application versions, non-Qt tool locks, CRT/Vulkan payload locks, libmpv,
Tracy, and WiX identities were preserved exactly during this Qt refresh.

## Advertised source and exact fix ancestry

The common WebEngine main package advertises the Qt5 source SHA; the MSVC x64
binary package metadata does not advertise a separate source SHA. Its source
closure is:

1. [Qt5 `7886592e2e3ea2fd9a9b126b6a99b9e8d99f02ca`](https://code.qt.io/cgit/qt/qt5.git/tree/?id=7886592e2e3ea2fd9a9b126b6a99b9e8d99f02ca)
   pins QtWebEngine `8c73295524f3ce6a2e10ad2f71141c695376c876`.
2. [That QtWebEngine source tree](https://code.qt.io/cgit/qt/qtwebengine.git/tree/src?id=8c73295524f3ce6a2e10ad2f71141c695376c876)
   pins Chromium `7927ce61b5a302a3da8172eefbe260ec17484bcd`.
3. [The September 22 Chromium roll](https://code.qt.io/cgit/qt/qtwebengine.git/commit/?id=92911e07972dbb8fe151ed62ddb14f44503d720d)
   lists both relevant Windows changes.

An independent walk of the exact parent hashes using the
[official Gerrit commit endpoint](https://codereview.qt-project.org/projects/qt%2Fqtwebengine-chromium/commits/7927ce61b5a302a3da8172eefbe260ec17484bcd)
reached `9b162a9269cb8913c792dd6c22d222330a6fdb03` after thirty parent
edges and `62f90830c0750182d2b40be60e2e5f9c2f31c4c9` after thirty-five.
Each inspected commit had one parent.

[Change 770837](https://codereview.qt-project.org/c/qt/qtwebengine-chromium/+/770837)
merged September 14 and changes V8 write barriers from `true` to `is_win`.
[Change 772657](https://codereview.qt-project.org/c/qt/qtwebengine-chromium/+/772657)
merged September 21, changes `is_win` to `false`, and explicitly fixes
QTBUG-141377. [The pinned V8 settings](https://code.qt.io/cgit/qt/qtwebengine-chromium.git/plain/chromium/v8/BUILD.gn?id=7927ce61b5a302a3da8172eefbe260ec17484bcd)
confirm `v8_disable_write_barriers=false`, including on Windows.

For comparison, the previous 6.11.2 common package advertised Qt5
`713a36536903d172f9e6737584d428753c119496`, pinning WebEngine
`a33fa2a897e5ee58e385b3f88dc247d99fca56db` and Chromium
`5170777d28bee1ce92cc693a0dbf2ad01492e5cf`, whose
[V8 settings](https://code.qt.io/cgit/qt/qtwebengine-chromium.git/plain/chromium/v8/BUILD.gn?id=5170777d28bee1ce92cc693a0dbf2ad01492e5cf)
set `v8_disable_write_barriers=true`.

## Binary SBOM source linkage

The locked MSVC x64 WebEngine archive also contains producer build metadata,
`sbom/qtwebengine-6.11.3.spdx.json`. Its 519,828 bytes have SHA256
`ffd8df951bdbd57a1dfcd775995029ba36fe2afb7e2cc28e65fe8315f828b4b8`.
Selected extraction from the already downloaded, checksum-verified archive
matches the installed SDK SBOM byte for byte. No source or symbol archive was
downloaded for this check.

The SBOM's `WebEngineCore` package identifies its source as
`git://code.qt.io/qt/qtwebengine.git@8c73295524f3ce6a2e10ad2f71141c695376c876`.
A `CONTAINS` relationship links that package to `./bin/Qt6WebEngineCore.dll`.
The producer emits the same relationship twice; both entries identify the same
package and file. This adds a binary-package source statement to the common
main-package source closure described above.

The SBOM records SHA1 `ae4c51d6dd08702947ed8a66a4912801ff3c8238` for the DLL.
The signed, deployed DLL has SHA1
`0f14b1b588b7507ceafab512bc3e042b198409c0` and the previously locked SHA256
`7461c85b35347e86ad563f32bec83b503f5fefdc91898dab25a2db4504ca3b0b`.
The difference is accounted for exactly by signing-related PE bytes. A read-only
hash comparison zeroed the PE checksum and security-directory entry, then
excluded the appended 12,088-byte certificate table at offset 203,750,912 from
the 203,763,000-byte signed file. Every other byte was retained; no alignment
padding was removed. The resulting SHA1 exactly matches the SBOM's DLL entry.
The original signed file was never rewritten. This comparison reconstructs a
checksum identity; it is not an Authenticode trust-chain verification.

Thus the actual deployed binary, its producer SBOM, and the source commit form
one checksum-backed chain. That source commit pins the Chromium revision
containing the write-barrier fixes already traced above. The check does not
disassemble compiled V8 write barriers or independently inspect generated GN
overrides, and it does not establish memory behavior or official release status.

The reviewed additional source/checksum facts are retained in
[quality/observations/qt6113-additional-package-evidence.json](../quality/observations/qt6113-additional-package-evidence.json).
They are historical evidence alongside the unchanged tested manifest, rather
than a dependency-lock change after measurement.

## Verification limits

Official package metadata, exact Git ancestry, archive checksums, DLL versions
and the producer SBOM comparison identify the candidate and its declared source.
They do not independently audit compiled V8 code, generated GN overrides,
Authenticode trust or the application's memory behavior.

The actual full-hour Desktop run at commit `6eeebfa3298e68f452b58b47b14bbf0e7f69e97f` failed its declared SVG
growth ceiling: +278.44 MiB against 128 MiB; the native-music
scenario changed by -30.56 MiB. See
[the failed-run evidence](qt6113-memory-observation.md). The Qt qualification
flag stays false. This result is separate from official release status and
does not by itself identify the retained allocations or establish a Qt
regression. No Qt 6.12 upgrade or forced-GC workaround is involved.
