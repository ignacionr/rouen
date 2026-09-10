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
For cards with dynamic state (e.g. running Pomodoro countdown timer or live system monitor):
- **In-Card Metadata**: The card JSON includes `"refreshIntervalMs": 1000` and standard `"refresh"` action objects to inform external renderers of update frequency.
- **Push Model** (`adaptive-process` & Streaming Plugins): Background processes emit new single-line Adaptive Card JSONs directly over `stdout` or WebSocket/event bus as state updates.
- **Pull Model** (Native C++ Cards): Cards manage `card::requested_fps` (`1` while active/running, `0` when paused/idle) to drive host render/update loops efficiently.

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
