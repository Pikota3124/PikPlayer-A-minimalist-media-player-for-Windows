# PikPlayer

A minimalist media player for **Windows 10/11 (x64)**, inspired by the Movies & TV app: dark UI, floating controls that auto-hide, and as little clutter as possible.

Powered by **[libmpv](https://mpv.io/)** (FFmpeg under the hood), with first-class support for uncommon formats — including **CRI Sofdec2 `.usm`** game videos — and real **Explorer thumbnails** for every registered type.

---

## Features

- **Clean playback UI** — dark window, floating control bar, fullscreen, next/previous in folder
- **Wide format support** — everything libmpv/FFmpeg can open (MP4, MKV, WebM, AVI, MOV, TS, audio, …)
- **`.usm` (CRI Sofdec2)** — demux of VP9 / H.264 / MPEG-1 video and ADX audio without external tools
- **Exact seeking** on the timeline for video and audio (no jumping back to the previous keyframe)
- **Explorer thumbnails** via `PikPlayerThumbnail.dll` (real frames, including USM; uniform **16:9** canvas)
- **Audio visualizer** when playing audio-only files
- **Drag & drop**, Open With, playlist-style navigation within a folder
- **Single self-contained installer** — no extra runtimes to download on the target PC
- **Repair / uninstall** from the setup wizard or Windows Settings

---

## Requirements

| | |
|---|---|
| **OS** | Windows 10 or 11, **64-bit** |
| **To run** | Nothing else — libmpv is embedded |
| **To build** | MSVC (Visual Studio Build Tools), Internet once for libmpv |

---

## Install (end users)

1. Run **`PikPlayer-Setup.exe`** (administrator).
2. Optional: desktop shortcut and default-app settings.
3. Launch from Start Menu or *Open with*.

Uninstall from **Settings → Apps → PikPlayer**, via **`Uninstall.exe`** in the install folder, or by running the setup again and choosing *Uninstall*.

---

## Build from source

```bat
Compilar.bat
```

What it does:

1. Locates or installs **MSVC** Build Tools (via `winget` if needed).
2. Resolves **libmpv** (`libs\mpv`, then a local cache, then download once).
3. Builds `PikPlayer.exe`, `PikPlayerThumbnail.dll`, `Uninstall.exe`.
4. Packs and encrypts them into **`dist\PikPlayer-Setup.exe`**.

Force a fresh libmpv download:

```bat
Compilar.bat /updatempv
```

Internet is only required while building. The resulting setup runs offline on any Windows 10/11 x64 machine.

---

## Supported media (overview)

**Video / containers:**  
`.mp4` `.mkv` `.avi` `.mov` `.wmv` `.webm` `.flv` `.m4v` `.mpg` `.ts` `.m2ts` `.vob` `.3gp` `.ogv` `.usm` and more.

**Audio:**  
`.mp3` `.flac` `.m4a` `.aac` `.ogg` `.opus` `.wav` `.wma` `.ape` `.ac3` `.dts` …

USM files are detected by extension or by the `@SFV` signature and demuxed before playback.

---

## Keyboard shortcuts

| Key | Action |
|-----|--------|
| `Space` | Play / pause |
| `←` / `→` | Seek ±5 s |
| `J` / `L` | Seek ±10 s |
| `Shift`+`←` / `→` | Seek ±30 s |
| `↑` / `↓` | Volume |
| `M` | Mute |
| `F` / `Enter` | Fullscreen |
| `Esc` | Exit fullscreen / close overlays |
| `Ctrl`+`O` | Open file |
| `N` / `P` | Next / previous in folder |
| `I` | File info panel |

Mouse: seek and volume on the floating bar; scroll wheel adjusts volume.

---

## Explorer thumbnails

`PikPlayerThumbnail.dll` implements `IThumbnailProvider`. Explorer asks for a thumbnail; the DLL runs:

```text
PikPlayer.exe /thumb <file> <output.jpg> <size>
```

in the background (libmpv + USM demux when needed). Frames are fitted into a **16:9** bitmap so icons line up with typical video thumbnails (letterboxing for square covers, etc.).

After reinstalling, clear Explorer’s thumbnail cache if old icons stick:

```bat
taskkill /f /im explorer.exe
del /f /s /q %localappdata%\Microsoft\Windows\Explorer\thumbcache_*.db
start explorer.exe
```

---

## Project layout

```text
Pikplayer/
├── Compilar.bat          # one-click build → dist\PikPlayer-Setup.exe
├── src/
│   ├── main.cpp          # player UI + libmpv + /thumb helper
│   ├── usm.cpp / usm.h   # Sofdec2 demuxer
│   ├── thumbnail.cpp     # Explorer IThumbnailProvider DLL
│   ├── setup.cpp         # installer UI
│   ├── uninstall.cpp     # Uninstall.exe
│   └── install_common.h  # registry / associations
├── tools/                # libmpv fetch + payload packer
├── libs/mpv/             # libmpv (filled by the build)
└── dist/                 # output installer (after build)
```

---

## Notes

- **Seeking:** timeline seeks use `absolute+exact` so the playhead matches the click position on video and audio.
- **Installer payload:** embedded binaries are packed/encrypted so a casual resource editor cannot pull plain PE files out of the setup.
- **Elevation:** if you open the player from an elevated installer session, drag-and-drop from Explorer may be blocked by Windows; launch PikPlayer from the Start Menu instead.

---

## License

Add your license here (e.g. MIT).  
libmpv / FFmpeg and other third-party components retain their own licenses.
