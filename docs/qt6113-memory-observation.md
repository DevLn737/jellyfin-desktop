# Qt 6.11.3 failed Desktop memory observation

The full Desktop run at `6eeebfa3298e68f452b58b47b14bbf0e7f69e97f` failed the predeclared final-growth rule
on 2026-10-11. The package remains an unqualified internal candidate; no passing
memory report, official release proof or user acceptance is claimed.

The run started at 2026-10-11T17:12:23.4581119Z and finished at
2026-10-11T18:15:07.5207066Z, using app `2.0.0-rc.1`, Qt
`6.11.3` and Windows build 26100.
The tested manifest SHA256 was `af7e6e844bc858191fcce7b88ef3d211dc5fd5d9905c37ff181d64a7431b69fc`;
the already committed protocol SHA256 was `cb2ee39ab817a667e279e1bde9eeee4a69857e7db9d9cbeae6ccf5ad9bb238d7`.
Each scenario had 691 samples over 3601.896 seconds
after the unchanged 120-second warmup. Both stayed below the 2 GiB RSS peak
ceiling; SVG exceeded the 128 MiB final-growth ceiling.

| Owned process tree | Baseline bytes | Peak bytes | Final bytes | Final growth |
| --- | ---: | ---: | ---: | ---: |
| Desktop SVG/XHR | 1031032832 | 1527844864 | 1322999808 | +278.44 MiB (291966976 bytes) |
| Desktop native music | 252243968 | 264118272 | 220196864 | -30.56 MiB (-32047104 bytes) |

The workload drivers completed their workloads, which is distinct from passing
the memory rule. SVG made 37491 data requests, 37488 image requests and 61 child
iframe navigations. Native music made 62 file changes and 3705 native position
checks. These are hidden full Desktop process trees; Node drivers are excluded.
Process working sets count shared pages once per process and are not unique
physical-memory use or private committed bytes.

[The derived failed observation](../quality/observations/qt6113-memory-failed-observation.json)
recomputes the two results from
[the exact raw samples](../quality/observations/qt6113-memory-failed-samples.json), SHA256
`c640a42bd9158a8ac3a399bdd262743a4802358661a58ad71ee9dfc74e04cee3`. It also records the original derivation tool
and workload hashes. [The original failed gate report](../quality/observations/qt6113-memory-failed-gate-report.json)
is retained byte for byte, SHA256 `fec8f6fcfd8b9e08e9c2b9743e0c48b08e8630634d0eb00d7cf2e12651deddc8`.
Its empty scenarios and raw checksum reflect the owner's failure path; the
separate derived observation preserves completed samples without inventing a
passing report. These explicitly failed filenames do not replace the manifest's
required passing report or change qualification.

## Hidden-window and attribution limits

A separate single primitive visibility read observed document visible/focused,
Chromium viewport 0×0 and DPR1. It did not establish that tuple throughout the
hour. Actual Desktop QML retains QQuickWindow default-data parenting to its
contentItem; zero Chromium viewport does not prove an unattached WebEngineView.
A hidden application provides no integrated video render pass or hardware/driver
qualification. Mixed native/Web drift, installed upgrades and user acceptance
remain separate checks.

The measured result establishes SVG process-tree growth in this hidden Desktop
workload. It does not establish whether the cause is Qt/Blink, a Desktop
integration layer, retained images, process working-set behavior or another
allocation category. The producer-declared fixing source and DLL checksum chain
are stronger package identity evidence, not a memory-behavior result.

The earlier Widgets controls stopped before RSS sampling. One had an argv setup
failure; the corrected one observed hidden/unfocused 0×0 with DPR1.25 and failed
its equivalence guard. Their zero-sample failures, including the corrected run's
immediate cleanup=false and later separately confirmed shutdown, are preserved
in [prior control observations](../quality/observations/qt6113-prior-control-observations.json).
They cannot be compared as full-hour RSS controls. A separate minimal QtQuick
comparison subsequently completed: 698 samples over 3603.565 seconds, baseline
1762926592 bytes, peak 1815408640 bytes and final 1225474048 bytes. Its final
change was -537452544 bytes and its declared limits were met. It remains
ineligible for Desktop or Qt release qualification.

