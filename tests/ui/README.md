This bounded Qt Core regression compiles `closingWindow()` and
`saveStateBeforeQuit()` directly from the production `KonvergoWindow.cpp`, and
checks that its actual `aboutToQuit` connection points to the save-only method.
Only geometry persistence is replaced by a counter; no window, WebEngine,
installed profile or media player is created.

Each CTest case has a five-second process timeout and an internal two-second
event-loop deadline. Cases cover native close, direct application quit, repeated
close requests, persistence reentrancy and fullscreen geometry retention. The
tests do not prove actual window teardown, rendering or video playback.

Configure this directory with the manifest-pinned Qt prefix and the existing
Windows compiler environment, build it, then run CTest with
`--output-on-failure`. Generated method bodies and executables belong in an
ignored build directory.

The fullscreen target extracts the production forced-screen, window-state,
screen-added and geometry-restoration methods. Qt native window operations are
replaced with counters and a visibility model; it opens no windows. Cases cover
obsolete delayed transitions, forced-fullscreen policies, and repeated exits to
both maximized and normal windows. The maximized test rejects applying the saved
maximized client rectangle as normal geometry, and both roundtrip tests reject
saving geometry during restoration. Native pixels, frame decorations, asynchronous
Windows state delivery and WebEngine sizing still require the desktop harness.
