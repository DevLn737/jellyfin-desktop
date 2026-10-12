# Unified Windows update

The development branch is `codex/desktop-qt6`, based on the combined Discord and
SyncPlay commit `955a60349cfb6837d635af19e8dd192d7218109f`. Qt6 changes are ported
by subsystem with reference to upstream
`2cb4a4456fd29b5b62d825ef0e5df93ed6913328`; a repeated history merge would leave
the earlier tree reset in place. The target integration branch is `master`.

## Implemented client changes

- The rc.3 Windows Web interface uses software WebEngine by default to avoid
  reproduced rapid GPU-composition memory growth. Native video retains Qt Quick
  OpenGL and mpv hardware decoding. Shutdown saves state and requests quit once;
  hidden first native frames are consumed without blocking visibility delivery.
  See [the playback/composition investigation](windows-playback-composition-fix.md).
- Qt6/C++17, explicit persistent WebEngine profile and profile-level NativeShell,
  updated QML Controls/WebEngine APIs, native Windows taskbar integration.
- The rc.2 server chooser checks only the public API, acknowledges the saved
  server setting before navigation, and opens the actual server document.
  Removing the redundant cross-origin HTML fetch fixes false connection errors
  when reverse proxies permit API CORS but omit it on Web HTML. See
  [the connection fix and regression](server-connection-cors-fix.md).
- OpenGL chosen before GUI creation. One PlayerComponent consumes mpv events;
  the scenegraph renderer renders into a reusable FBO and composites below QML
  in `beforeRenderPassRecording`, with external command boundaries and restored
  framebuffer/viewport state. Renderer callbacks are coalesced; GL resources are
  released on the render thread. A context-loss marker preserves the selected
  video track across renderer recreation without selecting a previous file's track.
- Millisecond position/duration getters and events, checked per-mille native
  speed values, client rates 0.25..5 and temporary corrections 0.2..5.
  A stock SyncPlay group without the custom Web rate wrapper rejects a local
  rate change and shows a fixed warning; custom group rates remain server-owned.
- Asynchronous external audio loading, current-file request generations,
  duplicate/stale request rejection and independent participant selections.
  Preloaded metadata and tracks activate only at the matching START_FILE event.
  Windows media controls also retain the playing episode until that event.
- Native load acknowledgement and playback readiness are separate. The injected
  client waits for mpv's real decode/seek restart and a fresh millisecond position
  before reporting Ready, including paused operations. Client-local request IDs
  reject replies belonging to a previous file or superseded seek.
- Logical SyncPlay positions include progressive-transcode offsets exactly once
  through an adapter on the owned native Web wrapper. Server HLS/copytimestamps
  streams expose original media timestamps through per-file mpv options. Video
  keeps original decoder timestamps and external-track alignment. Music uses
  mpv's rebased clock internally and restores the probed demuxer start exactly
  once at the native boundary; otherwise mpv clamps paused audio-only HLS PTS
  to the shorter playlist duration. Preserved-stream durations expose their
  original timeline end. Ordinary direct transport streams keep default rebasing.
  The version-sensitive wrapper hook is recorded in the pinned API contract;
  an unavailable hook rejects an offset-dependent group load visibly.
- Existing Windows Discord IPC, Unicode, posters, progress and reconnect behavior.
  Its standard Discord timer continues during pause; the text shows the paused
  position, and resume/seek corrects timestamps. See the Discord feature document.
- Local bounded diagnostics and a separate static Tracy diagnostic build.
- Locked dependency archives, WiX7 MSI/Burn authoring, own-repository SemVer
  update notifications, shared-AppData ZIP defaults and explicit isolated markers.

Server/Web implementation, plugins and deployments remain the server agent's
responsibility. Server SSH reads are used only for compatibility evidence.

## Build and release status

`build-manifest.json` locks a prepared, unreleased Qt/WebEngine 6.11.3 candidate
for internal verification and the Windows toolchain. The release qualification
remains false. Local successful builds and
automated checks are development evidence, not final user acceptance.

