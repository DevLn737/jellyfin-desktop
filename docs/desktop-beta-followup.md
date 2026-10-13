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
- [x] Fix shared dialog composition while preserving modal input and cleanup.
- [x] Verify SyncPlay group/settings, video card context menu, media information,
  playback settings/subtitles and outside-click dismissal.
- [x] Build and verify the intermediate 2.0.1-rc.1 candidate (MSI/Burn 2.0.8).
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
animations on small menus and GPU rendering. A controlled pre-packaging comparison reached the
fully drawn form around 246–261 ms after the click, versus 438–510 ms with the
old transition. One-time form creation can still take about 50 ms per frame;
these measurements do not claim every opening frame is 60 FPS.

Final packages use clean `7c52bdc2c2c63c3da250046cf53ea2f4e8a108a5`.
The fresh production run checked all four broad dialog routes twice, plus
playback settings/subtitles twice, without injected style overrides. Form
entry/exit callbacks used 1 ms; focus and cleanup passed. Cold large forms
finished opening around 316–325 ms after the click, warm forms around
247–263 ms. Creation still produced individual 50–67 ms intervals; a sustained
low-frame-rate transition is no longer required. Active-video close took
437.54 ms, the original profile was unchanged and no owned children remained.

Normal and diagnostic builds, all native/JavaScript checks, strict CSP,
renderer, package payload binding, symbols and localhost-only Tracy passed.
[CI for the built source](https://github.com/DevLn737/jellyfin-desktop/actions/runs/38381331638)
passed all three jobs; publication was skipped. One initial local test-process
startup exceeded its five-second limit; the targeted retry and full build
rerun passed. Its failed-attempt log hash is retained rather than treated as a
successful first attempt.

[The numeric evidence](../quality/observations/desktop-beta-7c52bdc.json)
binds the installer, portable, diagnostic portable and symbols to this source.
It also distinguishes earlier fullscreen observations on unchanged native
code from final production dialog measurements. The later evidence-only
commit does not change the source embedded in these packages.

Long-duration Qt memory and mixed-client SyncPlay drift qualification remain
separate outstanding measurements. The previous publication exception does
not establish that these newly reported regressions are fixed.
