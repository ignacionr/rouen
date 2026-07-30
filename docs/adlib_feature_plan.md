# Rouen Ad-Lib Support Architecture Plan

## Executive Summary
This document outlines the design and implementation strategy for adding **Live and Recorded Ad-Libs** to the Rouen video & presentation system. An **Ad-Lib** is a structured presentation sequence consisting of:
1. **Intro Video**: An opening video sequence.
2. **Fixed Background (Presentation Phase)**: A custom static image, graphic, or background color shown while the presenter speaks. Active deck cards can overlay dynamic UI widgets (e.g. Git status, Weather, RSS highlights, AI companion text, lower-thirds) on top of this background. Overlays can be toggled live using existing deck hotkeys and visibility controls.
3. **Transition-Out Video**: A closing video sequence.

Ad-Libs support two primary operation modes:
- **Live Mode**: Plays the sequence on Rouen's detached window, suitable for window sharing in video conferencing apps (Zoom, Teams, Google Meet), where audio/video distribution is handled by the conference software.
- **Recorded Mode**: Generates a synchronized MP4 file pipeline capturing exact visual frames from the detached window (background + intro/outro videos + card overlays) merged with audio from the intro/outro videos and a selected audio input device (Microphone) during the presentation phase.

---

## Architecture Overview

```
+-----------------------------------------------------------------------------------+
|                                  AdLibCard (UI)                                   |
|  - Template Config (Intro video, Background, Outro video, Mic Input, Output Path) |
|  - Execution Controls (Prepare, Play/Pause, Stage Skip, Stop, Mode: Live/Rec)     |
+----------------------------------------+------------------------------------------+
                                         |
                                         v
+-----------------------------------------------------------------------------------+
|                                  AdLibEngine                                      |
|  - Controls stage transitions: INTRO -> MIDDLE (Presentation) -> OUTRO            |
|  - Coordinates video decoders for Intro/Outro videos                              |
|  - Drives Detached Window Surface rendering                                       |
+-------------------+------------------------------------+--------------------------+
                    |                                    |
                    v                                    v
+---------------------------------------+  +----------------------------------------+
|        Detached Window Render         |  |         Recorded MP4 Pipeline          |
|  - Draws Video Frame / Background     |  |  - Frame Grabber (GPU/Pixel Buffer)   |
|  - Renders Deck Card Overlays         |  |  - Audio Capture (Mic via SDL Audio)   |
|  - High-precision frame pacing        |  |  - NativeMP4Encoder (H.264 + AAC MP4) |
+---------------------------------------+  +----------------------------------------+
```

---

## Detailed Components & Implementation Steps

### Phase 1: Ad-Lib Template & Card Definition (`src/cards/production/adlib.hpp`, `adlib.cpp`)
- **Card Schema**: `adlib:default` / `adlib:<template_id>`.
- **Card State**:
  - `std::string intro_path`: Path to intro video file.
  - `std::string background_path`: Path to background image (or solid hex color).
  - `std::string outro_path`: Path to transition-out video file.
  - `SDL_AudioDeviceID selected_mic_id`: Selected microphone input device.
  - `std::string recording_output_path`: Target directory/file for MP4 recordings.
  - `AdLibMode mode`: `Live` vs `Recorded`.
  - `AdLibStage stage`: `Idle`, `Intro`, `Middle` (Presentation), `Outro`, `Finished`.
  - `bool is_paused`: Pause/play status.
- **UI Elements**:
  - Template configuration section (file selectors for Intro/Outro/Background, Mic selection drop-down).
  - Live status indicator (STAGE, FPS, Recording state & file size, Mic VU meter).
  - Transport controls: `[ Prepare Sequence ]`, `[ Play / Pause ]`, `[ Next Stage ]`, `[ Stop / Finish ]`.

### Phase 2: Stage & Sequence Management Engine (`src/helpers/adlib_engine.hpp`, `adlib_engine.cpp`)
- **State Machine**:
  - **`Stage::Intro`**: Decodes `intro.mp4` using `media_player_item`. On video completion (or manual skip), automatically advances to `Middle`.
  - **`Stage::Middle`**: Renders fixed background texture onto detached window. Renders active `card->render_video_ui()` overlays on top. Presenter talks while toggling card overlays. Advance to `Outro` via transport control / hotkey.
  - **`Stage::Outro`**: Decodes `outro.mp4` using `media_player_item`. On video completion, closes recording (if active) and resets to `Finished`.
- **Detached Window Integration**:
  - Integrate `adlib_engine` into `main_wnd_loop.cpp` inside `process_detached_window()`.
  - Detached window renders active stage content + card overlays + transport progress line.

### Phase 3: Audio Input Capture & Audio Mixing Pipeline (`src/helpers/audio_capture.hpp`, `audio_capture.cpp`)
- **Microphone Capture**:
  - Enumerate audio input devices via `SDL_GetAudioInputDevices()`.
  - Open selected audio input device using SDL3 audio stream (`SDL_OpenAudioDeviceStream`) in PCM 16-bit / 44.1kHz stereo format.
  - Read input samples during `Stage::Middle` and queue into audio stream.
- **Audio Routing**:
  - **`Stage::Intro`**: Route `intro.mp4` audio stream to MP4 encoder (in recorded mode) and system output.
  - **`Stage::Middle`**: Route microphone PCM samples (+ card audio if playing) to MP4 encoder (in recorded mode).
  - **`Stage::Outro`**: Route `outro.mp4` audio stream to MP4 encoder (in recorded mode) and system output.

### Phase 4: MP4 Video & Audio Writing Pipeline (`src/helpers/mp4_writer.hpp`, `mp4_writer.cpp`)
- **Encoder Architecture**:
  - Utilize FFmpeg (`libavformat`, `libavcodec`, `swscale`, `swresample`).
  - Container format: `mp4`.
  - Video Codec: `h264_videotoolbox` (macOS hardware acceleration) with fallback to `libx264`.
  - Audio Codec: `aac` (44.1 kHz, 128 kbps stereo).
- **Pixel Frame Grabbing**:
  - In `Recorded` mode, after detached window rendering pass completes, read back rendered frame buffer or texture pixels (RGBA format).
  - Convert RGBA -> YUV420P using `sws_scale` and pass into H.264 video encoder stream.
  - Interleave video frames (30 FPS) with audio PCM chunks to guarantee precise A/V sync in the resulting `.mp4` output.

### Phase 5: Card Registration, Menu & Keybinding Integration
- Register `AdLibCard` in `factory.hpp`.
- Add menu action under "Production / Live Presentation" in `menu.hpp`.
- Keybindings in detached window:
  - `Space`: Toggle Play/Pause.
  - `Right Arrow` / `N`: Skip to Next Stage (Intro -> Middle -> Outro).
  - `1`, `2`, `3`...: Toggle deck card video overlays live on-air.

---

## Plan Verification & Delivery Criteria

1. **Build & Compilation**: Clean incremental build using `nix develop --command cmake --build build --target rouen -j2`.
2. **Live Test**: Load Ad-Lib card, specify Intro video, Background image, Outro video. Detach window and verify seamless transition through Intro -> Middle (with live card overlays) -> Outro.
3. **Recorded Test**: Select Mic input, start Recorded Ad-Lib, run sequence, verify output `.mp4` file is created, playable, and contains synchronized video + intro/outro sound + mic voice audio.
4. **App Deployment & Code Signing**: Deploy binary to `$HOME/Applications/Rouen.app/Contents/MacOS/rouen` preserving `.env` and applying `codesign`.
