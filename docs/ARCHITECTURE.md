# Architecture Guide

Rouen uses a modular, card-based architecture that promotes separation of concerns, flexibility, and maintainability.

![Rouen Architecture Diagram](../rouen_architecture.png)

---

## Core Components

### 1. Card System
The primary building block of Rouen's interface is the **Card**. Each card represents an independent, interactive component with its own logic and layout.

* **Card Base Class** ([card.hpp](file:///Users/ignaciorodriguez/src/rouen/src/cards/interface/card.hpp)):
  - Defines the core interface for rendering, focus, sizing, and event handling.
  - Manages card-specific styling, colors, and refresh/fps throttling for rendering efficiency.
  - Provides utility window helpers like `render_window` to standardize wrapper UI frames.

* **Card Registration and Creation**:
  - Registered dynamic cards are constructed via the **Card Factory** ([factory.hpp](file:///Users/ignaciorodriguez/src/rouen/src/cards/interface/factory.hpp)) using a URI-based scheme (e.g., `git`, `rss`, `notes`).

---

### 2. Deck Management
The **Deck** ([deck.hpp](file:///Users/ignaciorodriguez/src/rouen/src/interface/deck.hpp)) controls collections of active cards:
- Manages horizontal tiling, workspace positions, and order of cards.
- Handles persistent workspace states by saving card layout, sizing, and configuration parameters.
- Implements global shortcuts (e.g. `Cmd+W`/`Ctrl+W` to close focused cards, screenshot snapshots, etc.).

---

### 3. Helper Libraries
To support application logic, Rouen provides several static/service helper modules in `src/helpers/`:

* **Configuration Service** ([config_service.hpp](file:///Users/ignaciorodriguez/src/rouen/src/helpers/config_service.hpp)):
  - Manages environment variables and local `.env` keys.
  - Supports category groupings, fallback defaults, and import/export of configuration maps.
* **HTTP Client** ([fetch.hpp](file:///Users/ignaciorodriguez/src/rouen/src/helpers/fetch.hpp)):
  - A thread-safe wrapper around `libcurl` providing SSL/TLS modes (strict, relaxed, compatible, insecure) for robust corporate proxy support.
* **LLM Integration** ([llm_config.hpp](file:///Users/ignaciorodriguez/src/rouen/src/helpers/llm_config.hpp)):
  - An adapter layer facilitating switches between LLM API providers (Grok, OpenAI, Groq, custom endpoints).
* **Media Player** ([media_player.hpp](file:///Users/ignaciorodriguez/src/rouen/src/helpers/media_player.hpp)):
  - Performs native, in-process decoding of multimedia files (audio/video) using FFmpeg (`libavcodec`, `libavformat`, `libswscale`, `libswresample`).
  - Supports local playback with hardware audio output via SDL3 Audio Streams.
  - Dynamically routes audio and video to the local interface or to the cast stream.
* **Database Access** ([sqlite.hpp](file:///Users/ignaciorodriguez/src/rouen/src/helpers/sqlite.hpp)):
  - Thread-safe key-value store wrapper around SQLite3 for config/state caching.

---

### 4. Host Infrastructure
Hosts in `src/hosts/` manage background data synchronization and caching for card states:
- **Video Cast Host (`VideoFeedHost`)**: A TCP unicast streaming server that encodes and casts the current desktop / media view to remote network players (e.g. `mpv`, VLC) using TCP sockets.
- **RSS Host**: Fetches XML feeds, parses enclosures, extracts podcast watermarks, and caches local feed models.
- **Weather Host**: Fetches meteorological reports and caches forecast results.
- **Trello Host**: Manages board queries, card listings, and CRUD synchronization.

---

## Design Patterns

* **Factory Pattern**: Dynamically instantiates card classes from URI-like parameters.
* **Template Metaprogramming & Concepts**: Enables compile-time optimization, such as type-safe texture pointer casts in `texture_utils.hpp`.
* **Observer/Registrar Pattern**: Facilitates loose coupling between cards, allowing cards to register interests and receive lifecycle events.
* **RAII (Resource Acquisition Is Initialization)**: Standardized resource wrappers around files, sockets, curl sessions, and sqlite transaction handles.

---

## Media Synchronization Model (Audio-Master Clock)

For local playback, Rouen implements an **Audio-Master Clock** model to ensure perfect video and audio alignment:
* **Continuous Audio Flow:** Audio is decoded and written directly to an `SDL_AudioStream` which plays continuously at a fixed hardware sample rate.
* **Rate-limiting via Backpressure:** The demuxer thread pauses reading packets if the audio queue has more than `250ms` of buffered playback (`44100 bytes`), preventing infinite buffer growth and keeping processing aligned to real-time playback.
* **Video Frame Selection & Dropping:** Video frames are decoded as fast as possible and stored in a queue. During each ImGui rendering tick, the main thread calculates the current audio clock ($t_{\text{audio}} = t_{\text{last\_audio\_pts}} - \text{duration of queued audio}$) and queries the queue:
  - If a frame is late by more than **15ms**, it is **dropped** (skipping color space conversion, scaling, memory copying, and GPU upload).
  - If a frame is in the future by more than **30ms**, it is skipped/held in the queue.
  - If a frame is on time, it is uploaded and rendered on screen.
* **Zero Pacing Sleeps:** No sleeps or delays block the video presentation path inside the decoding thread, ensuring that audio decoding and playback never experience underflow stuttering.
