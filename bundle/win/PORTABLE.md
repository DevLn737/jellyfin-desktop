The ordinary and diagnostic Windows ZIPs use the existing Jellyfin Media Player
AppData profile by default, so server connections, login and playback settings
can be compared without migrating a profile. The app-local Microsoft runtime
is included; no installer is needed to run the ZIP.

To use an isolated portable profile, create an empty file named `portable` or
`portable.txt` beside `JellyfinMediaPlayer.exe`, then launch the executable.
Settings, cookies and logs use `data/`; temporary cache uses `cache/` beside it.
The executable directory must be writable. This opt-in never copies the installed
profile automatically. Back up the existing profile before copying it manually.

Keep `data/` and `cache/` when replacing application files with a newer isolated
portable version. Removing the marker returns to the existing AppData profile.
Builds can include the marker explicitly with `-IsolatedPortable` or CMake's
`JMP_PORTABLE_PROFILE=ON`; CI distributions omit it by default.