The comparison retained Qt Quick, WebEngineView, layer and inherited QQuickWindow
parenting, while removing Desktop components, NativeShell, WebChannel and libmpv
together. It used the default Qt user agent, additional five-second DOM reads
and a substantially higher baseline than Desktop. Those differences prevent
attribution to an individual component. RSS/DOM reductions are observations;
they do not identify a GC mechanism or erase the Desktop failure. The original
control report recorded an integer 5-second maximum sample gap; independent
double-precision derivation gives 5.404 seconds, still below the unchanged
10-second limit. The original report is preserved as reported. Read the
[completed comparison](../quality/qt6113-quick-control-observation/README.md)
and its byte-bound original receipts and separate derived observation.

## Bounded numeric CDP observations

During the frozen `6eeebfa3298e68f452b58b47b14bbf0e7f69e97f` memory run,
an additional bounded observer read the already running `navigation-svg`
instance. Before each paired OS/CDP observation it verified that the single
localhost listener on port 9222 belonged to the application in that run's
isolated portable directory. It created no application or browser, changed no
workload, and retained no page URLs, DOM snapshots, protocol events, accounts,
logs or user data. The observer retained numeric counters only.

Only `Memory.getDOMCounters` and `Runtime.getHeapUsage` were sent. No domain was
enabled, page JavaScript evaluated, profiler started, page navigated, or garbage
collection requested. The exact implementations were read at Chromium commit
`7927ce61b5a302a3da8172eefbe260ec17484bcd` before using either method:

