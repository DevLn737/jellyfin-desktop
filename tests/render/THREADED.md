# Exposed hardware renderer investigation

This opt-in standalone test uses the actual PlayerQuickItem/PlayerRenderer with
a real exposed QQuickWindow and Qt's threaded OpenGL loop. It never launches
JellyfinMediaPlayer.exe or accesses a server, user account, profile or media.
The existing local H.264/AAC asymmetric red/cyan HLS fixture loops continuously.
Decoding uses software; OpenGL must be hardware. The separate mpv controller
worker is the only client-event consumer.

Build without launching:

```powershell
./tests/render/run-threaded-windows.ps1 -BuildOnly -BuildDirectory build/threaded-renderer-tests
```

Run a bounded 40-second observation:

```powershell
./tests/render/run-threaded-windows.ps1 -SkipBuild -BuildDirectory build/threaded-renderer-tests -DurationSeconds 40
```

`-GuiSync` deliberately mimics synchronous GUI-thread native property reads.
It is an investigation mode, not recommended application code. `-CaptureWindow`
adds a sparse public QQuickWindow.grabWindow capture every three seconds, with
video red/cyan and QML blue-overlay checks; it can request extra renders and
records its own maximum cost. The default sparse FBO pixel checks establish
native video texture output only, not final-window composition.

`-HiddenStartup` starts the item hidden, waits for actual file-loaded,
vo-configured and fresh original position observations before showing it, then
repeats a paused hidden load halfway through the run. The window stays exposed.
It checks both shown loads, no hidden FBO allocation/output and no render-stuck
or missing-swap warnings. Use `-CaptureWindow` to verify composed paused pixels.

An independent non-GUI reporter preserves partial JSON if GUI/render/core
blocks. The runner watches only its own process, stops it at 60 seconds or
2 GiB RSS/private bytes, and records the actual failure without rewriting
partial results. Render/swap/FBO/context counts, warning counts, own memory,
GUI heartbeat/property latency and stop/window/core shutdown costs are retained.
No forced collection or per-frame glFinish/readback is performed. All results
are unqualified diagnostic evidence, not release or playback acceptance.

For comparison with a frozen renderer, pass `-RendererSourceDirectory` pointing
to a copied PlayerQuickItem.cpp and .h under build/. Its exact byte hashes are
compiled into the test report. Use distinct build directories to preserve the
baseline executable and observations. This test excludes Desktop/WebEngine
layers; constant FBO reuse cannot rule out Qt/WebEngine/driver texture retention.
