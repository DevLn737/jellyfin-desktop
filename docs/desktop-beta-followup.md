# Desktop beta follow-up, 2026-10-13

The earlier acceptance of 2.0.0 was withdrawn after reports of critical
fullscreen regressions and additional dialog stuttering. GitHub 2.0.0 is now
a prerelease, removed from Latest, with an explicit regression notice.
The previously published package bytes remain available for reproduction.

## Work list

- [x] Withdraw stable status of 2.0.0.
- [x] Reproduce repeated fullscreen transitions in the full Desktop application.
- [x] Identify incorrect maximized-window restoration and stale forced-screen callbacks.
- [x] Verify fullscreen state correction against native and HTML fullscreen,
  repeated entry/exit, restored geometry, moving video and WebEngine sizing.
- [x] Reproduce SyncPlay and card-context dialog stalls with a controlled
  comparison of the software WebEngine backdrop.
- [x] Fix shared dialog composition without disabling dialog animations or controls.
- [ ] Verify SyncPlay group/settings, video card context menu, media information,
  playback settings/subtitles and outside-click dismissal.
- [ ] Build and verify the intermediate 2.0.1-rc.1 candidate (MSI/Burn 2.0.8).
- [ ] Obtain renewed acceptance; do not promote this candidate to stable automatically.

## Reproduction evidence

The local baseline uses an isolated profile copy, the actual served Web client,
blocked remote writes/WebSockets and local synthetic video. It does not change
the server or play personal media. Reports contain numeric observations; the
profile copy, raw screenshots and application logs are removed after each run.

At 125% scaling, the first native fullscreen cycle used 2560x1440 pixels and
restored 2560x1351. The second entry incorrectly produced a 2578x1398 client
area. The subsequent HTML fullscreen exit lost the capturable window and
failed the CSS/native viewport consistency check. This is a failing baseline,
not acceptance evidence.

Sixteen transitions passed on clean `459a25b`: four native and four HTML
fullscreen transitions from each of maximized and normal windows. Moving
synthetic video remained visible, CSS/native viewport sizes agreed, and normal
window geometry returned to 640x480 CSS pixels at 125% scaling. The two runs
closed gracefully with no owned child processes left behind. This is a bounded
single-desktop regression check, not physical multi-monitor qualification.

The dialog baseline observed 233–300 ms frame intervals with the old
full-window translucent backdrop. An A/B/A comparison demonstrated the shared
software-composition cost on library pages as well as video. Omitting dimming
while retaining modal hit areas fixes that path. Actual SyncPlay group menus,
card context menus and playback settings/subtitles then mostly ran at 16.8 ms.

Large fixed-size forms still had 33–83 ms intervals during their 180 ms entry
animation. Containment, will-change, shadow and fade experiments did not remove
this reliably. Tracing found distributed software paint/draw work; Qt's software
frame bridge additionally recreates and uploads a viewport-sized texture.
A software-client-only 1 ms transition for `.formDialog.dialog-fixedSize`
avoids repeated expensive animation frames, while preserving animation-end
callbacks, focus, close behavior and original layout. It retains existing
animations on small menus and GPU rendering. Repeat observations reached the
fully drawn form around 246–261 ms after the click, versus 438–510 ms with the
old transition. One-time form creation can still take about 50 ms per frame;
these measurements do not claim every opening frame is 60 FPS.

Final packages and fresh production checks follow the final source commit.

Long-duration Qt memory and mixed-client SyncPlay drift qualification remain
separate outstanding measurements. The previous publication exception does
not establish that these newly reported regressions are fixed.
