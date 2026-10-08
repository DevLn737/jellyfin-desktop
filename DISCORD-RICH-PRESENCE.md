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

Download the standalone `win64.exe` from this fork's GitHub Releases for an
installed copy. The portable ZIP is a separate alternative; downloading both is
unnecessary. The workflow uploads them separately and publishes a prerelease
only after tests and the Windows build succeed, without replacing the latest
stable SyncPlay release.

The default public Application ID is `1557831977250463766`. Its owner must name
the application **Jellyfin Desktop** in the
[Discord Developer Portal](https://discord.com/developers/applications).
The application name comes from Discord's registration, not the executable name.
The Application ID can be changed in Client Settings. An empty/invalid ID disables
the connection. Never enter a Client Secret or bot token.

The Discord build is version `1.12.4-discord.4` (Windows Installer version
`1.12.4`), so the installer upgrades existing `1.12.0` / SyncPlay and earlier Discord installations
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
| State | `Фильм · 2016` | `Сериал · S01 E02` |
| Large image | Movie poster | Series poster |
| Small image | Jellyfin logo | Jellyfin logo |
| Timer | Native progress bar when duration is known | Native progress bar when duration is known |

On pause the state becomes, for example, `Фильм · 2016 · Пауза · 12:34`. The timer keeps
its existing start timestamp through pauses, buffering and periodic refreshes,
instead of resetting to Discord's fallback session timer. Discord cannot freeze
its standard timer, so it continues advancing during a pause; the position in
the state text is the exact paused position. Resuming or seeking recalculates the
start time from the media position, excluding paused time. Both start and end
timestamps are sent when duration is available, enabling Discord's native Watching
time bar. Jellyfin's full runtime is preferred, with mpv duration as a fallback.
With unknown duration, only a start timestamp is sent for elapsed time. The bar
also advances during pauses and is corrected on resume. Discord controls the card
layout, bar colors, image size and which elements appear in compact views.
The native Watching heading communicates playback; hovering the Jellyfin logo
also shows `Просмотр`, `Пауза` or `Загрузка`. No buttons are added.

Discord renders the poster in a square image area and may crop portrait posters.
The Jellyfin image endpoint does not add square letterboxing; forcing both width
and height would distort the poster. Set an App Icon in the Discord Developer
Portal for the application's own logo. Member-list badges and their placement
are controlled by Discord, not by Rich Presence.

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

The feature is compiled and registered only on Windows.
Discord sources use the same UTF-8 compiler settings in production and tests;
MSVC character-conversion warning C4566 is an error for these sources.
Existing SyncPlay external-track support remains included. The workflow
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
credential stripping, local image fallback, elapsed/paused timestamps, progress bar
duration and fallback, absence of buttons, fragmented
IPC frames, handshake, PING/PONG, acknowledgements, update coalescing, clearing,
disconnect/reconnect, invalid IDs and oversized frames. They use a fake Discord
peer and do not modify a real Discord account.

Manual acceptance with real Discord:

- Movie: verify name, year, poster, Jellyfin overlay and elapsed playback time.
- Episode: verify series poster/name, season and episode, including specials.
- Pause/resume and seek: verify status and timer, allowing five seconds to update.
- Known duration: verify the native progress bar in the expanded Discord profile.
- Switch episodes, stop, playback failure and app exit: verify no stale activity.
- Restart Discord during playback: verify reconnection (allow up to 30 seconds).
- Disable/re-enable presence in Client Settings; verify removal and restoration.
- Try an inaccessible/local poster: playback and text presence must still work.

Protocol references: [Discord RPC](https://discord.com/developers/docs/topics/rpc),
[Rich Presence timestamps](https://discord.com/developers/discord-social-sdk/development-guides/setting-rich-presence#setting-timestamps).
