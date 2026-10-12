# Windows playback and shutdown regression

## RC4: retained opaque WebEngine background

RC3's synthetic overlay check missed the real Web client's transition from an
opaque detail page to the transparent video OSD. In the failing transition,
WebEngine's public background property was already transparent, but its software
compositor image remained opaque RGB (16,16,16). Hiding WebEngine revealed the
correct native frame. A Web-only screenshot was transparent, so that screenshot
alone was not a valid check of the composed application window.

The client now notifies native code once, after OSD navigation and two animation
frames (also for the miniplayer). Native code changes the background to black
and back to transparent in the same GUI callback. Qt's setter otherwise skips
an unchanged value and leaves Chromium's retained content background untouched.
Delayed navigation/paint callbacks are discarded after stop, file replacement
or dialog destruction. Native calls after stop and during window shutdown are
also ignored. No periodic reset or private Qt API is used.

The native compositor additionally disables inherited face culling and unbinds
an inherited sampler before drawing the video texture. Renderer tests poison
both states, including a sampler LOD that would otherwise sample a missing mip.

The initial production-code check used the actual served Jellyfin Web OSD,
local synthetic media, and an isolated copy of the existing browser profile.
Remote writes and WebSockets were blocked. Native video remained visible under
the OSD; actual slider bubbles appeared and their old rectangles returned to
the exact baseline after movement/dismissal. Active-window close took 470 ms.
The original browser profile was unchanged; cloned credentials, raw logs and
screenshots were removed after extracting numeric results. This is a bounded
regression check, not the hour-long memory or multi-client SyncPlay acceptance.

The tracked synthetic composition check now requires a moving native marker,
an opaque-page transition, and tooltip movement/removal. It checks pixels from
the composed native window instead of accepting DOM transparency as success.

## Earlier RC3 memory and shutdown correction

The rc.2 installed executable and six main Qt/mpv DLLs matched the packaged
`33a3722039541cfd510999a4c55cb1600c4f8d52` build. The reported rapid memory growth
was reproduced on Windows 11 with an AMD Radeon RX 9060 XT, using a fresh,
isolated portable profile and local synthetic media. No server login, real
media or SyncPlay group was used.

A transparent animated HTML overlay, including a small backdrop blur, caused
the Desktop process to exceed 2 GiB private memory in approximately 3–6 seconds.
The same animation also reproduced it while idle with the native video renderer
uninitialized. This rules out video decoding and the native mpv FBO as necessary
conditions for that reproduction. It does not identify an exact Qt/driver
allocation or reproduce every detail of the user's served Web interface.

Controlled comparisons retained the actual old executable unless otherwise
specified. The observations are short investigations, not release qualification:

| Comparison | Result |
| --- | --- |
| Default GPU WebEngine, synthetic 1080p60 video and animated overlay | 2,232,750,080 bytes within 3.21 seconds; memory guard stopped the test |
| Animated overlay, idle, native renderer uninitialized | 2,169,298,944 bytes; guard stopped the test |
| GPU rasterization disabled, GPU composition retained | 2,182,004,736 bytes; guard stopped the test |
| Additional QML layer removed in an experimental executable | Rapid growth remained; this change was reverted |
| Chromium ANGLE OpenGL override | Shared-image/context-loss errors; DOM command completion was not accepted as display success |
| Existing software WebEngine switch, same video/animation | Four playback cycles over 16 seconds completed; peak 710,402,048 bytes, final 593,367,040 bytes |

The software comparison retained AMD OpenGL rendering and hardware-copy mpv
decoding. Windows now selects software WebEngine by default before initialization.
The Qt Quick OpenGL backend and native video hardware-decoding setting remain
independent. The old forced GPU-rasterization flag was removed. Conflicting
inherited Chromium backend flags are normalized so they cannot silently select
a hardware transport in software mode. `--webengine-gpu` explicitly opts into
the GPU Web interface for investigation; combining it with `--disable-gpu` fails.
Linux retains its existing software WebEngine default.

The close delay was reproduced separately: native window close took roughly
6–7.5 seconds, with more than 600 geometry saves. `aboutToQuit` called the same
handler that requested `quit()` again. Shutdown now saves state once, requests
quit once, and ignores late window/settings callbacks. The six bounded Qt Core
regressions exercise real quit delivery and reentrant persistence; a full
Desktop regression separately checks closing during active video.

An additional startup correction consumes queued native frames while the GUI
has not delivered `vo-configured` visibility yet, using skip rendering without
allocating an FBO. Skipped frames receive a swap acknowledgement. Visible video
retains its normal mpv timing and paused redraw behavior. The old first-frame
200 ms warnings are distinct from the rapid WebEngine memory reproduction.

The opt-in live Desktop regression uses the actual packaged executable, a fresh
portable profile, the locked asymmetric red/cyan fixture, an animated transparent
Web overlay, sparse composed-window pixel samples, bounded process memory and
native X-close. The threaded native-renderer investigation covers a separate
production-FBO lifecycle. Both are internal checks; the earlier paused software
renderer test alone did not cover these paths.

These changes do not qualify QTBUG-141377, the hour-long memory protocol,
diagnostics overhead or the final user acceptance. Qt release qualification
remains false. Server/Web implementations, contracts and deployments are unchanged.

## Final development build check

The complete normal and diagnostic pipelines on clean
`14922aa8efa49b91e7ecfcd880275abd71fba563` passed, including package binding,
runtime import closure, NativeShell/CSP, six shutdown cases and matching
diagnostic EXE/PDB identity. The diagnostic listener bound only
`127.0.0.1:8086`; an ordinary launch exposed no DevTools port. Both push and PR
CI runs succeeded. Installer/portable/diagnostic/symbols assets and SHA256 sums
are under `build/desktop-qt6-development/14922aa`.

The same tracked live Desktop protocol first reproduced the old executable's
memory ceiling failure, then passed on the exact packaged rc.3 executable.
Four load/play cycles, native stop, an additional active load and three sparse
composed-window samples passed. Parent private memory peaked at 559,517,696
bytes; separately verified children peaked at 21,291,008 bytes. Native stop
took 0.70 ms; native X-close during video took 485.22 ms and exited with code
zero. No owned child remained before cleanup. AMD OpenGL and five observed
`d3d11va-copy` decoder activations were retained.

Three transient mpv startup/close frame-stall warnings remain in this full
Desktop observation; composed pixels and playback progressed. The standalone
paused startup comparison passed with both renderers and did not reproduce
those warnings. Neither result is proof that all native-thread stalls are fixed.
The earlier failed investigation attempts remain retained; no failure was
converted into passing evidence. The short observation does not replace the
hour-long or diagnostics-overhead protocols.
