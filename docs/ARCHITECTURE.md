# Architecture Guide

Rouen uses a modular, card-based architecture that promotes separation of concerns, flexibility, and maintainability.

![Rouen Architecture Diagram](diagrams/rouen_architecture.png)

---

## Core Components

### 1. Card System
The primary building block of Rouen's interface is the **Card**. Each card represents an independent, interactive component with its own logic and layout.

* **Card Base Class** ([card.hpp](file:///Users/ignaciorodriguez/src/rouen/src/cards/interface/card.hpp)):
  - Defines the core interface for rendering, focus, sizing, and event handling.
  - Manages card-specific styling, colors, and refresh/fps throttling for rendering efficiency.
  - Provides utility window helpers like `render_window` to standardize wrapper UI frames.
  - Exposes `virtual void render_video_ui()` to allow cards to paint custom vector graphics, telemetry badges, or layout overlays on offscreen video streams.

* **Card Registration and Creation**:
  - Registered dynamic cards are constructed via the **Card Factory** ([factory.hpp](file:///Users/ignaciorodriguez/src/rouen/src/cards/interface/factory.hpp)) using a URI-based scheme (e.g., `camera:1:1`, `number-series:sales`, `git`, `rss`).

---

### 2. Deck Management & Workspace Layout
The **Deck** ([deck.hpp](file:///Users/ignaciorodriguez/src/rouen/src/cards/interface/deck.hpp)) controls collections of active cards:
- **Horizontal Tiling & Workspace Sizing**: Manages card positioning, vertical row stacking, and horizontal workspace width.
- **Width Multipliers & Section Boundaries**:
  - **Width Factor Multiplier ($wf$)**: Configured via the `display` card ([display_card.hpp](file:///Users/ignaciorodriguez/src/rouen/src/cards/system/display_card.hpp)), extending total row width capacity to $\text{row\_max\_width} = \text{size.x} \times wf$ (e.g. 1.0x, 2.0x, 3.0x, 4.0x).
  - **Section Width ($\text{section\_width} = \text{size.x}$)**: Each section represents one full OS window viewport page width.
  - **Last Fitting Window Expansion**: As cards are laid out sequentially into a row, if adding a card would exceed a section boundary ($\text{sec\_boundary} = (k + 1) \times \text{size.x}$), the layout engine expands the width of the last card fitting in section $k$ so its right edge perfectly aligns to $\text{sec\_boundary}$.
  - **Section Navigation**: When focusing a card, Rouen calculates its section index ($\text{section\_idx} = \lfloor\text{abs\_x} / \text{size.x}\rfloor$) and programmatically performs smooth horizontal scrolling to $\text{target\_scroll\_x} = \text{section\_idx} \times \text{size.x}$.
- **Persistent Workspace State**: Saves active card URIs and configurations to `rouen.ini` in user config directory.
- **Global Shortcuts**: Implements shortcuts (`Cmd+W`/`Ctrl+W` to close focused cards, `Cmd+Shift+F`/`Ctrl+Shift+F` fit window width, `Cmd+Shift+S`/`Ctrl+Shift+S` snapshot).

---

### 3. HTTP REST API Server
The **API Server** ([api_server.hpp](file:///Users/ignaciorodriguez/src/rouen/src/helpers/api_server.hpp)) exposes a lightweight embedded Mongoose HTTP server on port `8081`:
- **Card Lifecycle API**: `GET /api/cards`, `POST /api/cards`, `DELETE /api/cards?uri=...`.
- **Camera Layout API**: `GET /api/camera/layout`, `POST /api/camera/layout` (switches between 7 video feed presets dynamically), `GET /api/camera/status`, `POST /api/camera/snapshot`.
- **Video Feed & Cast API**: `POST /api/cast/start`, `POST /api/cast/play`, `GET /api/cast/status`.
- **Schema Discovery API**: `GET /api/schemas` (returns JSON schemas for AI Chat and external integrations).

---

---

### 4. Integrations & Unified Services Architecture: Helpers vs. Hosts

Rouen organizes reusable integrations, protocols, external API access, and system services into two distinct categories based on **statefulness**, **authentication lifecycle**, and **usage tracking**:

```
                       ┌─────────────────────────────────────────────────────────┐
                       │          Rouen Integrations & Unified Services          │
                       └────────────────────────────┬────────────────────────────┘
                                                    │
                   ┌────────────────────────────────┴────────────────────────────────┐
                   ▼                                                                 ▼
      ┌─────────────────────────┐                                       ┌─────────────────────────┐
      │   Helpers (src/helpers) │                                       │    Hosts (src/hosts)    │
      ├─────────────────────────┤                                       ├─────────────────────────┤
      │  • Completely Stateless │                                       │  • Stateful Integrations│
      │  • Pure Transformations │                                       │  • Auth Keys & OAuth    │
      │  • Utility Functions    │                                       │  • Token Revocation     │
      │  • Parsers & Renderers  │                                       │  • Usage & Quota Track  │
      │  • Zero Session State   │                                       │  • Background Sync Loop │
      └─────────────────────────┘                                       └─────────────────────────┘
```

#### A. Stateless Helpers (`src/helpers/`)
Helpers provide pure, stateless utility functions, text/data transformations, formatting, and protocol parsers. They hold zero persistent connection handles, zero authentication session tokens, and zero usage metrics.

* **HTTP Client Helper** ([fetch.hpp](file:///Users/ignaciorodriguez/src/rouen/src/helpers/fetch.hpp)): Thread-safe, stateless `libcurl` fetch engine supporting configurable TLS security modes (`strict`, `relaxed`, `compatible`, `insecure`).
* **Markdown & Syntax Renderer** ([markdown_renderer.hpp](file:///Users/ignaciorodriguez/src/rouen/src/helpers/markdown_renderer.hpp)): Stateless AST parser and ImGui layout renderer for Markdown documents.
* **Chess API & PGN Utilities** ([chess_com_api.hpp](file:///Users/ignaciorodriguez/src/rouen/src/helpers/chess_com_api.hpp), [chess_game_analyzer.hpp](file:///Users/ignaciorodriguez/src/rouen/src/helpers/chess_game_analyzer.hpp)): Pure JSON parsing and PGN string sanitization helpers.
* **Email & HTML Metadata Extractors** ([email_metadata_analyzer.hpp](file:///Users/ignaciorodriguez/src/rouen/src/helpers/email_metadata_analyzer.hpp), [html_media_extractor.hpp](file:///Users/ignaciorodriguez/src/rouen/src/helpers/html_media_extractor.hpp)): Stateless string/MIME extractors for headers and embedded media URLs.
* **Platform & OS Utilities** ([platform_utils.hpp](file:///Users/ignaciorodriguez/src/rouen/src/helpers/platform_utils.hpp), [string_helper.hpp](file:///Users/ignaciorodriguez/src/rouen/src/helpers/string_helper.hpp), [md5.hpp](file:///Users/ignaciorodriguez/src/rouen/src/helpers/md5.hpp)): Cross-platform paths, process execution helpers, and hashing algorithms.
* **UI Widgets & Rendering Contexts** ([imgui_helper.hpp](file:///Users/ignaciorodriguez/src/rouen/src/helpers/imgui_helper.hpp), [flag_renderer.hpp](file:///Users/ignaciorodriguez/src/rouen/src/helpers/flag_renderer.hpp), [date_picker.hpp](file:///Users/ignaciorodriguez/src/rouen/src/helpers/date_picker.hpp)): Immediate-mode UI layout helpers and drawing primitives.

#### B. Stateful Hosts (`src/hosts/`)
Hosts encapsulate long-lived, stateful services, authenticated external API clients, credential managers, background synchronization loops, socket listeners, and usage/token counters.

* **Crypto Exchange Host** ([bybit_host.hpp](file:///Users/ignaciorodriguez/src/rouen/src/hosts/bybit_host.hpp)): Manages Bybit API authentication (HMAC SHA-256 signing, API key/secret storage), rate-limiting state, asset balance caches, and WebSocket/HTTP connections.
* **Trello Sync Host** ([trello_host.hpp](file:///Users/ignaciorodriguez/src/rouen/src/hosts/trello_host.hpp)): Handles Trello OAuth developer keys, secret tokens, token revocation, board caching, and CRUD state synchronization.
* **RSS Feed Host** ([rss_host.hpp](file:///Users/ignaciorodriguez/src/rouen/src/hosts/rss_host.hpp)): Manages background feed polling intervals, feed XML model persistence, local SQLite cache state, and podcast watermark extraction.
* **Weather Service Host** ([weather_host.hpp](file:///Users/ignaciorodriguez/src/rouen/src/hosts/weather_host.hpp)): Manages weather provider API credentials, location queries, background refresh timers, and cached forecast models.
* **Travel & Transit Host** ([travel_host.hpp](file:///Users/ignaciorodriguez/src/rouen/src/hosts/travel_host.hpp)): Manages flight/transit provider authorization headers, live location polling, and schedule state.
* **Dictation & Speech-to-Text Host** ([dictation_host.hpp](file:///Users/ignaciorodriguez/src/rouen/src/hosts/dictation_host.hpp)): Manages Whisper server subprocess lifecycle, recording audio stream buffers, and local transcription state.
* **Video Feed & Broadcast Host** ([video_feed_host.hpp](file:///Users/ignaciorodriguez/src/rouen/src/hosts/video_feed_host.hpp)): Offscreen 1080p ImGui rendering engine, multi-camera context manager, and TCP streaming broadcast server (`tcp://127.0.0.1:8889`).

---

### 5. Identified Host Reclassification & Extraction Roadmap

To strictly enforce this architecture across the codebase, several stateful integrations currently located under `src/helpers/` are identified for reclassification and extraction into `src/hosts/`:

1. **LLM Provider Host (`llm_host` / `ai_provider_host`)**
   - *Current location*: `src/helpers/llm_config.*`, `src/helpers/cppgpt.hpp`, `src/helpers/gemini_adapter.hpp`
   - *Reason for Reclassification*: Manages API keys (OpenAI, Gemini, Grok), active provider configuration, authentication headers, token usage metrics (prompt/completion tokens), model quotas, and chat conversation history state.
   - *Target*: Reclassify into `src/hosts/llm_host.hpp`.

2. **Model Context Protocol (MCP) Host (`mcp_host`)**
   - *Current location*: `src/helpers/mcp_service.*`
   - *Reason for Reclassification*: Manages MCP server subprocess lifecycles, JSON-RPC connection handles, active tool registration, sub-agent authentication/env injection, and session channels.
   - *Target*: Reclassify into `src/hosts/mcp_host.hpp`.

3. **Git Synchronization Host (`git_sync_host`)**
   - *Current location*: `src/helpers/git_sync_service.hpp`
   - *Reason for Reclassification*: Handles remote repository authentication (SSH keys / HTTPS tokens), git process execution state, remote origin tracking, and background auto-commit/push timers.
   - *Target*: Reclassify into `src/hosts/git_sync_host.hpp`.

4. **Universal Cloud Sync Host (`universal_sync_host`)**
   - *Current location*: `src/helpers/universal_sync_service.hpp`
   - *Reason for Reclassification*: Manages cloud storage authentication tokens, token revocation, background sync scheduling, and remote/local file state comparisons.
   - *Target*: Reclassify into `src/hosts/universal_sync_host.hpp`.

5. **Embedded REST API Server Host (`api_server_host`)**
   - *Current location*: `src/helpers/api_server.*`
   - *Reason for Reclassification*: Manages TCP listener socket binding (port 8081), Mongoose HTTP server context, active client session lifecycle, and dynamic API route handlers.
   - *Target*: Reclassify into `src/hosts/api_server_host.hpp`.

6. **Configuration & Credentials Store Host (`config_host` / `api_keys_host`)**
   - *Current location*: `src/helpers/config_service.*`, `src/helpers/api_keys.hpp`
   - *Reason for Reclassification*: Manages global system environment state, `.env` secret key parsing, persistent `rouen.ini` settings, and subscriber notifications.
   - *Target*: Reclassify into `src/hosts/config_host.hpp`.

7. **Media Engine & Socket Host (`media_engine_host` / `mpv_socket_host`)**
   - *Current location*: `src/helpers/media_player.*`, `src/helpers/mpv_socket.hpp`, `src/helpers/stream_encoder.hpp`
   - *Reason for Reclassification*: Manages FFmpeg demuxer/decoder state, SDL audio output streams, MPV IPC socket handles, and live stream encoding socket servers.
   - *Target*: Reclassify into `src/hosts/media_engine_host.hpp`.

---

## Media Synchronization Model (Audio-Master Clock)

![Media Pipeline Diagram](diagrams/media_pipeline.png)

For local playback, Rouen implements an **Audio-Master Clock** model to ensure video and audio alignment:
* **Continuous Audio Flow:** Audio is decoded and written directly to an `SDL_AudioStream` which plays continuously at a fixed hardware sample rate.
* **Rate-limiting via Backpressure:** The demuxer thread pauses reading packets if the audio queue has more than `250ms` of buffered playback (`44100 bytes`).
* **Video Frame Selection & Dropping:** During each ImGui rendering tick, the main thread calculates current audio clock ($t_{\text{audio}} = t_{\text{last\_audio\_pts}} - \text{duration of queued audio}$):
  - Late frames (> **15ms**) are **dropped** (skipping color space conversion, scaling, memory copying, and GPU upload).
  - Early frames (> **30ms**) are held in queue.
  - On-time frames are uploaded and rendered.
* **Zero Pacing Sleeps:** Ensures audio decoding and playback never experience underflow stuttering.
