# User & Usage Guide

This guide details how to configure, customize, and operate Rouen.

---

## Getting Started

1. **Launch Rouen**: Open the application from your launcher or via shell:
   ```bash
   rouen
   ```
2. **First-Time Setup**: Copy `.env.template` to `.env` in the application data directory (or configure keys in your global `~/.secrets` file).
3. **Workspace Organization**:
   - Cards can be dragged by their title headers and resized from their borders.
   - Use the **Menu** launcher card to browse and create new cards.

---

## Configuration & Environment Variables

Rouen loads variables from system environment, `~/.secrets`, or a local `.env` file. Below is the full configuration list:

### API Keys & Credentials
* `OPENWEATHER_KEY`: Required for fetching local conditions in the Weather Card.
* `OPENAI_API_KEY`: API key for OpenAI GPT models.
* `GROK_API_KEY`: API key for Grok models (x.ai).
* `GROQ_API_KEY`: API key for Groq fast inference models.
* `BYBIT_API_KEY` & `BYBIT_SECRET_KEY`: Cryptocurrency asset and spot portfolio tracking.
* `GOOGLE_CALENDAR_CLIENT_ID` & `GOOGLE_CALENDAR_CLIENT_SECRET`: Credentials to pull tasks and events.

### JIRA Integration
* `JIRA_URL`: Base URL (e.g., `https://company.atlassian.net`).
* `JIRA_EMAIL`: Account email.
* `JIRA_API_TOKEN`: API token generated under Atlassian account security settings.

### HTTP/SSL Customizations
* `ROUEN_SSL_MODE`: Controls certificate verification. Options:
  - `strict` (Default): Standard verification.
  - `relaxed`: Safe for corporate filters/interceptors.
  - `compatible`: Workaround for outdated servers.
  - `insecure`: Disables certificate checks completely (not recommended).
* `ROUEN_SSL_VERIFY_PEER` & `ROUEN_SSL_VERIFY_HOST`: `true`/`false` toggles for cert checks.

---

## Card Management & URIs

Rouen allows creating cards dynamically using URI-like strings in the Command Palette or menu search:

* **Camera Video Feed**: 
  - `camera:` opens camera device index 0 with Full Screen layout.
  - `camera:<device_index>:<layout_index>` opens device index with preset layout (e.g. `camera:1:1` for Bottom-Right PiP).
  - Presets: `0` (Full Screen), `1` (Bottom-Right PiP), `2` (Bottom-Left PiP), `3` (Top-Right PiP), `4` (Top-Left PiP), `5` (Centered Circle Avatar), `6` (Right Side Bar).
* **Number Series Data Visualization**:
  - `number-series:sales` opens monthly sales revenue chart.
  - `number-series:temps` opens weekly temperature forecast chart.
  - `number-series:cpu` opens system CPU load telemetry chart.
  - `{...}` JSON string opens a custom dataset.
  - Supports live continuous broadcast animation on video feed streams.
* **Directory Explorer**: `dir:/absolute/path/to/folder` opens a folder browser card.
* **Markdown Notes**: 
  - `notes:` opens the note management index.
  - `notes:my-note` opens or creates a note named "my-note".
* **Trello Boards**:
  - `trello:` opens Trello board search and card listings.
  - `trello-board:<board-id>` directly opens a dedicated column board viewer.
* **Adaptive Cards**: `adaptive-card` opens built-in renderer tests.
* **Display Settings & Section Multipliers**: `display` opens the Display Settings card.
  - Controls global deck width factor multipliers (`1x`, `2x`, `3x`, `4x`, or custom slider).
  - Multiplies total row capacity by `size.x` (OS window viewport width).
  - Section Alignment: Expands the "last fitting window" in each viewport section to fit perfectly to section boundaries, ensuring smooth section scrolling without window boundary clipping.

---

## Embedded REST API Endpoints (Port 8081)

| Endpoint | Method | Payload | Action |
| :--- | :--- | :--- | :--- |
| `/api/cards` | `GET` | N/A | List all active cards and URIs |
| `/api/cards` | `POST` | `{"uri":"camera:1:1"}` | Open a new card |
| `/api/cards` | `DELETE` | N/A | Close an active card |
| `/api/camera/layout` | `GET` | N/A | Query active camera layout preset |
| `/api/camera/layout` | `POST` | `{"preset":1}` | Change camera layout preset |
| `/api/camera/status` | `GET` | N/A | Query camera resolution and status |
| `/api/cast/start` | `POST` | `{"url":"camera:1:1"}` | Start TCP video streaming server |
| `/api/cast/status` | `GET` | N/A | Query video stream status |

---

## Keyboard Shortcuts

| Shortcut | Action |
| :--- | :--- |
| **`Cmd+W`** (macOS) / **`Ctrl+W`** (Windows/Linux) | Close the focused card |
| **`Cmd+Shift+S`** (macOS) / **`Ctrl+Shift+S`** (Windows/Linux) | Capture a high-res PNG snapshot of the focused card |
| **`Cmd+Shift+F`** (macOS) / **`Ctrl+Shift+F`** (Windows/Linux) | Fit the parent window window size to the total card workspace width |
| **`Tab`** | Cycle focus to the next card |
| **`F11`** | Toggle Fullscreen mode |
| **`Escape`** | Close overlays, popups, or cancel current operation |

---

## AI Chat MCP Tool

Rouen AI Chat exposes an MCP function named `run_local_command` that can execute local shell commands (including `curl`) and returns combined stdout/stderr output.

Expected params JSON:
```json
{
  "command": "curl -sS http://127.0.0.1:8099/v1/models",
  "working_directory": "/optional/path"
}
```

---

## Troubleshooting

### SSL/TLS Certificate Handshake Errors
* If you see CURL handshake errors on corporate proxies or custom servers, set `ROUEN_SSL_MODE=relaxed` in your `.env` config file.

### Audio or Video Not Playing
* Rouen relies on native FFmpeg libraries and SDL3. Verify audio device output is unmuted and camera access permission is allowed in system settings.

### API Key Updates Not Reflecting
* Values inside `.env` or system profiles are cached. Use the Settings card's **Refresh Cache** button, or restart the application to reload variables.
