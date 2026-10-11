# Native/WebEngine memory measurement

Build a clean committed source with the manifest-pinned Qt packages. From the
checkout, run:

```powershell
./scripts/build-windows.ps1
./scripts/test-windows-memory.ps1 -StagePath build/windows/release/portable-stage
```

The owner starts two hidden full application instances with new explicit
portable profiles. It checks deployed Qt hashes, source commit, packaged import
closure and separate localhost-only DevTools listeners. It observes the native
process and all descendants with Windows process-tree working-set samples.
Node workload drivers are outside that measured tree. Working sets include
shared pages once per process; these are process-tree RSS observations, not
unique physical-memory usage or private committed bytes.

After a 120-second warmup, both scenarios run for at least an hour. The persistent
SVG page follows QTBUG-141377's XHR/DOM replacement pattern with an original
synthetic SVG; only an auxiliary iframe navigates, so replacing the main V8
context cannot hide the defect. The other instance plays silent PCM using actual
libmpv, changes files and verifies native readiness and advancing millisecond
positions. Both use local generated content without a server account.

The protocol in `quality/memory-protocol.json` is committed before observation.
It fixes the duration, sampling interval and limits. Reports retain actual raw
samples and workload counters. No forced collection, remote transmission or GC
workaround is used. Test-only flags disable background timer throttling to keep
the hidden XHR workload near ten requests per second; the driver checks its
observed cadence. These flags are never added to normal release startup.

Use `-Smoke -SmokeSeconds 15` to check harness startup before investing an hour.
A smoke report explicitly records `qualificationEligible=false`; it cannot be
used as release memory evidence. Results stay in a new ignored `build/memory-*`
directory, including local app logs and profiles. Retain only reviewed sanitized reports
and raw numeric samples under `quality/`. Failed full runs use explicitly failed
observation filenames and `passed=false`; they never substitute for required
passing gate evidence. Never publish the test profile or raw logs.

The owner restores its environment and stops only processes it launched or
observed as their descendants with matching creation times. It does not stop
another app occupying its test ports, copy user profiles or install an upgrade.

A hidden QQuickWindow has no integrated video render pass. The production
offscreen renderer test covers FBO composition separately; hardware decoding,
driver behavior, multiple monitors, mixed-client drift, installed upgrades and
the final user acceptance remain distinct checks. Passing a prepared Qt package
does not establish an official release: the release gate separately requires
reviewed official release evidence and a live released-status check.

The [2026-10-11 failed Qt 6.11.3 observation](qt6113-memory-observation.md)
retains a real completed hour and its unchanged limits. SVG growth was
+278.44 MiB; native music changed by -30.56 MiB.
Qualification remains false.
