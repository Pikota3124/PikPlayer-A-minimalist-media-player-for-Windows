# PikPlayer

A minimalist media player for **Windows 10/11 (x64)**, inspired by Movies & TV app from Windows: dark UI, floating controls that auto-hide, and minimalist as possible.

Powered by **[libmpv](https://mpv.io/)** (FFmpeg under the hood), with first-class support for uncommon formats — including **CRI Sofdec2 `.usm`** game videos — and real **Explorer thumbnails** for every registered type.

---

## Features

- **Wide format support** — everything libmpv/FFmpeg can open (MP4, MKV, WebM, AVI, MOV, TS, audio, …)
- **Player's accent color** — It will change based on the Windows color being used.
- **`.usm` (CRI Sofdec2)** — demux of VP9 / H.264 / MPEG-1 video and ADX audio without external tools
- **Exact seeking** on the timeline for video and audio (no jumping back to the previous keyframe)
- **Explorer thumbnails** via `PikPlayerThumbnail.dll` (real frames, including USM; uniform **16:9** canvas)
- **Audio visualizer** when playing audio-only files
- **Drag & drop**, Open With, playlist-style navigation within a folder
- **Single self-contained installer** — no extra runtimes to download on the target PC


---


## Supported media (overview)

**Video**  
`.mp4` `.mkv` `.usm` `.avi` `.mov` `.wmv` `.webm` `.flv` `.m4v` `.mpg` `.ts` `.m2ts` `.vob` `.3gp` `.ogv` and more.

**Audio**  
`.mp3` `.flac` `.m4a` `.aac` `.ogg` `.opus` `.wav` `.wma` `.ape` `.ac3` `.dts` and more.

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


## Overview

Interface is everything on the bottom, cleanest as possible

<img width="3839" height="2159" alt="Captura de pantalla 2026-10-06 175129" src="https://github.com/user-attachments/assets/682306a0-0691-4c46-b3d4-6b810e454ea9" />


---


On the right side there are several options, such as a panel with video information, selectors for subtitles, audio, or playback speed, as well as a button to enter full-screen mode.

<img width="1126" height="878" alt="Screenshot 2026-10-06 175605" src="https://github.com/user-attachments/assets/fa862c13-ab9a-4e6c-8c92-f394ef6590af" />

<img width="1951" height="1447" alt="Screenshot 2026-10-06 175534" src="https://github.com/user-attachments/assets/e33f5d95-a8ad-4156-90c6-c5b8eeaa619b" />


---


Thumbnails are generated using a dedicated DLL, so the first time it will take a few seconds for them to generate. This is because Windows lacks the ability to create thumbnails for .usm files or for certain .mkv, .mp4, and other files with special characteristics.

<img width="2233" height="1257" alt="Screenshot 2026-10-06 175704" src="https://github.com/user-attachments/assets/056ec648-459d-4a8e-9b04-a86477ea2d90" />


---


This is what an audio file without cover art looks like while playing.

<img width="3839" height="2159" alt="Screenshot 2026-10-06 175731" src="https://github.com/user-attachments/assets/34e8ad60-e9f2-485c-9c28-bfc2e9116d2f" />


---


## Disclosure

This project was entirely made with AI with Claude + Chatgpt


