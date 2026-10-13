# Single final Windows acceptance

The user withdrew acceptance of Windows 2.0.0 on 2026-10-13 after critical
fullscreen regressions and stuttering dialogs. GitHub v2.0.0 is now an
intermediate prerelease and is excluded from Latest. Its original assets are
retained for reproduction. [desktop-acceptance.json](desktop-acceptance.json)
retains the historical RC6 identity and records the explicit revocation with
`accepted: false`.

The follow-up candidate is 2.0.1-rc.1 (MSI/Burn 2.0.8). See the
[regression work list](../docs/desktop-beta-followup.md). It requires renewed
acceptance and must not be promoted to stable automatically. Mixed-client
SyncPlay drift, current-source long-duration Qt memory and diagnostics-overhead
qualification remain unasserted.

This file is a checklist, not a passing report. The user tests one complete
qualified candidate after migration and Server/Web contract integration.

The [rc.2 composition investigation](observations/desktop-composition-baseline-33a3722.json)
retains seven short historical observations from the exact installed executable.
Case codes are: 1 GPU Web/video, 2 moving Hi10/Web, 3 idle animated Web,
4 idle Web with native renderer uninitialized, 5 disabled GPU rasterization,
6 experimental ANGLE GL, and 7 software WebEngine. Earlier harness-source
snapshots were not attested at launch. These observations establish a controlled
rapid-memory reproduction and a software comparison, not release qualification.
See [the client correction and scope](../docs/windows-playback-composition-fix.md).

The [rc.3 development record](observations/desktop-development-14922aa.json)
binds nine passing selected build/package/runtime checks to clean `14922aa`.
The separate [full Desktop observation](observations/desktop-live-14922aa.json)
and [native phase values](observations/desktop-live-probe-14922aa.json) passed
on that exact normal executable: correct composed video/overlay pixels,
559,517,696-byte parent peak and 485.22 ms active-video close with exit zero and
no remaining owned children before cleanup. Its
[old-executable baseline](observations/desktop-live-baseline-33a3722.json)
failed the same controlled private-memory ceiling. Current-source hour-long
memory and diagnostics-overhead qualification remain not run; aggregate checks,
ready RC, release qualification and user acceptance remain false.

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

Before measuring, commit the exact reviewed Qt archives, reviewed
release/fix evidence URLs, `fixRelease`, `memoryReportPath` and
`memoryProtocolPath` and `releaseProofPath` in the manifest, with `qualified: false`.
An internally prepared Qt candidate remains a development build until official
release proof is available. Commit the memory
protocol in the same tested source. Build that immutable commit and record its
original manifest SHA256. The protocol fixes two scenarios, `navigation-svg`
and `long-playback`, each at least 3600 seconds after a 120-second warmup, with
five-second samples of the owned native/WebEngine process tree. The main SVG/XHR
page stays alive throughout the test; only a separate script-free iframe
navigates each minute. Native music alternates two generated WAV files each
minute. It declares a
2 GiB RSS ceiling and a 128 MiB final-growth ceiling before observation.

Commit actual observations under `quality/`: the report at `memoryReportPath`
and the raw sample JSON at its `rawSamplesPath`. The report records appVersion,
qtVersion, buildCommit, buildManifestSha256, issue=QTBUG-141377, actual passed,
processTreeIncluded=true, protocolSha256 and rawSamplesSha256. The raw JSON has
schemaVersion=1, scope=windows-owned-process-tree-rss and both scenarios, each
with ordered samples containing elapsedSeconds, rssBytes and processCount.
Report scenarios contain durationSeconds, rssBaselineBytes, rssPeakBytes,
rssFinalBytes and limitBytes. Validation recomputes these from raw samples,
checks both protocol limits, and verifies that the protocol bytes were already
present in the tested commit. Report-selected limits cannot replace that lock.

After a real passing measurement, only `qt.qualification.qualified` may change
from false to true in the manifest. Keep the report's original tested manifest
SHA256. Validation allows that one flag transition while requiring all other
manifest fields and application source to remain identical; subsequent changes
may only add `docs/` or `quality/` evidence. The flag alone does not satisfy the
release gate. A failing measurement leaves qualification false.

