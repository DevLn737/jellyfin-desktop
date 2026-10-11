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

The source-pinned contract records the deployed Server/Web 12.2-syncplay.1 pair,
exact immutable commits, a Desktop contract revision and synthetic fixture
hashes. It uses the authorized read-only fallback: no separate server-agent API
handoff was available. The live image, public version and release manifest were
cross-checked. If a later handoff selects another pair, revise the contract and
repeat compatibility checks. RequiredHttpHeaders forwarding remains unsupported
because its origin/redirect guarantees are unspecified; header-dependent remote
sources need a scoped implementation or server-mediated URL. No server legacy-auth
setting or new endpoint is introduced by this client migration.

Release prerequisites:

1. Lock an officially released Qt 6.11.x/WebEngine pair with verified
   QTBUG-141377 fix evidence. A patch version or repository directory by itself
   is not proof. Qt6.12 and a GC workaround are outside this update.
2. Confirm the agreed Server/Web pair still matches the recorded contract and
   verify client behavior against it, including auth, external URLs and SyncPlay
   rate/errors. Agree drift tolerances for the qualification scenarios; the
   protocol's algorithm defaults are not a guarantee of maximum observed drift.
3. Run the native/WebEngine memory qualification and complete the single final
   acceptance checklist on one agreed build. Automated checks stay inside the
   agents' implementation work.
4. Publish a qualified `2.0.0-rc.N` prerelease with `latest=false`.
5. After the user's complete acceptance and client fixes, integrate into
   `master`, increment the three-part package version, and publish `v2.0.0`.

Qt release, memory, contract and stable acceptance checks are enforced by
`scripts/validate-build-manifest.py`; tag-only publication is in the Windows
Desktop workflow. Do not manufacture passing quality reports to bypass a gate.

The isolated `scripts/test-native-shell.ps1` probe verifies first navigation,
strict CSP, page reload, Unicode configuration and explicit localhost-only
DevTools. It copies the deployment into its own temporary profile and refuses
to interfere with an already running application. It does not replace hardware
playback, installation-upgrade or long-running memory qualification.

## One final acceptance

Record exact app commit/version, Qt version, manifest SHA256, Server/Web versions
and contract revision. Use the updated custom server as the primary target,
the old 10.11.11-syncplay.4 pair for regressions and ordinary Jellyfin 12.2 for
normal playback compatibility. Read [quality/README.md](../quality/README.md) for
the single acceptance matrix and evidence format. Server failures are reported
to the server agent; only confirmed client causes are fixed here.
