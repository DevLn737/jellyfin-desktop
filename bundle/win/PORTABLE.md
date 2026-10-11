The ordinary and diagnostic Windows ZIPs use the existing Jellyfin Media Player
AppData profile by default, so server connections, login and playback settings
can be compared without migrating a profile. The app-local Microsoft runtime
is included; no installer is needed to run the ZIP.

To use an isolated portable profile, create an empty file named `portable` or
`portable.txt` beside `JellyfinMediaPlayer.exe`, then launch the executable.
Settings, cookies and logs use `data/`; temporary cache uses `cache/` beside it.
The executable directory must be writable. This opt-in never copies the installed
profile automatically. Back up the existing profile before copying it manually.

Each isolated portable data directory has its own application instance. Launches
using the same canonical data directory focus the existing instance. Installed
and unmarked ZIP launches retain their shared AppData instance.

For one launch, `--webengine-devtools` enables DevTools on localhost port 9222.
Use `--webengine-devtools --webengine-devtools-port 9223` for a second isolated
instance. The port must be an ASCII integer from 1024 to 65535 and requires the
DevTools flag. Ordinary launches do not open a DevTools listener.

Keep `data/` and `cache/` when replacing application files with a newer isolated
portable version. Removing the marker returns to the existing AppData profile.
Builds can include the marker explicitly with `-IsolatedPortable` or CMake's
`JMP_PORTABLE_PROFILE=ON`; CI distributions omit it by default.
