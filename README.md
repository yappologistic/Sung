<div align="center">

<img src="assets/readme-banner.png" alt="Sung showing music collections and synchronized lyrics" width="100%">

<a href="https://buymeacoffee.com/e_gurl">
  <img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Support Sung on Buy Me a Coffee" width="217" height="60">
</a>

# Sung

**YouTube Music, your music files, and your music server. Native on Linux.**

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![Linux](https://img.shields.io/badge/platform-Linux-blue.svg)
![Qt 6](https://img.shields.io/badge/built_with-Qt_6-41CD52.svg)

A minimal Material 3 player built with C++ and Qt Quick, designed for CachyOS and Wayland.

[Install](#install) · [Features](#features) · [Development](#development)

</div>

## Features

- **YouTube Music** — search songs, albums, artists and playlists; play audio without an embedded browser or ad interface.
- **Navidrome / Subsonic** — browse and search your server, play original or transcoded audio, edit server playlists, rate songs, sync favorites and listening history, and display server lyrics.
- **Jellyfin** — browse music libraries, albums, artists and genres; search, stream original or transcoded audio, manage permitted server playlists, sync favorites and display synchronized lyrics.
- **Your music** — import FLAC, MP3 and other supported audio files or folders; browse albums and artists, search paths and group songs by folder. Mix local and YouTube songs in the same playlists.
- **Animated artwork** — local animated covers and automatic online covers for matching YouTube songs, shared across the player, immersive view and mini player; lists use still covers.
- **Lyrics** — synchronized lyrics, an immersive view, timing adjustments, LRC import, seek previews and search with jump-to-line playback.
- **Library tools** — likes, listening history, smart mixes, custom smart playlists, custom playlist covers, playlist cleanup, multi-selection, drag reordering and Undo.
- **Playback controls** — mini player, queue editing with source headings, shuffle, repeat, sleep timer, playback speed and audio-device selection.
- **Desktop integration** — media keys through MPRIS, optional notifications, light/dark themes and Noctalia palette support.

Native rendering and bounded artwork caches keep Sung lightweight. Animated covers share one additional decoder, released when the player is hidden. Animations can be disabled in Settings.

## Install

### CachyOS / Arch Linux

Install the build and runtime dependencies:

```bash
sudo pacman -S --needed git base-devel cmake ninja python nodejs ffmpeg qt6-base qt6-declarative qt6-multimedia qt6-svg qt6-wayland qt6-imageformats
```

Download and install Sung:

```bash
git clone https://github.com/yappologistic/Sung.git
cd Sung
./scripts/install.sh
```

Open **Sung** from your application menu, or run:

```bash
~/.local/bin/sung
```

Installation is per-user in `~/.local`; do not run the install script with `sudo`. Python dependencies are installed in an isolated environment. Internet access is needed during installation and for YouTube playback.

### Other Linux distributions

Install the equivalent development packages for **Qt 6.8+** (Core, Gui, Quick, Qml, QuickControls2, Multimedia, Network, DBus, Svg and Wayland), a C++20 compiler, CMake 3.24+, Ninja, Python 3 with `venv`/`pip`, Node.js 20+ and FFmpeg. Install the Qt image-format plugins for WebP artwork. Then follow the clone and install commands above.

Sung uses Google Sans Flex when installed and otherwise falls back to a system font. Noctalia is optional.

## Getting started

### Music library

Search for music or paste a YouTube song or playlist link. Use **Library → Local files → +** to add files, or **Folders → Add folder…** for a whole music folder. Enter its absolute path (or `~/Music`), or use **Browse…**, then choose **Add folder**. This also works for network shares mounted as local folders and does not depend on the system folder picker. Subfolders are scanned recursively. Saved folders update automatically while Sung is running. Settings can disable automatic updates; the refresh button also rescans them. Missing files remain listed as unavailable; files and playlist entries are never deleted by a scan. Folder monitoring uses filesystem notifications and is bounded to 4,096 directories and 10,000 audio files; use manual rescan for larger libraries or mounts that do not deliver notifications.

In **Local files**, open **Find and sort songs → Folder** to group songs by their parent directory, with natural filename order inside each group. The filter also searches folder paths.

Create an automatic playlist from **Library → Playlists → Smart playlist**. Combine artist, title, source, liked status and last-played rules over your saved music. Use **Edit rules** to change it; matching songs update automatically.

**Local files → Albums / Artists** groups imported music by its tags. Albums use album-artist tags when present, with disc and track order preserved. Use **Rescan** after upgrading to refresh tags on existing imports. Folder-sorted songs and multi-disc albums have collapsible group headings with track counts and group-play buttons. Collapsed songs stay in the collection but are excluded from selection.

In a playlist’s menu, choose **Change cover…** to crop a PNG, JPEG or WebP. Sung saves a 512px copy; the original stays untouched. **Restore cover collage** returns to automatic artwork.

### Artwork and appearance

Open **Home → Customize Home** to reorder or hide sections; **Reset layout** restores them. **Settings → Library → Start page** chooses Home, Local, Server or Liked for future launches. Direct launch links still take priority.

**Settings → Appearance → Current view layout** saves a density override for the current view. Local album/artist browsers and the playlist overview also offer Grid / List. Choose Default density to follow the global setting. Up to 64 view preferences are retained locally.

**Settings → Appearance → Density** switches between comfortable and compact track rows and album grids without changing font size. Density changes animate when motion is enabled. Opening an album carries its cover into the header; Back returns it to the originating card when visible. The header contracts as you scroll while keeping playback actions available.

For animated artwork, place a **GIF, animated WebP, MP4 or WebM** beside your music, named `cover`, `folder`, `front` or `artwork` (for example, `cover.mp4`). A matching song filename, such as `Song.gif` beside `Song.flac`, takes priority. Names are case-insensitive. JPG, PNG and static WebP sidecars also work as still covers. Import or rescan the folder after changing its artwork. Covers are limited to 128 MiB and 4096 × 4096 pixels; unreadable covers fall back to embedded artwork. Animation is silent, pauses with playback, and respects **Settings → Appearance → Animations** and **Animated album artwork**. No artwork service account is needed.

For YouTube songs, **Online animated covers** looks for a matching album on Apple Music’s public pages. This unofficial, best-effort lookup needs no account; it sends the song’s title and artist to Apple and checks the album and duration when available. Singles can use artwork from a verified original album release; missing search results are checked against the album’s track list. Many albums have no animation, and uncertain matches keep the original still cover. Temporary lookup failures get one automatic retry. Downloads are limited to 16 MiB per silent cover and 64 MiB of disk cache. Disable the lookup in Settings or remove downloaded covers with **Clear cache**.

Open **Settings → Appearance → Current artwork** to preview the current cover, view its source album, retry a match, disable animation for that song or choose a local GIF, WebP, MP4 or WebM. Local choices are saved per song and reference the selected file; keep it in place. **Use automatic cover** clears the override.

**Settings → Appearance → Use artwork accent** colors controls from the current cover. It is off by default; desktop surfaces and Noctalia integration are preserved. Monochrome or missing covers use the normal theme.

The artwork controls also offer **Fit / Fill**, remembered per album where album metadata is available, otherwise per song. Immersive artwork requests a display-sized still cover up to 1600px; source quality remains the limit. Artwork accents transition smoothly when animations are enabled. Next and Previous move song information in opposite directions. Player covers crossfade between songs; transitions stop when hidden or animations are disabled.

Click album or immersive artwork to inspect the full cover. Use the wheel or + / − to zoom, 0 to reset, and Escape to close. The viewer uses available source detail, capped at 1600px. You can also click the preview in **Current artwork**.

### Playback and shortcuts

**Settings → Playback → Fade out before sleep** lowers the audio over the last 30 seconds of a timed or end-of-track sleep timer. Your chosen volume stays saved and is restored when the timer ends or is cancelled.

Queue headings distinguish songs added manually, collection tracks and autoplay recommendations when their origin is known. These labels preserve playback order, including after dragging songs. Older queues without origin information retain source headings.

Hold **Shift while dragging the seek bar** for fine seeking; the new position applies when you release. **Shift+Left / Right** seeks by 100ms. Escape cancels a fine drag.

Click the volume icon for a slider and an exact percentage. Enter a value from 0 to 100 and press Enter or Apply. This works in the main, mini and immersive players.

Timed lyrics show a countdown during intros and explicit gaps of at least five seconds. Sung uses supplied line boundaries or blank timed lines; it does not infer instrumental passages from a long lyric line. Timing adjustments apply to the countdown.

The queue shows remaining time and a finish estimate during uninterrupted playback. Unknown durations, random shuffle, repeat, autoplay or a sleep timer can make a finish estimate unavailable.

Album pages show the artist, release year when available, track count and duration, with disc headings when the source supplies disc numbers. Drag the lyrics/queue divider to resize the panel; double-click it to reset. Its width is remembered.

Press **Ctrl+Shift+P** for quick actions, saved playlists and audio outputs. Type to filter, use the arrow keys, then press Enter.

**Listening sessions** in Settings or Quick Actions save your queue, song position, speed, shuffle, repeat and autoplay settings. Resume asks before replacing the current queue. Sessions can be renamed, updated or deleted; up to 20 sessions of 2,000 songs each are kept locally.

The arrow beside the player’s volume controls opens an audio-output picker. It remains available in narrow windows.

**Pause when audio output disconnects** is optional. Sung pauses when the selected device disappears; wired headphone-port detection uses `pactl` from `libpulse`. Reconnecting does not automatically resume playback.

**Track details** shows playback codec, bitrate and decoded sample rate/channels when reported by the decoder. Local file metadata is labeled separately. Missing values are omitted.

**Settings** groups controls into Appearance, Playback, Library, Connections, and Privacy & data. Search finds controls across all categories. Narrow windows use a category selector.

Open a song’s menu to queue it, like it or add it to a playlist. Local playlist additions skip duplicates and can be undone. Views remember their filter, sort and scroll position during the session. Open **Clean up** in a local playlist to review duplicates and missing files; removal never deletes the original audio.

| Shortcut | Action |
| --- | --- |
| Space | Play / pause |
| Ctrl+F | Focus search |
| Ctrl+Shift+P | Quick actions |
| Ctrl+J | Show the playing song in the queue |
| ? / F1 | Keyboard shortcut reference (outside text fields) |
| Ctrl+M | Toggle mini player |
| F11 | Toggle immersive player |
| Ctrl+A | Select songs in the focused list |
| Escape | Close the current view or clear selection |

### Connect a music server

Open **Settings → Connections → Music server** and choose **Subsonic** (including Navidrome) or **Jellyfin**, and enter your server address, username and password. Use the server root, including any deployment subpath, without `/rest` or `/web`. Use HTTPS for remote servers.

Open **Library → Music server** to browse. The main search bar searches your server while this view is open. The server menu offers library selection and playlist creation. Permitted playlists support renaming, song removal and drag reordering; deletion requires owner or administrator permissions. Subsonic also offers ratings and server queue save/restore. Jellyfin shared playlists respect the server’s editing permissions.

Local playlists can mix YouTube, local files and server songs. Server playlists accept songs from that server only. One server account can be connected at a time. Server lyrics use synchronized lyrics when available, otherwise plain text.

**Remember in desktop keyring** uses `secret-tool` (the `libsecret` package on Arch) and a running Secret Service provider. If the keyring is unavailable, the connection works for the current session. Jellyfin saves its session token in the keyring instead of its password. Passwords and authenticated URLs are not saved in library exports. Disconnect removes the saved login.

Connection settings include audio quality and server listening history. Original audio is buffered on disk before playback, with a 512 MiB limit per song; choose a lower bitrate for very large files. Server transcoding must be available for the selected bitrate. Subsonic listening history is submitted after half a song or four minutes of playback, whichever comes first. Jellyfin receives playback status and progress and manages its own play counts. Private listening disables these reports.

Tested against Navidrome 0.63.2 and Jellyfin 10.11.11 / 12.0. Other servers must support Subsonic 1.16.1 token authentication and JSON responses. OpenSubsonic lyrics and form POST are detected when available. Jellyfin 10.11 removes duplicate playlist additions on the server; 12.0 preserves them. Jellyfin collections are paginated; a single opened collection is limited to 20,000 items. Server administration, video, podcasts, remote-device control and permanent offline downloads are outside this music integration.

### Accounts and saved data

YouTube browsing is anonymous. YouTube likes, local playlists and local history are stored locally and **do not sync with your Google account**. Settings offers library JSON import/export; audio files, custom cover images and imported LRC files are not bundled into exports.

For streams requiring sign-in, Settings can import a user-selected Netscape-format cookie file. Sung does not read your browser profile. Cookies can be removed in Settings.

Library data is stored in `~/.local/share/Sung/sung/`, settings in `~/.config/Sung/`, and cache in `~/.cache/Sung/sung/`. Standard XDG directory overrides are respected. Sung has no analytics or telemetry. Optional LRCLIB lyric lookups send the song’s title, artist and duration; they can be disabled in Settings.

### Troubleshooting

Playback depends on YouTube availability, region and network conditions. Sung buffers audio before playing, so starting a song can take a moment. It does not remove sponsor segments embedded in recordings or promise gapless playback.

If YouTube playback stops working after an upstream change, update the resolver:

```bash
~/.local/lib/sung/runtime/bin/python -m pip install --upgrade 'yt-dlp[default]' ytmusicapi
```

To update Sung, quit the player, then run `git pull` and `./scripts/install.sh` from this checkout. To uninstall, run `./scripts/uninstall.sh`; your library and settings are kept.

## Development

Build and run from the checkout:

```bash
./scripts/setup.sh
./scripts/build.sh
./scripts/run.sh
```

Run automated tests:

```bash
./scripts/test.sh
./scripts/verify.sh --offline
```

The full integration suite is `./scripts/verify.sh`. It needs network access, a working audio session, Google Sans Flex, Qt Test, `qdbus6` and `dbus-run-session` (`qt6-tools` and `dbus` provide the command-line tools on Arch). It briefly plays audio at low volume and uses isolated test profiles. The online-artwork checks compare fresh downloads and cached replay across five albums, then verify native rendering with muted YouTube playback. These live checks depend on the albums remaining available.

Reports and screenshots are written to the ignored `verification/` directory. Do not attach raw logs or cookie files to issues; playback logs may contain signed media URLs. The Git allowlist keeps build outputs, runtime environments, personal media and development reports out of the repository.

To test the server integration, build the test targets and provide a Navidrome executable:

```bash
./scripts/test.sh
python3 tests/navidrome_integration.py \
  --navidrome /path/to/navidrome \
  --test-binary build-tests/sung-subsonic-tests \
  --output verification/navidrome
```

The script starts a loopback-only server, creates a temporary account and 105 generated audio fixtures, and tests browsing, playback, seeking, lyrics, playlist edits, ratings, favorites, queue restoration and scrobbling. It stops the server and removes its temporary data afterward. Use a new output directory for each run. With a diagnostics build, add `--ui-binary /path/to/sung` to exercise the rendered interface too.

To test Jellyfin with generated music and disposable accounts:

```bash
python3 tests/jellyfin_integration.py \
  --server-binary /path/to/jellyfin \
  --test-binary build-tests/sung-jellyfin-tests \
  --output verification/jellyfin
```

The server binds to loopback only and stops after testing. Test data and credentials stay in the private output directory; remove it when finished. Add `--ui-binary /path/to/sung` for rendered UI checks, or additionally `--native-ui` to use Hyprland workspace 2. The normal test suite also checks malformed responses, redirects, cancellation, credential persistence and failed downloads using a local mock server.

## License

[MIT](LICENSE). Material Symbols are licensed under Apache-2.0; see [NOTICE](NOTICE) for third-party acknowledgments. Sung is an independent project and is not affiliated with Google or YouTube.
