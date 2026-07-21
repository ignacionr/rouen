# Rouen Multi-Modal UI Architecture

## Overview
Rouen's **Multi-Modal UI** bridges the native desktop interface, host background services, and external media endpoints by providing a real-time, low-latency 1080p @ 24fps video stream over HTTP.

External video clients (such as `mpv`, VLC, or browser streams) can connect to `http://127.0.0.1:8889` to receive a live visual stream rendered with the full richness of Rouen's ImGui interface.

---

## Architectural Components

```
┌───────────────────────────────────────────────────────────────────────┐
│                             Rouen Engine                              │
│                                                                       │
│  ┌──────────────────┐    ┌────────────────────┐    ┌───────────────┐  │
│  │   Main ImGui     │    │ VideoFeed Host     │    │  Alarm Card   │  │
│  │   Context        │    │ (Secondary ImGui   │    │  ImGui UI     │  │
│  │ (Desktop Window) │    │   Context 1080p)   │    │  (render_     │  │
│  └──────────────────┘    └─────────┬──────────┘    │   video_ui)   │  │
└────────────────────────────────────┼───────────────┴───────────────┘  │
                                     │ UNIX Pipe (RGB24 1920x1080)
                                     ▼
                             ┌───────────────┐
                             │ ffmpeg child  │
                             │ (-listen 1)   │
                             └───────┬───────┘
                                     │ HTTP Stream (http://127.0.0.1:8889)
                                     ▼
                             ┌───────────────┐
                             │ mpv / Client  │
                             └───────────────┘
```

---

## 1. Offscreen ImGuiContext Engine (`src/hosts/video_feed_host.hpp`)

`VideoFeedHost` uses a secondary `ImGuiContext` (`video_imgui_ctx_`) bound to an offscreen target texture (`1920x1080`):

* **Context Switching**: Saves the main desktop context via `ImGui::GetCurrentContext()`, switches to `video_imgui_ctx_`, sets `DisplaySize = (1920, 1080)`, and executes an offscreen ImGui frame.
* **Process Management**: Uses `pipe()` and `fork()`/`execlp()` to spawn `ffmpeg`.
* **FFmpeg Command**:
  ```bash
  ffmpeg -loglevel warning -f rawvideo -pixel_format rgb24 -video_size 1920x1080 -framerate 24 -i pipe:0 -c:v libx264 -preset ultrafast -tune zerolatency -pix_fmt yuv420p -g 4 -f mpegts -listen 1 http://127.0.0.1:8889
  ```
* **Thread Safety**: Offscreen rendering runs safely on the main thread during `main_wnd::run()` after `SDL_RenderPresent()`.
* **Pixel Transfer**: Reads rendered 1080p frames back to RAM using `SDL_RenderReadPixels` and streams raw RGB24 bytes to the `ffmpeg` pipe.
* **Process Safety**: Ignores `SIGPIPE` (`signal(SIGPIPE, SIG_IGN)`) and cleans up worker threads, offscreen textures, and process IDs to prevent crashes on shutdown or broken pipe reads.

---

## 2. Card Offscreen ImGui UI Interface

Rouen cards can render full ImGui widgets, progress bars, vector shapes, rounded panels, and custom themes on the video surface:

### Base Card Interface (`src/cards/interface/card.hpp`)
```cpp
virtual void render_video_ui() {}
```

During each video frame render, `VideoFeedHost` queries `"get_active_cards"` from `registrar` and invokes `render_video_ui()` for each active card.

---

## 3. Alarm Card ImGui Video UI (`src/cards/productivity/alarm.hpp`)

The **Alarm Card** overrides `render_video_ui()` to render a rich ImGui window on the video stream:

* **Location & Styling**: Positioned at `(1380, 40)` with width `500`, 16px rounded corners, and colored borders.
* **Active Countdown**: Renders `"⏰ ALARM CARD"`, `"COUNTDOWN ACTIVE"`, large timer text (`HH:MM:SS`), and an interactive `ImGui::ProgressBar`.
* **Ringing State**: Flashes red window background with yellow border and displays `"🚨 ALARM RINGING!"`.
* **Inactive State**: Renders muted window styling with `"ALARM INACTIVE"`.

---

## 4. Client Script & Low-Latency Options

A convenience script is provided in `scripts/play_videofeed.sh`:

```bash
./scripts/play_videofeed.sh
```

This launches `mpv` with low-latency settings:
```bash
mpv --profile=low-latency --no-cache --framedrop=vo --demuxer-lavf-o=fflags=nobuffer http://127.0.0.1:8889
```
