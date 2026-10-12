# Windows playback and shutdown regression

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
