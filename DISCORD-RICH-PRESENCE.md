# Discord Rich Presence (Windows)

This fork connects directly to the running Discord desktop client over Windows
named pipes using Qt Network. No extra DLL, background helper, bot token, login,
or Discord SDK installation is required.

## Use

1. Install the Windows Discord build, or extract the entire portable ZIP and run
   `JellyfinMediaPlayer.exe`.
2. Start Discord and allow activity sharing in Discord's privacy settings.
3. Play a movie or episode. In the player's **Client Settings → main**,
   **Discord Rich Presence** can be disabled at any time.

The default public Application ID is `1557831977250463766`. Its owner must name
the application **Jellyfin Desktop** in the
[Discord Developer Portal](https://discord.com/developers/applications).
The application name comes from Discord's registration, not the executable name.
The Application ID can be changed in Client Settings. An empty/invalid ID disables
the connection. Never enter a Client Secret or bot token.

The Discord build is version `1.12.1-discord.1` (Windows Installer version
`1.12.1`), so the installer upgrades the existing `1.12.0` / SyncPlay installation
without a manual uninstall. Close the player before updating. The existing
UpgradeCode and settings location are preserved. The portable ZIP can be
extracted to a separate directory, but uses the same user settings by default.
This build suppresses upstream update offers, since upstream packages do not
contain the fork's Discord integration. Install future fork builds manually.

## Display

| Field | Movie | Episode |
| --- | --- | --- |
| Application | Jellyfin Desktop | Jellyfin Desktop |
| Details | Movie title | Series title |
| State | `2016 · Watching` | `S01 E02 · Watching` |
| Large image | Movie poster | Series poster |
| Small image | Jellyfin logo | Jellyfin logo |
| Timer | Elapsed playback position | Elapsed playback position |

On pause the state becomes, for example, `2016 · Paused · 12:34` and the live
timer is removed. Discord cannot freeze its standard timestamp timer. Resuming
or seeking recalculates its start time from the media position. Buffering also
suspends the timer. Only a start timestamp is sent: adding an end timestamp makes
Discord display remaining time instead of elapsed time. Discord controls the
card layout; a graphical progress bar is not guaranteed.

The timer ticks at Discord's standard 1× rate. At other playback speeds it is
corrected from mpv's actual position every 15 seconds; seeks and larger drift
also request updates. Activity updates are coalesced to at most once every five
seconds. Clearing after stop is not throttled (it waits for any in-flight ACK).
Exiting the app or disabling integration disconnects IPC and removes the activity.
Music and other unsupported item types do not publish a movie presence.

## Posters and privacy

Discord fetches images from its own servers. A poster requires a publicly
reachable HTTPS Jellyfin image endpoint that works **without authentication**.
The integration builds `/Items/{id}/Images/Primary` URLs and preserves server
subpaths such as `/jellyfin`. It uses the series primary image for episodes.
It never sends playback URLs, authentication parameters, credentials, or API keys
to Discord. Obvious local hosts and IP literals are not used for posters.

For local servers or missing image metadata, the large image falls back to the
Jellyfin logo. A public-looking hostname that actually requires a login or is
unreachable to Discord can still yield a missing image; the client cannot inspect
Discord's image-proxy result. The integration does not upload posters to a third
party or change your server's access controls. Enabling presence shares the title,
episode/year, playback status and position; poster URLs also expose the server
hostname and relevant item/image identifiers.

## Build and test

The feature is compiled and registered only on Windows. Existing SyncPlay
external-track support remains included. The workflow
`.github/workflows/windows-discord-presence.yml` runs the existing JavaScript
regression test, builds/runs the Qt activity and named-pipe tests on Windows,
and builds installer/portable artifacts. It does not replace the SyncPlay release.

To run the standalone tests with Qt 5.15 and CMake:

```text
cmake -S tests/discord -B build/discord-tests -DCMAKE_PREFIX_PATH=<Qt directory>
cmake --build build/discord-tests --config Release
ctest --test-dir build/discord-tests -C Release --output-on-failure
```

Tests cover metadata formatting, series posters, missing data, Unicode limits,
credential stripping, local image fallback, elapsed/paused timestamps, fragmented
IPC frames, handshake, PING/PONG, acknowledgements, update coalescing, clearing,
disconnect/reconnect, invalid IDs and oversized frames. They use a fake Discord
peer and do not modify a real Discord account.

Manual acceptance with real Discord:

- Movie: verify name, year, poster, Jellyfin overlay and elapsed playback time.
- Episode: verify series poster/name, season and episode, including specials.
- Pause/resume and seek: verify status and timer, allowing five seconds to update.
- Switch episodes, stop, playback failure and app exit: verify no stale activity.
- Restart Discord during playback: verify reconnection (allow up to 30 seconds).
- Disable/re-enable presence in Client Settings; verify removal and restoration.
- Try an inaccessible/local poster: playback and text presence must still work.

Protocol references: [Discord RPC](https://discord.com/developers/docs/topics/rpc),
[Rich Presence timestamps](https://discord.com/developers/discord-social-sdk/development-guides/setting-rich-presence#setting-timestamps).
