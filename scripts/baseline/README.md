# Stage 1 baseline of the 2.x client

Measures the packaged 2.x client on a real Windows PC with the same scenarios
as the new client prototype (`jellyfin-desktop-next`), so the stage 1
thresholds "better than the fork" can be checked. The protocol (media,
overlay, scenarios, statistics) is kept in the project's
`architecture/measurement-protocol.md`.

- `Measure-Baseline.ps1` runs an isolated portable copy of a release stage,
  drives it over localhost DevTools (bundled page, no server, no sign-in) and
  samples the client's own process tree. Window frames come from the Qt Quick
  render loop log (`qt.scenegraph.time.renderloop`), optionally PresentMon.
- `jfd-probe.lua` is loaded by libmpv through `mpv.conf` and records avsync,
  dropped frames, `hwdec-current` and load/seek timing. The prototype loads
  the same script.

Run it at the PC's own console, not over Remote Desktop: an RDP session
renders through the remote display adapter and its GPU, frame and CPU
numbers are not representative.

```powershell
powershell -ExecutionPolicy Bypass -File scripts\baseline\Measure-Baseline.ps1 `
  -StagePath C:\jfd\stage -MediaDir C:\jfd\media -OutDir C:\jfd\out `
  -PresentMonPath C:\jfd\PresentMon.exe
```

Results are committed under `quality/baseline/`.
