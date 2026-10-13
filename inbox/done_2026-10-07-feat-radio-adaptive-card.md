# Feature Request: Adaptive Cards Presentation and Two-Way Action Handling for Internet Radio Tuner Card (`radio`)

**Date**: October 7, 2026  
**From**: Rouen Desktop Client Engineering Team  
**To**: Rouen Architecture & Core Card Maintainers  
**Status**: Open (Feature Specification RFC)  
**Target Components**:
- [`src/cards/media/radio.hpp`](file:///Users/ignaciorodriguez/src/rouen/src/cards/media/radio.hpp)
- Adaptive Card Renderer Helper: [`src/helpers/adaptive_cards/renderer.hpp`](file:///Users/ignaciorodriguez/src/rouen/src/helpers/adaptive_cards/renderer.hpp)
- Adaptive Card Parser Helper: [`src/helpers/adaptive_cards/parser.hpp`](file:///Users/ignaciorodriguez/src/rouen/src/helpers/adaptive_cards/parser.hpp)
- Unit & Regression Tests: `tests/test_card_adaptive_interface.cpp`

**File Location**: [`inbox/2026-10-07-feat-radio-adaptive-card.md`](file:///Users/ignaciorodriguez/src/rouen/inbox/2026-10-07-feat-radio-adaptive-card.md)  

---

## 1. Executive Summary & Rationale

### 1.1 Overview
The `radio` card currently renders its interface exclusively through immediate-mode ImGui controls in `render()`. While responsive locally, it lacks implementation of the two virtual card interoperability methods defined in [`src/cards/interface/card.hpp`](file:///Users/ignaciorodriguez/src/rouen/src/cards/interface/card.hpp):
1. `virtual std::string get_adaptive_card_json() const`: Exports declarative state as standard Adaptive Cards 1.5 JSON.
2. `virtual void handle_action(std::string_view action_json)`: Processes inbound user action payloads (`Action.Execute` and `Action.Submit`) to mutate card state.

Furthermore, this card does not yet provide an in-card **Adaptive Card Presentation Override** (`adaptive_view_mode` toggle), preventing users from experiencing the declarative card layout natively inside the Rouen deck.

### 1.2 System Motivations
- **Remote Mesh Operability (`mesh://<client-id>/radio`)**: Enables remote Rouen nodes (macOS, Windows, Linux) to interact with `radio` without heavy video streaming or RDP.
- **Web UI & Mobile Parity**: Allows the Rouen Web client and companion apps to render native responsive cards using official Adaptive Card renderers.
- **AI Agent & MCP Automation**: Allows LLM agents (via Model Context Protocol or Telegram host) to inspect current state in structured JSON and execute card actions programmatically.
- **Declarative User Experience**: Gives desktop users an alternative clean presentation style matching modern card aesthetics.

### Story 1: As an internet radio enthusiast
> **As an internet radio enthusiast**,
> I want to browse global radio stations and start live audio streams via an Adaptive Card,
> so that I can listen to worldwide news and music effortlessly..


---

## 2. Technical Architecture & Component Changes

### 2.1 C++ Class Header Adjustments
In [`src/cards/media/radio.hpp`](file:///Users/ignaciorodriguez/src/rouen/src/cards/media/radio.hpp), declare the virtual overrides and adaptive card caching state:

```cpp
// In radio: public card
public:
    /// Returns the Adaptive Cards 1.5 JSON representation of the card state
    [[nodiscard]] std::string get_adaptive_card_json() const override;

    /// Dispatches inbound Adaptive Card Action.Execute or Action.Submit payloads
    void handle_action(std::string_view action_json) override;

private:
    // Adaptive Card presentation override state
    bool adaptive_view_mode{false};
    mutable helpers::adaptive_cards::parser adaptive_parser_{};
    mutable helpers::adaptive_cards::renderer adaptive_renderer_{};
    mutable helpers::adaptive_cards::renderer::input_state adaptive_input_state_{};
    mutable helpers::adaptive_cards::card_document adaptive_bound_{};
    std::string adaptive_error_{};
    std::chrono::steady_clock::time_point last_adaptive_parse_time_{};
```

### 2.2 Dual-Mode Presentation in `render()`
Add the presentation mode toggle and renderer invocation at the top of the card's render loop:

```cpp
bool radio::render(rouen::ui::ui_context& ui) {
    return render_window([this, &ui]() {
        // Presentation mode selector
        ui.checkbox("Adaptive Card View", &adaptive_view_mode);
        ui.separator();

        if (adaptive_view_mode) {
            auto now = std::chrono::steady_clock::now();
            auto parse_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_adaptive_parse_time_).count();
            if (parse_elapsed >= 500 || adaptive_bound_.body.empty()) {
                try {
                    adaptive_bound_ = adaptive_parser_.parse(get_adaptive_card_json());
                    adaptive_error_.clear();
                } catch (const std::exception& e) {
                    adaptive_error_ = e.what();
                }
                last_adaptive_parse_time_ = now;
            }

            if (!adaptive_error_.empty()) {
                ui.text_colored(ImVec4{1.0f, 0.4f, 0.4f, 1.0f}, adaptive_error_);
            } else {
                adaptive_renderer_.render(
                    adaptive_bound_, adaptive_input_state_,
                    helpers::adaptive_cards::renderer::action_callbacks{
                        .open_url = [](const std::string& url) {
                            static_cast<void>(rouen::platform::open_url(url));
                        },
                        .on_submit = [this](const std::string& payload) {
                            handle_action(payload);
                        }
                    },
                    helpers::adaptive_cards::render_config{
                        .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                        .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                        .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono)
                    }
                );
            }
            return;
        }

        // Existing immediate-mode ImGui rendering follows...
    });
}
```

### 2.3 State Synchronization
The following primary internal state variables will be serialized into the card document:
- `std::string station_name`
- `std::string station_country`
- `std::string stream_url`
- `int bitrate_kbps`
- `bool is_favorite`

---

## 3. Two-Way Action Specification

The card will parse incoming JSON actions dispatched by user interactions in `handle_action`:

| Verb | Display Title | Input Parameters | Expected Side Effects & State Mutation |
| :--- | :--- | :--- | :--- |
| `radio_play` | **Tune Station** | `station_id (choice)` | Connects to live internet radio stream. |
| `radio_stop` | **Stop Radio** | `none` | Stops radio audio playback. |
| `radio_toggle_fav` | **Favorite Station** | `none` | Adds or removes active station from favorites. |


Action handling parses the JSON verb via Glaze:
```cpp
void radio::handle_action(std::string_view action_json) {
    try {
        glz::json_t payload;
        if (glz::read_json(payload, std::string(action_json))) return;

        std::string verb;
        if (payload.contains("verb") && payload["verb"].holds<std::string>()) {
            verb = payload["verb"].get<std::string>();
        } else if (payload.contains("action") && payload["action"].holds<std::string>()) {
            verb = payload["action"].get<std::string>();
        }

        glz::json_t data = payload.contains("data") ? payload["data"] : payload;

        // Dispatch verbs...
    } catch (const std::exception& e) {
        std::cerr << "[radio] Action error: " << e.what() << std::endl;
    }
}
```

---

## 4. Elegant Adaptive Card Template

The following Adaptive Card 1.5 document is generated dynamically by `get_adaptive_card_json()`:

```json
{
  "type": "AdaptiveCard",
  "version": "1.5",
  "$schema": "http://adaptivecards.io/schemas/adaptive-card.json",
  "body": [
    {
      "type": "Container",
      "style": "emphasis",
      "items": [
        {
          "type": "ColumnSet",
          "columns": [
            {
              "type": "Column",
              "width": "auto",
              "items": [
                {
                  "type": "TextBlock",
                  "text": "\ud83d\udcfb",
                  "size": "Large"
                }
              ]
            },
            {
              "type": "Column",
              "width": "stretch",
              "items": [
                {
                  "type": "TextBlock",
                  "text": "Global Internet Radio",
                  "weight": "Bolder",
                  "size": "Large"
                },
                {
                  "type": "TextBlock",
                  "text": "BBC Radio 4 (London, United Kingdom)",
                  "isSubtle": true,
                  "size": "Small"
                }
              ]
            },
            {
              "type": "Column",
              "width": "auto",
              "items": [
                {
                  "type": "TextBlock",
                  "text": "128 kbps MP3",
                  "weight": "Bolder",
                  "color": "Good",
                  "size": "Small"
                }
              ]
            }
          ]
        }
      ]
    },
    {
      "type": "Container",
      "items": [
        {
          "type": "Input.ChoiceSet",
          "id": "station_picker",
          "label": "Select Station",
          "value": "bbc4",
          "choices": [
            {
              "title": "BBC Radio 4 (UK - News & Spoken Word)",
              "value": "bbc4"
            },
            {
              "title": "KEXP 90.3 FM (USA - Seattle Alternative)",
              "value": "kexp"
            },
            {
              "title": "Radio Mitre (Argentina - News)",
              "value": "mitre"
            },
            {
              "title": "FIP (France - Eclectic Music)",
              "value": "fip"
            }
          ]
        }
      ]
    },
    {
      "type": "FactSet",
      "facts": [
        {
          "title": "Currently Broadcasting:",
          "value": "The World at One (Live News & Analysis)"
        },
        {
          "title": "Codec / Format:",
          "value": "MPEG-1 Audio Layer 3 (Stereo 44.1 kHz)"
        },
        {
          "title": "Station Status:",
          "value": "Stream Healthy (Buffer: 2.4s)"
        }
      ]
    }
  ],
  "actions": [
    {
      "type": "Action.Execute",
      "title": "Play Station",
      "verb": "radio_play"
    },
    {
      "type": "Action.Execute",
      "title": "Stop",
      "verb": "radio_stop"
    },
    {
      "type": "Action.Execute",
      "title": "Toggle Favorite",
      "verb": "radio_toggle_fav"
    }
  ]
}
```

---

## 5. Verification & Unit Test Plan

### 5.1 Test Cases (`tests/test_card_adaptive_interface.cpp`)
1. **Serialization (`radioAdaptiveTest.JsonSerialization`)**:
   - Instantiate `radio`.
   - Assert `get_adaptive_card_json()` returns non-empty string.
   - Parse with `helpers::adaptive_cards::parser` and verify root element is `AdaptiveCard` with version `1.5`.
   - Verify all required FactSet fields and inputs exist.
2. **Action Dispatch (`radioAdaptiveTest.ActionDispatch`)**:
   - Send simulated `Action.Execute` JSON payload for each supported verb.
   - Assert corresponding card internal state is mutated correctly.
3. **Resilience & Error Handling (`radioAdaptiveTest.MalformedActionHandling`)**:
   - Pass malformed or partial JSON to `handle_action()`.
   - Assert no segfault or unhandled exception occurs.

### 5.2 Build & Execution
Run test suite under nix environment:
```bash
nix develop --command cmake --build build --target unit_tests -j2
nix develop --command ./build/tests/unit_tests --gtest_filter="radioAdaptiveTest.*"
```

Followed by compiling application and ad-hoc codesigning:
```bash
nix develop --command cmake --build build --target rouen -j2
say "Adaptive card support implemented for Internet Radio Tuner Card (`radio`)"
```
