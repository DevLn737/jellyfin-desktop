# Local diagnostics

Open Client Settings and the «Диагностика» section. Start, stop, save and clear
operate on a local session. Collection is disabled at launch and is never
remembered in settings. It retains a moving ten-minute window with a 16 MiB
limit, including ring entry headers and metadata reserve, and stops after
fifteen minutes. Saving uses the system file dialog; cancelling does not send
or write anything. Export uses a worker and a snapshot of the ring. Continuing
capture during export can temporarily require one additional bounded ring copy.

The JSON report includes app/Qt/mpv build identifiers, a random session UUID,
UTC session start and elapsed timestamps. Records contain only finite numbers:

| kind | Data |
| --- | --- |
| 0 Process | CPU percentage, private bytes, aggregate child CPU/private bytes/count, sample duration |
| 1 Gui | GUI timer delay above its 100 ms interval |
| 2 Playback | Position/duration in ms, speed, A/V sync seconds, frame drops, cache seconds, FPS and bitrates |
| 3 Buffering | Native buffering percentage |
| 4 State | Native player state enum |
| 5 Command | Native command enum and duration in ms |
| 6 SyncPlay | Fixed event enum, available position/rate/state/command and scheduled UTC timestamp |
| 7 WebChannel | One asynchronous round-trip duration in ms per second during capture |

Native commands are Load=0, Queue=1, Stop=2, Seek=3, Pause=4, Play=5,
AudioTrack=6, SubtitleTrack=7, Rate=8 and MpvEvents=9. SyncPlay events are
join=0, leave=1, pause=2, resume=3, seek=4, rate=5, buffering=6,
reconnect=7, command=8, error=9 and drift=10. Optional SyncPlay numbers depend
on what the connected Web client exposes. Group command status uses
Unpause=0, Pause=1, Stop=2, Seek=3; buffering status uses Idle=0, Waiting=1,
Paused=2, Playing=3. Waiting due to a seek is not a buffering event.

CPU percentages are normalized by logical CPU count. Process private memory
is not RSS; measure RSS separately for release memory qualification. Child
process numbers include the WebEngine process tree, without identifying it in
the report. Playback metrics are cached from the existing mpv event consumer;
collection adds no synchronous mpv property queries to the GUI timer.

Windows process enumeration and memory/CPU queries run in a worker, with at
most one pending sample. A busy worker skips the next one-second timer tick.
Capture generations discard late results after stop/restart. CPU intervals use
the worker's monotonic observation times and process creation identity; process
IDs, names and creation timestamps remain internal and are never exported.

Tokens, cookies, headers, complete URLs, file paths, media titles, user data,
raw API responses and IPC payloads are not admitted. The normal log and debug
overlay are not attached. No report has a network upload action. Before sharing
a report, its optional user-supplied filename is the user's own choice.

## Detailed profiling

Build with `scripts/build-windows.ps1 -Diagnostic`. The separate portable has
the same app version and source commit, RelWithDebInfo and PDBs, with static
Tracy 0.14.1. Connect a local viewer on demand. Broadcast, frame screenshots,
source transfer, sampling, context switching, system tracing, call stacks and
VSync capture are disabled.
Tracy's supported platform hooks redact the Windows login, full name and host
identity; application zones do not transfer absolute checkout paths.
Constant zones cover native playback commands,
mpv events, render/composite/swap, diagnostic WebChannel dispatch, logger writes
and Discord IPC. Normal builds contain no Tracy client.

For a separate JavaScript/layout/paint investigation, close the app and launch
`JellyfinMediaPlayer.exe --webengine-devtools` once. The WebEngine inspector is
bound to `127.0.0.1:9222`. Restart normally to close it; the ordinary launch
removes inherited remote-debugging flags. Analyze native and Web profiles
separately, using timing and buffering evidence to distinguish GUI, WebEngine,
network and playback stalls.
