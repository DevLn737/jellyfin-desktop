# Single final Windows acceptance

This file is a checklist, not a passing report. The user tests one complete
qualified candidate after migration and Server/Web contract integration.

| Area | Required evidence |
| --- | --- |
| Playback | Direct Play, remux/transcode, hardware decode/fallback, passthrough, audio/subtitle selection, fullscreen, DPI and multiple monitors |
| SyncPlay | Two and four clients, pause/resume, seek, base speed, temporary correction, buffering, reconnect and episode change; each client's independent tracks persist |
| Lifecycle | Rapid file/track switches, no duplicate external demuxers/stale replies, metadata changes only on actual START_FILE |
| Discord | Cyrillic, poster/fallback, progress, pause/seek/resume, Discord restart and stop/disable/exit |
| Installation | Old SyncPlay and Discord installer upgrades, RC to stable, preserved settings/login, clean Windows without development tools |
| Performance | At least an hour with episode changes, no reproducible freezes or unbounded memory growth, demonstrated QTBUG-141377 fix |
| Diagnostics | Off at launch, ten-minute/16 MiB/15-minute limits, no secrets in export, CPU overhead at most two percentage points and p95 latency worsening at most 5% |

Drift thresholds come from the final Server/Web contract. Use a common time
scale when comparing clients; event arrival time alone is not measured drift.
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
identifiers required by release validation. Never prefill acceptance or memory
success. Promotion to stable must preserve the accepted code, with only the
reviewed release version/evidence changes and a strictly higher packageVersion.
