# Jellyfin Desktop 2.0.0 for Windows

Windows x64: Windows 10 version 1903 or later, and Windows 11.
This version promotes the owner-accepted `2.0.0-rc.6` application source
(`cac8ac6201401ae10fb81a47f828fcf6f5cf5c5a`) without further behavior changes.
MSI/Burn package version `2.0.7` supports upgrading from RC6 (`2.0.6`).
Release preparation also makes the RC-promotion unit fixture independent of
the repository's current version. It changes no application code or validator
rules. The strict evidence validator treats this test-file change as a source
change, so technical qualification must bind the final stable source commit;
the RC6 owner acceptance remains recorded as its original historical evidence.

## Changes

- Qt 6.11.3 and updated Windows build, rendering and packaging dependencies.
- Preserved SyncPlay with independent audio/subtitle choices, asynchronous
  external audio and explicit time/rate conversions for the final server contract.
- Preserved Discord Rich Presence, Cyrillic text, posters and reconnect behavior.
- Fixed server selection, retained playback backgrounds, black video, timeline
  trails, rapid WebEngine memory growth and delayed application shutdown.
- Improved software WebEngine cards and playback settings/subtitle menu animations.
- Added opt-in local diagnostics and a separate diagnostic portable with symbols.
- Fork-specific release checks distinguish stable and prerelease versions and
  open the release page for installation.

Only Desktop and its client integrations changed. The pinned compatibility
target is Server/Web `12.2.0`, contract `desktop-handoff-12.2-syncplay.1-r1`.
The exact source commits and synthetic fixture provenance are in
`contracts/desktop-api-contract.json`.

## Installation and assets

Close Jellyfin Desktop, then run `JellyfinMediaPlayer-2.0.0-windows-x64.exe`.
The existing executable name, installation identity and AppData settings remain.
The standard portable ZIP uses those same settings. An explicit `portable`
marker beside the executable enables isolated data/cache instead.

The installer, standard portable, diagnostic portable and diagnostic symbols
are separate assets. `build-manifest.json` pins dependencies; `SHA256SUMS`
covers the delivered files. The diagnostic build uses the same application
version and source commit, and Tracy listens only on localhost when requested.

## Known behavior

Software WebEngine is the default Windows composition mode. Hardware video
decoding remains available through mpv. To avoid expensive full-screen blending,
the video remains undimmed behind playback menus in this mode. Menu animations
and closing by clicking outside are preserved.

Discord's elapsed timer can continue while playback is paused. The displayed
position text remains accurate, and resume/seek updates the timestamps.

## Acceptance and publication status

The owner accepted RC6 on 2026-10-13. Its build, package, native startup,
strict CSP, rendering, playback lifecycle and menu performance checks passed.
Those short checks do not establish long-duration memory or mixed-client
SyncPlay qualification. The stable packages are rebuilt from the merged
`master` commit; their build identity and verification results accompany them.

This release is prepared as a **draft**, with public publication gated by the
existing release validator. `qt.qualification.qualified` remains false.
Current-source long-duration Qt memory, measured native/Web SyncPlay drift,
diagnostics-overhead qualification and clean-machine installation coverage
must not be inferred from the owner's general acceptance. No passing results
have been invented and no qualification checks have been disabled.