- [Memory agent](https://codereview.qt-project.org/projects/qt%2Fqtwebengine-chromium/commits/7927ce61b5a302a3da8172eefbe260ec17484bcd/files/chromium%2Fthird_party%2Fblink%2Frenderer%2Fcore%2Finspector%2Finspector_memory_agent.cc/content): `getDOMCounters` reads three existing instance counters.
- [Runtime agent](https://codereview.qt-project.org/projects/qt%2Fqtwebengine-chromium/commits/7927ce61b5a302a3da8172eefbe260ec17484bcd/files/chromium%2Fv8%2Fsrc%2Finspector%2Fv8-runtime-agent-impl.cc/content): `getHeapUsage` requests V8 statistics and CppHeap statistics at `kBrief` detail.
- [V8 API](https://codereview.qt-project.org/projects/qt%2Fqtwebengine-chromium/commits/7927ce61b5a302a3da8172eefbe260ec17484bcd/files/chromium%2Fv8%2Fsrc%2Fapi%2Fapi.cc/content) and [heap implementation](https://codereview.qt-project.org/projects/qt%2Fqtwebengine-chromium/commits/7927ce61b5a302a3da8172eefbe260ec17484bcd/files/chromium%2Fv8%2Fsrc%2Fheap%2Fheap.cc/content): `GetHeapStatistics` releases unused linear allocation buffers and reads statistics; it makes no collection request.
- [CppHeap forwarding](https://codereview.qt-project.org/projects/qt%2Fqtwebengine-chromium/commits/7927ce61b5a302a3da8172eefbe260ec17484bcd/files/chromium%2Fv8%2Fsrc%2Fheap%2Fcppgc-js%2Fcpp-heap.cc/content) and [HeapBase implementation](https://codereview.qt-project.org/projects/qt%2Fqtwebengine-chromium/commits/7927ce61b5a302a3da8172eefbe260ec17484bcd/files/chromium%2Fv8%2Fsrc%2Fheap%2Fcppgc%2Fheap-base.cc/content): `kBrief` returns existing size counters before any sweeper calls.

The [performance agent](https://codereview.qt-project.org/projects/qt%2Fqtwebengine-chromium/commits/7927ce61b5a302a3da8172eefbe260ec17484bcd/files/chromium%2Fthird_party%2Fblink%2Frenderer%2Fcore%2Finspector%2Finspector_performance_agent.cc/content)
was also inspected. Its `getMetrics` would return an empty array without first
enabling that domain; neither method was called. Full source byte sizes and
SHA256 values accompany the implementation references in the evidence delta.

Four CDP reads span about 85.5 seconds. The last three were paired with OS reads
over 31 seconds; these are additional diagnostics, not a new measurement
protocol or a substitute for the owner's five-second RSS samples.

| Counter | First CDP read | Last CDP read |
| --- | ---: | ---: |
| Documents | 155 | 120 |
| Nodes | 247978 | 204576 |
| JS event listeners | 52 | 36 |
| V8 used bytes | 877476 | 873496 |
| V8 total bytes | 1835008 | 2097152 |
| Embedder used bytes | 494415584 | 414033160 |
| Backing storage bytes | 267784 | 267784 |

The reduction in document/node/embedder counters occurred without a collection
request from this observer. Ordinary JS used size stayed below one MiB. This
supports natural reclamation during the observed interval and locates much of
the sampled heap use in the embedder rather than ordinary JS objects. It does
not identify every retained allocation or establish that memory is bounded.

Across the three paired OS reads, renderer working set stayed about 1.294
billion bytes (1.205 GiB; `1294340096` to `1294503936` bytes), while renderer handles
rose from 2254 through 2520 to 2786. Native application working set stayed near
172 million bytes. This short handle trend warrants retaining the evidence;
it does not establish a Chromium leak or its cause. CDP heap statistics and
Windows working set/private bytes measure different memory categories and
cannot be added or substituted for one another.

The CDP and OS observations remain separate from the declared full-hour limits.
They do not turn a failed RSS growth gate into a pass, prove that QTBUG-141377 is
fully resolved in this workload, establish an official Qt release, or qualify
mixed clients, installed upgrades or user acceptance.

## Late OS-only observations and the separate visibility read

Three additional OS-only samples span 373.536 seconds. Renderer handles were
1078 → 4172 → 1054; renderer RSS was 1264046080 → 1226035200 → 1180344320 bytes.
Native handles were 2188 → 2184 → 2188. Each read rechecked the exact owned root,
listener and descendant path/creation identities; retained data contains only
UTC, numeric roles, RSS/private bytes, handles and thread counts. No CDP method,
process control or workload change was performed by this later observer.

This natural drop means the earlier short handle increase is not evidence of
continuous monotonic growth. It does not erase the failed hour or prove bounded
long-term behavior. The primitive visibility read was a separate
`Runtime.evaluate` of visibilityState, hasFocus, innerWidth/innerHeight and DPR.
An earlier read failed an incorrect positive-viewport validation before its
values were saved; the corrected read retained zero dimensions and that
limitation. This read is not part of the no-JavaScript-evaluation heap observer.

The complete sanitized numeric observations and original hashes are in
[quality/observations/qt6113-numeric-observations.json](../quality/observations/qt6113-numeric-observations.json).
No observer requested garbage collection. Statistics reads can still have
observation effects, including release of unused linear allocation buffers;
"no forced GC" does not mean zero observation overhead. Source byte identities
and exact implementation references are retained in
[additional package evidence](../quality/observations/qt6113-additional-package-evidence.json).

## Separate diagnostics performance evidence

The final sanitized export `performance-evidence-6eeebfa-v4` is recorded in
[the diagnostics performance index](../quality/diagnostics-performance/prior-three-evidence-index.json),
SHA256 `33e8cafbe80ed2efc5f562a95ae7463889101ebf325a9c891774792990aaabe7`.
It preserves three completed synthetic observations and 126000 verified replies;
all three have a threshold-failing pair. The latest high-resolution observation
used frozen source `6eeebfa3298e68f452b58b47b14bbf0e7f69e97f` and approximately 5 microsecond clock steps in
its isolated local context. Its third pair had p95 +6.190470216495991%, exceeding
the unchanged 5% limit. See
[highres-derived.json](../quality/diagnostics-performance/highres-derived.json),
SHA256 `3db619a514a70877903008d75d17f3ca27254fc2115f9c6d76bac3e2a2984966`.

That higher precision did not turn the criterion into a pass. The measured
157-vs-122-second phase windows and their residual are descriptive context,
not evidence of a scheduling mechanism or attribution to diagnostics. The
source review confirms synchronous process-stat enumeration on the GUI thread,
but no per-operation trace links it to the p95 increase. These historical hidden
idle observations do not qualify playback/rendering, mixed clients, production
or Tracy overhead. Later worker/source changes require their own measurements
and do not change the recorded failures or qualify a new source version.