Once Qt's official Jira version metadata actually marks this exact package
version `released: true`, commit the reviewed snapshot at `releaseProofPath`,
with `releaseEvidenceUrl` matching the locked Jira version endpoint,
`observedUtc` in UTC, and `metadata` containing matching `name`, `released: true`
and its real `releaseDate`. Also include `archiveSha1s`, a complete map from each
locked Qt archive name to the matching official `.sha1` observed for that
released package. A missing, extra, or changed archive identity is rejected.
Do not create this proof from unreleased metadata.
Ordinary CI checks the committed snapshot without network access; tag
qualification also re-fetches that exact official endpoint with a ten-second
timeout and fails on unreleased, inconsistent or unavailable metadata. It also
rechecks every locked Qt archive's official `.sha1`, with at most four parallel
requests and a ten-second timeout per request. Changed checksums, malformed
responses and unavailable sidecars fail the tag gate even if cached packages
still match the old manifest. Sidecar redirects outside HTTPS `download.qt.io`
fail closed before they are followed; review an official mirror change before
changing that policy. Official
proof may be added under `quality/` after the tested commit if all archive and
manifest locks remain unchanged.

Record packageVersion, contract revision, Server/Web versions/commits,
machine/Windows build, scenario results and any defects as well.

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
Promotion to stable must preserve the accepted code and all dependency/evidence
locks, with only appVersion, a strictly higher packageVersion and the allowed
qualification flag transition differing from the original tested RC manifest.

## Historical failed internal observation

The [Qt 6.11.3 full-hour evidence](../docs/qt6113-memory-observation.md) records
a failed SVG final-growth rule at Desktop commit `6eeebfa3298e68f452b58b47b14bbf0e7f69e97f`.
The separately named failed gate report, derived observation and raw samples
preserve actual results: SVG +278.44 MiB and native music
-30.56 MiB. The tested manifest and qualification=false
remain unchanged. Additional SBOM and numeric diagnostics cannot turn this
failed result into passing release evidence; no user acceptance is recorded.

The preserved v4 [diagnostics performance index](diagnostics-performance/prior-three-evidence-index.json)
retains three completed synthetic observations with failed overall criteria.
The high-resolution observation's third pair was +6.190470216495991% against
the unchanged 5% p95 limit. The later process sampler worker is outside the measured
source; these historical results cannot qualify it or establish causality.

The later [post-worker observation](diagnostics-performance/post-worker-derived.json) is a
separate completed measurement on clean `ebd33bb`, not a replacement of these
three failures. It contains 60,000 verified replies and thirteen actual clock
probes at about 5 microseconds. All CPU differences satisfy the two-percentage-
point limit. P95 differences are +1.28205%, **+13.26531%** and +1.53062%; pair2
fails the unchanged 5% limit. Overall synthetic thresholds remain false and
the observation is ineligible for release qualification or user acceptance.

The [retained rc.2 development package record](observations/desktop-development-33a3722.json)
binds the packages to clean `33a3722` and manifest `c02f654a...ff9a3d74`.
Both build pipelines, actual Qt chooser/NativeShell/CSP/reload, renderer, imports,
Tracy and package/PDB checks passed. The
[connection record](observations/desktop-connection-33a3722.json) binds the old
CORS failure and new synthetic and real-server prelogin checks. The real-server
probe performed 121 GET requests and no authentication/media/group actions.
Memory and diagnostics-performance qualification have not run on `33a3722`;
aggregate checks, ready RC, release qualification and user acceptance remain false.

The [retained rc.1 development package record](observations/desktop-development-289859f.json)
binds the normal and diagnostic packages to clean `289859f`, its tested manifest,
operation outputs, complete stage inventories, installer contents and PDB.
Both local build pipelines passed, including fourteen process-sampler tests;
NativeShell, renderer and localhost Tracy probes passed. Full-hour memory and
diagnostics-performance qualification have not run on `289859f`. Aggregate checks,
ready RC, release qualification and user acceptance remain false. Later docs-only
commits do not change the `289859f` source embedded in these packages.

The [earlier development package record](observations/desktop-development-ebd33bb.json)
and its retained packages keep their original `ebd33bb` identity. The later
process-ancestry correction is outside that source and its failed p95 measurement;
the passing regression tests establish correctness for those cases, not overhead.

The [pinned Web clearQueue finding](../docs/server-web-clearqueue-finding.md)
is a separately reproduced Server/Web lifecycle defect. It is recorded without
patching served Web, changing contract pins or adding a Desktop workaround.
