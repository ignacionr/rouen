# Rouen Multi-Modal UI Architecture

## Overview
Rouen's **Multi-Modal UI** bridges the native desktop interface, host background services, and external media endpoints by providing a real-time, low-latency 1080p @ 24fps video stream over HTTP.

External video clients (such as `mpv`, VLC, or browser streams) can connect to `http://127.0.0.1:8889` to receive a live visual stream rendered directly by Rouen and active deck cards.

---

## Architectural Components

```
┌───────────────────────────────────────────────────────────────────┐
│                           Rouen Engine                            │
│                                                                   │
│  ┌───────────────┐     ┌────────────────┐     ┌────────────────┐  │
│  │     Deck      │ ──> │ VideoFeedHost  │ <── │   Alarm Card   │  │
│  │ (Active Cards)│     │  (SDL_Surface) │     │ (Overlay HUD)  │  │
│  └───────────────┘     └───────┬────────┘     └────────────────┘  │
└────────────────────────────────┼──────────────────────────────────┘
                                 │ UNIX Pipe (raw RGB24 1920x1080)
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

## 1. VideoFeedHost (`src/hosts/video_feed_host.hpp`)

`VideoFeedHost` is a self-contained host managing video frame generation and `ffmpeg` lifecycle:

* **Process Management**: Uses `pipe()` and `fork()`/`execlp()` to spawn `ffmpeg`.
* **FFmpeg Command**:
  ```bash
  ffmpeg -loglevel warning -f rawvideo -pixel_format rgb24 -video_size 1920x1080 -framerate 24 -i pipe:0 -c:v libx264 -preset ultrafast -tune zerolatency -pix_fmt yuv420p -g 4 -f mpegts -listen 1 http://127.0.0.1:8889
  ```
* **SDL2 Surface Engine**: Driven by an `SDL_Surface*` (`SDL_PIXELFORMAT_RGB24`). Frame generation and text rendering operate on the surface memory.
* **Typography**: Features a built-in scalable 8x8 bitmap font engine (`draw_string_on_surface`) capable of rendering multi-colored scaled text overlays directly onto the surface.
* **ImGui Integration**: Provides `get_texture_id()`, converting the `SDL_Surface` into an `SDL_Texture` and returning an `ImTextureID` via Rouen's `sdl_texture_cast()`.
* **Process Safety**: Ignores `SIGPIPE` (`signal(SIGPIPE, SIG_IGN)`) and cleans up worker threads and process IDs to prevent crashes on shutdown or broken pipe reads.

---

## 2. Card Video Surface Painting Interface

Rouen cards can render custom HUD elements and overlays directly onto the video surface:

### Base Card Interface (`src/cards/interface/card.hpp`)
```cpp
virtual void paint_video_surface(SDL_Surface* surface, int surface_w, int surface_h) {}
```

### Deck Card Registry (`src/cards/interface/deck.hpp`)
`deck` registers a lookup function in `registrar`:
```cpp
registrar::add<std::function<std::vector<std::shared_ptr<card>>()>>(
    "get_active_cards",
    std::make_shared<std::function<std::vector<std::shared_ptr<card>>()>>(
        [this]() { return cards_; }
    )
);
```

During each frame render, `VideoFeedHost` queries `"get_active_cards"` and invokes `paint_video_surface(surface, width, height)` for each active card.

---

## 3. Alarm Card HUD Overlay (`src/cards/productivity/alarm.hpp`)

The **Alarm Card** overrides `paint_video_surface` to draw a real-time HUD container on the video stream:

* **Location**: Top-right container box (`surface_w - 520`, `y = 40`).
* **Active Countdown**: Renders `"ALARM CARD"`, `"COUNTDOWN ACTIVE"`, and remaining time (`HH:MM:SS`) in large typography.
* **Ringing State**: Flashes container background in bright red and displays `"*** ALARM RINGING! ***"`.
* **Inactive State**: Renders muted grey container with `"ALARM INACTIVE"`.

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

---

## 5. System Notifications Integration

`VideoFeedHost` uses Rouen's built-in `"notify"_sfn` notification service to display toast notifications:
* When the video feed starts: `"Video feed started at http://127.0.0.1:8889"`
* When the video feed stops: `"Video feed stopped"`
