# Feature Request: Adaptive Cards Presentation and Two-Way Action Handling for Environment Variables Card (`envvars`)

**Date**: October 7, 2026  
**From**: Rouen Desktop Client Engineering Team  
**To**: Rouen Architecture & Core Card Maintainers  
**Status**: Open (Feature Specification RFC)  
**Target Components**:
- [`src/cards/system/envvars.hpp`](file:///Users/ignaciorodriguez/src/rouen/src/cards/system/envvars.hpp)
- Adaptive Card Renderer Helper: [`src/helpers/adaptive_cards/renderer.hpp`](file:///Users/ignaciorodriguez/src/rouen/src/helpers/adaptive_cards/renderer.hpp)
- Adaptive Card Parser Helper: [`src/helpers/adaptive_cards/parser.hpp`](file:///Users/ignaciorodriguez/src/rouen/src/helpers/adaptive_cards/parser.hpp)
- Unit & Regression Tests: `tests/test_card_adaptive_interface.cpp`

**File Location**: [`inbox/2026-10-07-feat-envvars-adaptive-card.md`](file:///Users/ignaciorodriguez/src/rouen/inbox/2026-10-07-feat-envvars-adaptive-card.md)  

---

## 1. Executive Summary & Rationale

### 1.1 Overview
The `envvars` card currently renders its interface exclusively through immediate-mode ImGui controls in `render()`. While responsive locally, it lacks implementation of the two virtual card interoperability methods defined in [`src/cards/interface/card.hpp`](file:///Users/ignaciorodriguez/src/rouen/src/cards/interface/card.hpp):
1. `virtual std::string get_adaptive_card_json() const`: Exports declarative state as standard Adaptive Cards 1.5 JSON.
2. `virtual void handle_action(std::string_view action_json)`: Processes inbound user action payloads (`Action.Execute` and `Action.Submit`) to mutate card state.

Furthermore, this card does not yet provide an in-card **Adaptive Card Presentation Override** (`adaptive_view_mode` toggle), preventing users from experiencing the declarative card layout natively inside the Rouen deck.

### 1.2 System Motivations
- **Remote Mesh Operability (`mesh://<client-id>/envvars`)**: Enables remote Rouen nodes (macOS, Windows, Linux) to interact with `envvars` without heavy video streaming or RDP.
- **Web UI & Mobile Parity**: Allows the Rouen Web client and companion apps to render native responsive cards using official Adaptive Card renderers.
- **AI Agent & MCP Automation**: Allows LLM agents (via Model Context Protocol or Telegram host) to inspect current state in structured JSON and execute card actions programmatically.
- **Declarative User Experience**: Gives desktop users an alternative clean presentation style matching modern card aesthetics.

### Story 1: As a systems developer configuring environment parameters
> **As a systems developer configuring environment parameters**,
> I want to inspect and modify process environment variables in an Adaptive Card table,
> so that I can verify API keys, paths, and runtime flags across operating systems..


---

## 2. Technical Architecture & Component Changes

### 2.1 C++ Class Header Adjustments
In [`src/cards/system/envvars.hpp`](file:///Users/ignaciorodriguez/src/rouen/src/cards/system/envvars.hpp), declare the virtual overrides and adaptive card caching state:

```cpp
// In envvars_card: public card
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
bool envvars_card::render(rouen::ui::ui_context& ui) {
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
- `std::vector<std::pair<std::string, std::string>> env_entries`
- `std::string search_filter`
- `size_t total_variables`

---

## 3. Two-Way Action Specification

The card will parse incoming JSON actions dispatched by user interactions in `handle_action`:

| Verb | Display Title | Input Parameters | Expected Side Effects & State Mutation |
| :--- | :--- | :--- | :--- |
| `env_set` | **Set / Update Variable** | `key (string), value (string)` | Sets process environment variable in the current runtime context. |
| `env_remove` | **Remove Variable** | `key (string)` | Unsets target environment variable. |
| `env_export` | **Export .env** | `none` | Exports current variable snapshot to .env file in user resources. |


Action handling parses the JSON verb via Glaze:
```cpp
void envvars_card::handle_action(std::string_view action_json) {
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
        std::cerr << "[envvars] Action error: " << e.what() << std::endl;
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
                  "text": "\u2699\ufe0f",
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
                  "text": "Environment Variables Inspector",
                  "weight": "Bolder",
                  "size": "Large"
                },
                {
                  "type": "TextBlock",
                  "text": "Active process runtime environment",
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
                  "text": "68 Variables",
                  "weight": "Bolder",
                  "size": "Small"
                }
              ]
            }
          ]
        }
      ]
    },
    {
      "type": "FactSet",
      "facts": [
        {
          "title": "ROUEN_HOME:",
          "value": "/Users/ignaciorodriguez/src/rouen"
        },
        {
          "title": "PATH:",
          "value": "/nix/var/nix/profiles/default/bin:/usr/local/bin:/usr/bin:/bin"
        },
        {
          "title": "SHELL:",
          "value": "/bin/zsh"
        },
        {
          "title": "TERM:",
          "value": "xterm-256color"
        }
      ]
    },
    {
      "type": "Container",
      "separator": true,
      "items": [
        {
          "type": "TextBlock",
          "text": "Set or Override Variable",
          "weight": "Bolder"
        },
        {
          "type": "Input.Text",
          "id": "env_key",
          "placeholder": "Variable Name (e.g. GEMINI_API_KEY)"
        },
        {
          "type": "Input.Text",
          "id": "env_val",
          "placeholder": "Value..."
        }
      ]
    }
  ],
  "actions": [
    {
      "type": "Action.Execute",
      "title": "Set Variable",
      "verb": "env_set"
    },
    {
      "type": "Action.Execute",
      "title": "Export .env",
      "verb": "env_export"
    }
  ]
}
```

---

## 5. Verification & Unit Test Plan

### 5.1 Test Cases (`tests/test_card_adaptive_interface.cpp`)
1. **Serialization (`envvars_cardAdaptiveTest.JsonSerialization`)**:
   - Instantiate `envvars_card`.
   - Assert `get_adaptive_card_json()` returns non-empty string.
   - Parse with `helpers::adaptive_cards::parser` and verify root element is `AdaptiveCard` with version `1.5`.
   - Verify all required FactSet fields and inputs exist.
2. **Action Dispatch (`envvars_cardAdaptiveTest.ActionDispatch`)**:
   - Send simulated `Action.Execute` JSON payload for each supported verb.
   - Assert corresponding card internal state is mutated correctly.
3. **Resilience & Error Handling (`envvars_cardAdaptiveTest.MalformedActionHandling`)**:
   - Pass malformed or partial JSON to `handle_action()`.
   - Assert no segfault or unhandled exception occurs.

### 5.2 Build & Execution
Run test suite under nix environment:
```bash
nix develop --command cmake --build build --target unit_tests -j2
nix develop --command ./build/tests/unit_tests --gtest_filter="envvars_cardAdaptiveTest.*"
```

Followed by compiling application and ad-hoc codesigning:
```bash
nix develop --command cmake --build build --target rouen -j2
say "Adaptive card support implemented for Environment Variables Card (`envvars`)"
```
