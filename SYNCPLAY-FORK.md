# Jellyfin Media Player SyncPlay build

This fork keeps Jellyfin Media Player 1.12.0 behavior and adds the native mpv
track lifecycle needed by the coordinated SyncPlay project:

- independently load and select external audio tracks for each participant;
- wait for asynchronous mpv track discovery before selecting the new track;
- deduplicate pending external-audio additions;
- remove stale external-audio demuxers without exposing stream URLs in logs;
- keep external subtitle URLs local to each client.

The synchronized playback-rate protocol remains implemented by the paired
Jellyfin Server and Web forks. This Desktop patch is intentionally limited to
native external-track handling.

## Stable Windows release

The validated Windows x64 build is tagged `v1.12.0-syncplay.1` and published as
an [installer](https://github.com/DevLn737/jellyfin-desktop/releases/download/v1.12.0-syncplay.1/JellyfinMediaPlayer-1.12.0-syncplay.1-win64.exe)
and [portable ZIP](https://github.com/DevLn737/jellyfin-desktop/releases/download/v1.12.0-syncplay.1/JellyfinMediaPlayer-1.12.0-syncplay.1-portable-win64.zip).

The release passed two-client canary acceptance and a four-client production
stress session using independent audio and subtitle selections while SyncPlay
rate changes, pause/resume, seek, reconnect recovery and concurrent groups
remained synchronized.
