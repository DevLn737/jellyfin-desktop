# Single final Windows acceptance

This file is a checklist, not a passing report. The user tests one complete
qualified candidate after migration and Server/Web contract integration.

| Area | Required evidence |
| --- | --- |
| Playback | Direct Play, remux/transcode, hardware decode/fallback, passthrough, audio/subtitle selection, fullscreen, DPI and multiple monitors |
| SyncPlay | Two and four clients including mixed Windows native Desktop + Web, cold second-client joins at 1/1.25/1.5x, pause/resume, seek, base speed, temporary correction, buffering, reconnect and episode change; each client's independent tracks persist |
| Lifecycle | Rapid file/track switches, no duplicate external demuxers/stale replies, metadata changes only on actual START_FILE |
| Discord | Cyrillic, poster/fallback, progress, pause/seek/resume, Discord restart and stop/disable/exit |
| Installation | Old SyncPlay and Discord installer upgrades, RC to stable, preserved settings/login, clean Windows without development tools |
| Performance | At least an hour with episode changes, no reproducible freezes or unbounded memory growth, demonstrated QTBUG-141377 fix |
| Diagnostics | Off at launch, ten-minute/16 MiB/15-minute limits, no secrets in export, CPU overhead at most two percentage points and p95 latency worsening at most 5% |

The final Server/Web handoff requires absolute drift **strictly below 400 ms**
at 10, 15 and 20 seconds for each of 1, 1.25 and 1.5x, with correction explicitly
enabled (the Web default is off) and a cold second client. Use real logical media
positions including any transcoding offset, projected to one wall-clock sample
time using each client's effective playback rate. Group base rate and temporary
effective correction rate are separate values. Event arrival time alone is not
measured drift; the server agent's passing browser receipts are not native results.

Include paused seek to 30 seconds, rate changes while paused, leave/rejoin and
reconnect with personal-speed restoration, accelerated legacy-participant 409
rejection with unchanged group state and a successful return to 1x. Exercise
Direct Play and HLS/transcode, external ASS subtitles and AC3 audio, music and
Live TV. Preserve independent audio/subtitle selections. Use the complete queue
or Controller.play for episode tests: the pinned Web autoplay helper's single
episode SetNewQueue behavior is recorded separately as a Web issue.

Run diagnostics-off/on comparisons on identical workloads and account for the
WebEngine process tree. Keep installer repair/downgrade/rollback checks within
this final acceptance session. Report server defects to the server agent.

Record appVersion, packageVersion, immutable buildCommit, Qt version, manifest
SHA256, contract revision, Server/Web versions/commits, machine/Windows build,
scenario results and any defects. Actual RSS observations (navigation and long
playback, at least 3600 seconds each) are committed as the manifest's
`memoryReportPath`, with appVersion, qtVersion, buildCommit,
buildManifestSha256, issue=QTBUG-141377, passed and scenarios containing
durationSeconds, rssBaselineBytes, rssPeakBytes, rssFinalBytes and agreed limitBytes.

Only after actual user acceptance create `quality/desktop-acceptance.json`
with accepted=true, acceptedBy=user and the exact version/source/manifest
identifiers required by release validation, plus `apiRevision`, `serverCommit`,
`webCommit` and `nativeAcceptancePerformed=true`. The `syncPlay` object records
`composition=windows-desktop-and-web`, `correctionEnabled=true`,
`coldSecondClient=true`, observed `driftSamples` with `baseRate`, `afterSeconds`
and `absoluteDriftMilliseconds`, and completed `mixedClientCases`:
`pause-seek`, `rejoin-reconnect`, `legacy-409`, `personal-speed-restoration`,
`independent-audio-subtitles`, `direct-play` and `hls`. Include all nine
rate/sample-time combinations. Never prefill acceptance or memory success.
Promotion to stable must preserve the accepted code, with only the
reviewed release version/evidence changes and a strictly higher packageVersion.
