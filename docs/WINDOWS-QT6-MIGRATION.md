# Unified Windows update

The development branch is `codex/desktop-qt6`, based on the combined Discord and
SyncPlay commit `955a60349cfb6837d635af19e8dd192d7218109f`. Qt6 changes are ported
by subsystem with reference to upstream
`2cb4a4456fd29b5b62d825ef0e5df93ed6913328`; a repeated history merge would leave
the earlier tree reset in place. The target integration branch is `master`.

## Implemented client changes

- Qt6/C++17, explicit persistent WebEngine profile and profile-level NativeShell,
  updated QML Controls/WebEngine APIs, native Windows taskbar integration.
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

`build-manifest.json` locks development Qt/WebEngine 6.11.2 and the Windows
toolchain. The release qualification remains false. Local successful builds and
automated checks are development evidence, not final user acceptance.

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