The current rc.2 development packages were built from clean source
`33a3722039541cfd510999a4c55cb1600c4f8d52`, manifest SHA256
`c02f654aafa032db7baa698531dc5cce04399c6538bbdeec1b133660ff9a3d74`.
Both complete local pipelines, native/Node/Python regressions, production
offscreen renderer, PE imports, actual chooser/NativeShell/CSP/reload probes
and localhost Tracy passed. The installer was extracted read-only and bound
to the tested EXE/manifest; both portable ZIPs match their complete stage
inventories and the diagnostic EXE/PDB GUID and age 7 match. Current-version
artifact selection is tested with older cached archives present. The
[rc.2 package record](../quality/observations/desktop-development-33a3722.json)
retains exact identities and selected check outputs.
[Push CI](https://github.com/DevLn737/jellyfin-desktop/actions/runs/38183341537)
and [PR CI](https://github.com/DevLn737/jellyfin-desktop/actions/runs/38183344732)
also passed for this exact source; publication was skipped.

The [connection regression record](../quality/observations/desktop-connection-33a3722.json)
retains the old chooser's actual CORS failure and the passing rc.2 API-only
navigation, strict-CSP injection and native/JS saved-setting checks. A fresh
owned Qt profile also reached the affected real server's login page with
NativeShell/API/plugins working: 121 GET requests, zero other methods, no
authentication, media or group actions. This establishes prelogin connection,
not authenticated playback or installed upgrade continuity.

Current-source memory and diagnostics-performance qualification have not run
on `33a3722`. Aggregate checks, ready RC, release qualification and user
acceptance remain false. The earlier package and measurement records below
retain their original source/version/manifest identities. Later docs-only
commits do not change the source embedded in these rc.2 packages.

The retained rc.1 development-package checkpoint was built from clean commit
`289859f75ade45a3bd3cd8f53ba8c5b429062626`, with manifest SHA256
`af7e6e844bc858191fcce7b88ef3d211dc5fd5d9905c37ff181d64a7431b69fc`.
Both full normal and diagnostic pipelines, their native regression tests,
production offscreen renderer, staged PE import checks, actual NativeShell/CSP
probe and localhost Tracy probe completed successfully. Fourteen process-sampler
tests cover blocked collection, queued delivery, stop/restart, destruction,
counter identity, failures and the five added parent-ancestry cases.
Read-only Burn/MSI extraction binds the installer
to the tested EXE and manifest; both portable ZIPs match their complete staging
inventories. The diagnostic EXE/PDB GUID and age 5 match. Sanitized package and
operation identities are retained in
[the current development-build record](../quality/observations/desktop-development-289859f.json).
[Push CI](https://github.com/DevLn737/jellyfin-desktop/actions/runs/38173155985)
and [PR CI](https://github.com/DevLn737/jellyfin-desktop/actions/runs/38173159437)
also passed for that exact commit; publication was skipped.

These packages remain unqualified. Full-hour memory and diagnostics-performance
qualification have not run on `289859f`. The earlier failed memory observation
belongs to `6eeebfa`; the post-worker diagnostics measurement belongs to `ebd33bb`.
Neither establishes qualification for this later production change. The retained package
record has `checksPassed`, `readyForReleaseCandidate`, `releaseQualified` and
`userAcceptanceCompleted` all false. Later documentation/evidence commits do
not change the source identity embedded in these local packages.
The [earlier development-build record](../quality/observations/desktop-development-ebd33bb.json)
and its packages remain separate, with their original `ebd33bb` source identities.

The official Qt version endpoint was checked again at `2026-10-11T20:54:43Z`:
it still reports `6.11.3` with `released: false` and a planned date of October 13.
The [official Qt schedule](https://wiki.qt.io/Qt_6.11_Release) has the same planned
date; the [released-package directory](https://download.qt.io/official_releases/qt/6.11/)
currently stops at 6.11.2. This is a status observation, not release proof.

The completed internal memory observation at commit
`6eeebfa3298e68f452b58b47b14bbf0e7f69e97f` failed its SVG final-growth limit
by retaining 278.44 MiB above its baseline (limit 128 MiB). The native music
scenario completed with a 30.56 MiB reduction. A separate minimal QtQuick/WebEngine
comparison completed within its declared limits, with a higher initial baseline
and additional DOM polling; it cannot establish a Desktop-specific cause or
qualify the application. The complete evidence and limitations are in
[the memory observation](qt6113-memory-observation.md).

Three historical diagnostics observations also retain threshold-failing p95
pairs. Confirmed synchronous Windows process queries have now been moved off
the GUI thread, with one pending worker and stale-capture rejection. That
change was measured once on `ebd33bb` with the prospectively fixed off/on/on/off/
off/on protocol: 60,000 verified replies, thirteen actual clock probes at about
5 microseconds, and unchanged limits. CPU increases of 0.17166, 0.08398 and
0.14804 percentage points stayed below the two-point target. P95 increases were
1.28205%, **13.26531%** and 1.53062%; the second pair exceeded 5%, so the overall
synthetic criterion failed. The successful command exit means the measurement
completed, not that it passed. The new result and the three historical failures
remain separate; no causal explanation or successful qualification is asserted.
See [the retained diagnostics observations](../quality/diagnostics-performance/README.md).
An independent review then found a separate process-accounting edge: an old
orphan was rejected after PPID expansion, but its newer descendant could remain.
The sampler now validates each branch through accepted direct parents before
flattening it; five added tests cover the rejected ancestry, missing counters,
reused parents, valid branches and cycles. This correctness change is outside
the measured `ebd33bb` source and has no established relation to its p95 result.
The `289859f` build/runtime checks above do not supply a new performance measurement.
The separate pinned Web clearQueue defect
is described in [the Server/Web finding](server-web-clearqueue-finding.md).

The pinned libmpv directly imports `vulkan-1.dll`, including when playback uses
OpenGL. MSI and both portable variants therefore include the official LunarG
1.4.363.0 x64 loader and its unmodified license notice, with archive and payload
SHA256 locks. This makes startup independent of a driver's global Vulkan runtime.
The loader is updated with the application's dependencies; keeping an app-local
loader indefinitely can miss future driver-discovery fixes, as described in the
[Khronos loader documentation](https://github.com/KhronosGroup/Vulkan-Loader/blob/fa218055ff4948b159f31d43f800c808a13d51df/docs/LoaderApplicationInterface.md#bundling-the-loader-with-an-application).
An import-closure check requires non-Windows DLLs in the deployed package even
when a developer's System32 contains them. Runtime probes use a restricted PATH.

The final server-agent handoff confirms the deployed Server/Web 12.2-syncplay.1
pair and the immutable commits independently checked through read-only inventory
and the public version API. The archive SHA256 is
`98f1708c6ac758fec11877c471bc898b6a6246562e5c80e41acd4e857f619c6d`.
Its file hashes and supplied Web Git bundle were verified. The sanitized
`contracts/desktop-api-contract.json` records Desktop-derived revision
`desktop-handoff-12.2-syncplay.1-r1`; the handoff does not declare a separate
server API revision. Fixtures are source-derived synthetic data, not captured
production responses. The 21 passing server-agent browser checks establish Web
behavior only, not Windows native or user acceptance.

RequiredHttpHeaders forwarding remains unsupported
because its origin/redirect guarantees are unspecified; header-dependent remote
sources need a scoped implementation or server-mediated URL. No server legacy-auth
setting or new endpoint is introduced by this client migration.

Release prerequisites:

1. Lock an officially released Qt 6.11.x/WebEngine pair with verified
   QTBUG-141377 fix evidence. A patch version or repository directory by itself
   is not proof. Qt6.12 and a GC workaround are outside this update.
2. Confirm the agreed Server/Web pair still matches the recorded contract and
   verify client behavior against it, including auth, external URLs and SyncPlay
   rate/errors. The final handoff requires mixed Windows Desktop/Web drift below
   400 ms at 10, 15 and 20 seconds for 1, 1.25 and 1.5x with correction enabled
   and a cold second client. Measure actual logical media positions on a common
   clock; browser receipts and algorithm defaults do not prove native drift.
3. Run the native/WebEngine memory qualification and prepare the single final
   acceptance checklist for the integrated build. Automated checks stay inside
   the agents' implementation work.
4. Publish a qualified `2.0.0-rc.N` prerelease with `latest=false`.
5. After the user's complete acceptance and client fixes, integrate into
   `master`, increment the three-part package version, and publish `v2.0.0`.

Qt release, memory, contract and stable acceptance checks are enforced by
`scripts/validate-build-manifest.py`; tag-only publication is in the Windows
Desktop workflow. Do not manufacture passing quality reports to bypass a gate.

The isolated `scripts/test-native-shell.ps1` probe verifies first navigation,
strict CSP, page reload, Unicode configuration and explicit localhost-only
DevTools, diagnostics off at launch and native capture controls. A generated
silent WAV with null audio output exercises real mpv paused load/seek readiness
and rapid replacement. A compact synthetic music HLS fixture checks 30-second
original timestamps, paused seek, timeline duration and ordinary direct-TS
rebasing. This hidden app probe has no Qt render pass and cannot verify video.

`tests/render/run-windows.ps1` builds the production PlayerQuickItem/renderer
with a separate isolated mpv owner. Qt RenderControl supplies an offscreen GL
render pass using the locked software OpenGL DLL; no app or visible window is
launched. Synthetic video checks original paused load/seek timestamps, image
orientation, QML overlay composition, rectangle resizing, FBO reuse and context
invalidation. The component binding is separate from rendering so this harness
uses the actual renderer without a second production mpv event consumer. It does
not establish hardware-driver, minimize/restore or PlayerComponent recovery
behavior.

These probes use no server media or account. The NativeShell probe copies the
deployment into its own temporary profile and refuses
to interfere with an already running application. It does not replace hardware
playback, installation-upgrade or long-running memory qualification.

## Existing installation and rollback

Before installing the qualified candidate, close the app and privately back up
the existing native settings/storage and WebEngine profile. The default Windows
locations are `%LOCALAPPDATA%/JellyfinMediaPlayer` (including
`jellyfinmediaplayer.conf` and `storage.json`) and
`%LOCALAPPDATA%/Jellyfin Media Player/QtWebEngine/Default`. Retain any existing
Roaming profile as well. The default ZIP uses these same locations; an explicit
`portable` or `portable.txt` marker selects adjacent `data` and `cache` instead.
Record the installed package version and executable checksum before upgrading.
Profile backups can contain login secrets and must never enter Git, release
assets or diagnostics exports.

For rollback, close every app process, restore the previous installed package
and its matching profile snapshot before launching it. Do not run the old and
new apps against the same profile simultaneously. Test this procedure with the
installer upgrade scenarios during the single final acceptance.

## One final acceptance

Record exact app commit/version, Qt version, manifest SHA256, Server/Web versions
and contract revision. Use the updated custom server as the primary target,
the old 10.11.11-syncplay.4 pair for regressions and ordinary Jellyfin 12.2 for
normal playback compatibility. Read [quality/README.md](../quality/README.md) for
the single acceptance matrix and evidence format. Server failures are reported
to the server agent; only confirmed client causes are fixed here.
