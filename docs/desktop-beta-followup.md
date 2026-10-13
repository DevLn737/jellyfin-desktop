# Desktop beta follow-up, 2026-10-13

The earlier acceptance of 2.0.0 was withdrawn after reports of critical
fullscreen regressions and additional dialog stuttering. GitHub 2.0.0 is now
a prerelease, removed from Latest, with an explicit regression notice.
The previously published package bytes remain available for reproduction.

## Work list

- [x] Withdraw stable status of 2.0.0.
- [x] Reproduce repeated fullscreen transitions in the full Desktop application.
- [x] Identify incorrect maximized-window restoration and stale forced-screen callbacks.
- [ ] Verify fullscreen state correction against native and HTML fullscreen,
  repeated entry/exit, restored geometry, moving video and WebEngine sizing.
- [x] Reproduce SyncPlay and card-context dialog stalls with a controlled
  comparison of the software WebEngine backdrop.
- [ ] Fix shared dialog composition without disabling dialog animations or controls.
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

The dialog baseline observed 233–250 ms frame intervals with the existing
full-window translucent backdrop. The same-process comparison without backdrop
blending substantially improved SyncPlay and card-menu animation. Media-info
coverage and complete final checks are still pending.

Long-duration Qt memory and mixed-client SyncPlay drift qualification remain
separate outstanding measurements. The previous publication exception does
not establish that these newly reported regressions are fixed.
