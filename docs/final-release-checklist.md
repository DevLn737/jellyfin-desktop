# Final current-client release preparation — 2026-10-20

The user requested a completed final release, permitted a full client code review
and confirmed that both participants tested 2.0.1-rc.2 (`9530a85`) without known
remaining bugs. Further development ends after the final release. Server/Web
and the separate replacement-client project remain outside this work.

- [x] Confirm both users' beta feedback; retain its exact source identity.
- [x] Review native playback/render/window lifecycle and fix confirmed defects.
- [x] Review JavaScript playback/SyncPlay/settings lifecycle and fix confirmed defects.
- [x] Review diagnostics, Discord, updater and packaging; fix supported credential redaction.
- [ ] Build and run all native/JavaScript/package checks on clean final source.
- [ ] Check composed paused-start video and client settings/fullscreen on that build.
- [ ] Repeat the frozen hour-long memory protocol and diagnostics benchmark.
- [ ] Verify current official Qt release status and package identity.
- [ ] Complete mixed-client drift evidence and installation qualification.
- [ ] Integrate in master, publish verified final assets and record development closure.

The official Qt version endpoint was read on 2026-10-20. It still reports
6.11.3 as `released: false`, with planned date 2026-10-23. A planned date is not
release evidence. This is an external qualification dependency; do not mark it
passed or bypass it merely because the application builds.

User feedback confirms observed behavior; it does not supply measured drift,
all matrix cases or clean-machine installation evidence. Historical failed
memory and diagnostic-overhead observations remain retained.

## Confirmed final-review defects

- Audio/video discarded saved zero volume because defaults used truthiness.
- Canceling an audio fade left its Promise pending; an old stop could affect
  replacement playback. Stop/fade transactions now cancel and settle explicitly.
- Video play yielded before loading despite synchronous media-element creation,
  allowing stop or replacement to be overtaken. Preparation now stays synchronous
  without emitting cancellation errors that Web interprets as playback failures.
- An old playback-error confirmation could emit an error after a replacement
  started; its result is now bound to the playback generation.
- Canceling a pending native load rejected the Web play promise, whose error
  handler could act on replacement playback. Dispatch completion is separated
  from native readiness; SyncPlay Ready still requires the current native sample.
- A synchronous stopped-event subscriber could start a new item before old
  state was cleared; old teardown could destroy the replacement.
- Display-switch callbacks were unbound to QObject lifetime and file generation.
- External subtitle loading used a synchronous mpv command on the GUI thread.
  A local delayed HTTP fixture blocked it for 1208 ms; asynchronous submission
  took 0.017 ms with successful completion after the response. Audio/subtitle
  request namespaces, cancellation, deduplication and stale-track cleanup are tested.
- Token censoring assumed exactly 32 characters and missed supported header,
  encoded and mixed-case forms. Direct stream-URL debug messages were removed.

Focused regressions exercise the actual player methods, production native
continuations and log censor. Full application verification follows the final
source commit; these focused checks do not replace it.
