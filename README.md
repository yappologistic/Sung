<div align="center">

# Sung

**YouTube Music and your own music. Native on Linux.**

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![Linux](https://img.shields.io/badge/platform-Linux-blue.svg)
![Qt 6](https://img.shields.io/badge/built_with-Qt_6-41CD52.svg)

A minimal Material 3 player built with C++ and Qt Quick, designed for CachyOS and Wayland.

[Install](#install) · [Features](#features) · [Development](#development)

</div>

## Features

- **YouTube Music** — search songs, albums, artists and playlists; play audio without an embedded browser or ad interface.
- **Your music** — import FLAC, MP3 and other supported audio files or folders. Mix local and YouTube songs in the same playlists.
- **Lyrics** — synchronized lyrics, an immersive view, timing adjustments, LRC import and search with jump-to-line playback.
- **Library tools** — likes, listening history, smart mixes, playlist cleanup, multi-selection, drag reordering and Undo.
- **Playback controls** — mini player, queue editing, shuffle, repeat, sleep timer, playback speed and audio-device selection.
- **Desktop integration** — media keys through MPRIS, optional notifications, light/dark themes and Noctalia palette support.

Native rendering, one audio decoder and bounded artwork caches keep Sung lightweight. Animations can be disabled in Settings.

## Install

### CachyOS / Arch Linux

Install the build and runtime dependencies:

```bash
sudo pacman -S --needed git base-devel cmake ninja python nodejs ffmpeg qt6-base qt6-declarative qt6-multimedia qt6-svg qt6-wayland
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

Install the equivalent development packages for **Qt 6.8+** (Core, Gui, Quick, Qml, QuickControls2, Multimedia, Network, DBus, Svg and Wayland), a C++20 compiler, CMake 3.24+, Ninja, Python 3 with `venv`/`pip`, Node.js 20+ and FFmpeg. Then follow the clone and install commands above.

Sung uses Google Sans Flex when installed and otherwise falls back to a system font. Noctalia is optional.

## Getting started

Search for music or paste a YouTube song or playlist link. Use **Library → Local files → +** to add files, or **Folders → Add folder…** for a whole music folder. The refresh button rescans saved folders for new and changed audio.

Open a song’s menu to queue it, like it or add it to a playlist. Open **Clean up** in a local playlist to review duplicates and missing files; removal never deletes the original audio.

| Shortcut | Action |
| --- | --- |
| Space | Play / pause |
| Ctrl+F | Focus search |
| Ctrl+M | Toggle mini player |
| F11 | Toggle immersive player |
| Ctrl+A | Select songs in the focused list |
| Escape | Close the current view or clear selection |

### Accounts and saved data

Browsing is anonymous. Likes, playlists and history are stored locally and **do not sync with your Google account**. Settings offers library JSON import/export; audio files and imported LRC files are not bundled into exports.

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

The full integration suite is `./scripts/verify.sh`. It needs network access, a working audio session, Google Sans Flex, Qt Test, `qdbus6` and `dbus-run-session` (`qt6-tools` and `dbus` provide the command-line tools on Arch). It briefly plays audio at low volume and uses isolated test profiles.

Reports and screenshots are written to the ignored `verification/` directory. Do not attach raw logs or cookie files to issues; playback logs may contain signed media URLs. The Git allowlist keeps build outputs, runtime environments, personal media and development reports out of the repository.

## License

[MIT](LICENSE). Material Symbols are licensed under Apache-2.0; see [NOTICE](NOTICE) for third-party acknowledgments. Sung is an independent project and is not affiliated with Google or YouTube.

---

<div align="center">
  <a href="https://buymeacoffee.com/E_Gurl">
    <img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Support Sung on Buy Me a Coffee" width="217" height="60">
  </a>
</div>
