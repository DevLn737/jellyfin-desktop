# Playback menu rendering in the software WebEngine client

RC6 leaves video undimmed while playback menus are open. It keeps the dialog's
entry/exit animations, modal hit area, outside-click dismissal and controls.
The local NativeShell stylesheet applies this only under `transparentDocument`
and only when the effective WebEngine backend is software. Other pages and the
GPU opt-in retain their Web styling. There is no repaint timer, Chromium flag
change, server change or change to mpv rendering.

## Reproduction and diagnosis

The October 13 RC5 reproduction used the actual served Web UI in an isolated
copy of the profile, a local synthetic mpv video, the real settings/subtitle
buttons and a foreground maximized window. Remote writes and WebSockets were
blocked. The original profile was hash-checked and the cloned profile and raw
screenshots removed after each run. No user media was played.

During opening, requestAnimationFrame intervals reached 250–267 ms. Native
WebChannel round trips remained around 1 ms and JavaScript/layout costs were
small. A `cc,viz,benchmark` Chromium trace localized long frames to
`SoftwareRenderer::DoDrawQuad` inside the browser process, with individual draws
around 250 ms. The trace establishes an expensive software draw, not the exact
underlying Skia operation.

Removing the full-window backdrop eliminated the stalls. Pausing playback,
removing menu shadows, filters, rounded corners or transforms did not. Changing
the backdrop from animated opacity to animated background alpha did not fix it.
These unsuccessful experiments are not included in production.

## Controlled comparison

`build/osd-menu-control/scoped-backdrop-rc5` compares original styling, the scoped
opacity override, a repeat, and original styling restored in the same app run.
Each row is a 1.6-second observation after clicking the actual menu button.

| Styling | Settings frames / maximum interval | Subtitle frames / maximum interval |
| --- | --- | --- |
| Original | 68 / 250 ms | 68 / 250 ms |
| Scoped override | 96 / 16.8 ms | 96 / 16.8 ms |
| Override repeated | 96 / 16.8 ms | 95 / 33.3 ms |
| Original restored | 55 / 250 ms | 22 / 266.7 ms |

In the first 650 ms, the override delivered 38–39 intervals, compared with
4–11 in the original controls. This distinguishes opening performance from
the otherwise smooth frames after a menu has finished appearing. The small
visual tradeoff is intentional: the video retains its brightness behind menus.
The menu itself remains opaque and readable.

This targeted observation does not establish hour-long memory qualification,
real multi-client SyncPlay, or final user acceptance. RC6 remains a development
candidate under the migration's existing release gates.
