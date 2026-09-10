# Rouen Native Cards AdaptiveCards Representation & Action Design

## Architecture & Interface Specification

To allow all native Rouen cards to expose an Adaptive Cards interface and receive action payloads, the `card` base class defines two virtual methods:

```cpp
/// Returns an Adaptive Cards JSON representation of this card, if supported.
virtual std::string get_adaptive_card_json() const { return {}; }

/// Handles an Adaptive Cards action payload (JSON string) for this card.
virtual void handle_action(std::string_view /*action_json*/) {}
```

---

## Key Design Decisions

### 1. Hybrid Companion Image Strategy
For cards with visual content that cannot be represented using standard Adaptive Card elements (e.g. radial progress timers, custom canvas graphics, waveforms, chess boards, or 3D viewports):
- **Interactive Controls**: Built strictly with native Adaptive Card elements (`TextBlock`, `Input.Text`, `Input.ChoiceSet`, `ActionSet` with `Action.Execute`/`Action.Submit`).
- **Visual Canvas Snapshots**: Rendered to an off-screen surface and embedded in the card using **Base64 Data URIs** (`"data:image/png;base64,..."`) or **Rouen HTTP/Image Cache URLs** (`"http://localhost:8080/cards/{id}/snapshot.png"`).

### 2. Live Card Refresh & Cadence Mechanics
For cards with dynamic state (e.g. running Pomodoro countdown timer, live system monitor, or streaming AI chat):

#### A. Declarative In-Card JSON Refresh Hints (`refreshIntervalMs`)
Cards include a top-level `"refreshIntervalMs"` hint in their Adaptive Cards JSON string output by `get_adaptive_card_json()`:
```json
{
  "type": "AdaptiveCard",
  "version": "1.5",
  "refreshIntervalMs": 1000,
  "refresh": {
    "action": {
      "type": "Action.Execute",
      "verb": "tick"
    }
  },
  "body": [ ... ]
}
```
- **Interpretation**: Tells host renderers (Rouen deck, REST API consumers, remote web views, or LLM agents) the recommended polling interval in milliseconds.
- **Value `0` / Omitted**: Indicates a static card that only updates in response to explicit user actions (`Action.Execute` / `Action.Submit`).

#### B. Native C++ Host Loop FPS Control (`card::requested_fps`)
Native Rouen cards inherit the `requested_fps` property defined in `struct card` (`src/cards/interface/card.hpp`):
```cpp
int requested_fps{1}; // Target update frequency in frames per second
```
Cards dynamically scale `requested_fps` based on active execution state:
- **`ai_chat`**: Sets `requested_fps = 2` (and `"refreshIntervalMs": 500`) while LLM response tokens are streaming; drops to `requested_fps = 0` when idle.
- **`alarm`**: Sets `requested_fps = 60` when ringing for smooth visual flashing/animations; scales to `requested_fps = 1` for countdowns.
- **`pomodoro`**: Sets `requested_fps = 1` while countdown timer is running; sets `requested_fps = 0` when paused.
- **`weather`**: Sets `requested_fps = 1` (and `"refreshIntervalMs": 60000` for OpenWeather API cache checks).

#### C. Push vs. Pull Update Models
- **Push Model** (`adaptive-process` & Streaming Plugins): Background executables emit new single-line Adaptive Card JSONs directly over `stdout` or WebSocket/event bus as state updates occur.
- **Pull Model** (Native C++ Cards & REST Clients): External clients poll `get_adaptive_card_json()` or send `handle_action({"verb": "tick"})` at intervals governed by `"refreshIntervalMs"`.

### 3. HTTP REST API Endpoints & OpenAPI Specification
Adaptive Card synthesis and Action payload dispatch are exposed over the embedded REST API on port `8081`:
- **`GET /api/cards/adaptive`**: Query parameters `index` or `uri`. Returns the Adaptive Cards JSON specification object for active cards. If no parameters are provided, returns an array of all active cards with their Adaptive Card representations.
- **`POST /api/cards/action`**: Accepts a JSON body containing `index` or `uri` and an `action` object (`Action.Execute` / `Action.Submit`). Dispatches the action payload directly to `card::handle_action()`.
- **OpenAPI 3.0 Documentation**: Fully documented in `/api/openapi.json` and interactive Swagger UI (`/swagger`).

---

## Implementation Difficulty Ranking

| Rank | Category | Representative Cards | Companion Image Needed? | Difficulty |
|---|---|---|---|---|
| **1** | **Adaptive & Scripted Plugins** | `adaptive_card`, `adaptive_process_card`, `adaptive_card_plugin_adapter`, `js_card` | Native JSON | **Trivial (Completed)** |
| **2** | **Form, Config, & Simple State** | `alarm`, `contact_card`, `theme_card`, `objectives_card`, `invoice_card`, `footprints_card` | No | **Very Easy** |
| **3** | **Data-Grid, List, & Chat Stream Cards** | `bybit_assets`, `weather`, `movies`, `calendar`, `rss`, `rss_feed`, `rss_item`, `ai_chat` | External URLs / None | **Easy** |
| **4** | **Interactive State Machines** | `pomodoro`, `calculator`, `converter`, `markdown_notes`, `git_overlay`, `adlib_card`, `chess_replay` | Yes (Off-screen snapshot) | **Moderate** |
| **5** | **Real-Time Video / Canvas / PTY** | `camera_card`, `media_card`, `pdf_viewer`, `image_viewer`, `solar_system`, `terminal` | Yes (Frame-grab buffer) | **Hardest** |

---

### Implementation Details by Category

#### Category 1: Adaptive & Scripted Plugins (Trivial / Done)
- Directly store or generate Adaptive Card JSON templates.
- Forward `handle_action()` to process `stdin`, QuickJS runtime, or plugin `on_submit()`.

#### Category 2: Form, Config, & Simple State (Very Easy)
- `get_adaptive_card_json()`: Maps struct fields to standard `TextBlock` and `Input` elements.
- `handle_action()`: Parses JSON key-value pairs and updates internal struct variables.

#### Category 3: Data-Grid & List Cards (Easy)
- `get_adaptive_card_json()`: Serializes internal vectors into `FactSet` or `Container` arrays with `ActionSet` rows.
- `handle_action()`: Handles item selection, filtering, or deletion verbs.

#### Category 4: Interactive State Machines with Companion Visuals (Moderate)
- **Example (Pomodoro)**:
  - `get_adaptive_card_json()`: Generates a hybrid card with a Base64 image snapshot of the timer ring, TextBlocks for time left, and `Action.Execute` buttons (`Start`, `Pause`, `Reset`). Sets `"refreshIntervalMs": 1000`.
  - `handle_action()`: Updates state machine (`running` vs `paused`), adjusting `requested_fps` accordingly.

#### Category 5: Real-Time Video / Canvas / PTY (Hardest)
- **Example (Camera / Terminal / Solar System)**:
  - `get_adaptive_card_json()`: Renders the active frame buffer or terminal canvas into a compressed PNG Base64 data URI, alongside media/PTY action controls.
  - `handle_action()`: Dispatches stream control actions (play, pause, seek, send terminal input).
