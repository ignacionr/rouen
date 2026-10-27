# Feature Request: Adaptive Cards Presentation and Two-Way Action Handling for GitHub CI Card (`github-ci`)

**Date**: October 7, 2026  
**From**: Rouen Desktop Client Engineering Team  
**To**: Rouen Architecture & Core Card Maintainers  
**Status**: Open (Feature Specification RFC)  
**Target Components**:
- [`src/cards/development/github/ci_card.hpp`](file:///Users/ignaciorodriguez/src/rouen/src/cards/development/github/ci_card.hpp)
- [`src/cards/development/github/ci_card.cpp`](file:///Users/ignaciorodriguez/src/rouen/src/cards/development/github/ci_card.cpp)
- Adaptive Card Renderer Helper: [`src/helpers/adaptive_cards/renderer.hpp`](file:///Users/ignaciorodriguez/src/rouen/src/helpers/adaptive_cards/renderer.hpp)
- Adaptive Card Parser Helper: [`src/helpers/adaptive_cards/parser.hpp`](file:///Users/ignaciorodriguez/src/rouen/src/helpers/adaptive_cards/parser.hpp)
- Unit & Regression Tests: `tests/test_card_adaptive_interface.cpp`

**File Location**: [`inbox/2026-10-07-feat-github-ci-adaptive-card.md`](file:///Users/ignaciorodriguez/src/rouen/inbox/2026-10-07-feat-github-ci-adaptive-card.md)  

---

## 1. Executive Summary & Rationale

### 1.1 Overview
The `github-ci` card currently renders its interface exclusively through immediate-mode ImGui controls in `render()`. While responsive locally, it lacks implementation of the two virtual card interoperability methods defined in [`src/cards/interface/card.hpp`](file:///Users/ignaciorodriguez/src/rouen/src/cards/interface/card.hpp):
1. `virtual std::string get_adaptive_card_json() const`: Exports declarative state as standard Adaptive Cards 1.5 JSON.
2. `virtual void handle_action(std::string_view action_json)`: Processes inbound user action payloads (`Action.Execute` and `Action.Submit`) to mutate card state.

Furthermore, this card does not yet provide an in-card **Adaptive Card Presentation Override** (`adaptive_view_mode` toggle), preventing users from experiencing the declarative card layout natively inside the Rouen deck.

### 1.2 System Motivations
- **Remote Mesh Operability (`mesh://<client-id>/github-ci`)**: Enables remote Rouen nodes (macOS, Windows, Linux) to interact with `github-ci` without heavy video streaming or RDP.
- **Web UI & Mobile Parity**: Allows the Rouen Web client and companion apps to render native responsive cards using official Adaptive Card renderers.
- **AI Agent & MCP Automation**: Allows LLM agents (via Model Context Protocol or Telegram host) to inspect current state in structured JSON and execute card actions programmatically.
- **Declarative User Experience**: Gives desktop users an alternative clean presentation style matching modern card aesthetics.

### Story 1: As a DevOps and build engineer
> **As a DevOps and build engineer**,
> I want to track GitHub Actions pipeline results and trigger workflow dispatches from an Adaptive Card,
> so that I can monitor cross-platform build matrix status on Mac and Windows simultaneously..


---

## 2. Technical Architecture & Component Changes

### 2.1 C++ Class Header Adjustments
In [`src/cards/development/github/ci_card.hpp`](file:///Users/ignaciorodriguez/src/rouen/src/cards/development/github/ci_card.hpp), declare the virtual overrides and adaptive card caching state:

```cpp
// In github_ci_card: public card
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
bool github_ci_card::render(rouen::ui::ui_context& ui) {
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
- `std::string repo_slug`
- `std::vector<workflow_run> runs`
- `std::string selected_run_id`
- `bool is_polling`

---

## 3. Two-Way Action Specification

The card will parse incoming JSON actions dispatched by user interactions in `handle_action`:

| Verb | Display Title | Input Parameters | Expected Side Effects & State Mutation |
| :--- | :--- | :--- | :--- |
| `trigger_workflow` | **Dispatch Workflow** | `workflow_choice (choice), target_branch (string)` | Triggers a GitHub Actions workflow_dispatch event. |
| `rerun_run` | **Rerun Failed Jobs** | `run_id (string)` | Re-runs failed jobs for the specified workflow run ID. |
| `cancel_run` | **Cancel Workflow Run** | `run_id (string)` | Cancels an ongoing GitHub Actions workflow run. |
| `refresh_ci` | **Refresh Runs** | `none` | Fetches recent CI runs and status conclusions. |


Action handling parses the JSON verb via Glaze:
```cpp
void github_ci_card::handle_action(std::string_view action_json) {
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
        std::cerr << "[github-ci] Action error: " << e.what() << std::endl;
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
  "refreshIntervalMs": 10000,
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
                  "text": "\u26a1",
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
                  "text": "GitHub Actions CI Telemetry",
                  "weight": "Bolder",
                  "size": "Large"
                },
                {
                  "type": "TextBlock",
                  "text": "ignacionr/rouen: workflows and status checks",
                  "isSubtle": true,
                  "size": "Small",
                  "spacing": "None"
                }
              ]
            },
            {
              "type": "Column",
              "width": "auto",
              "items": [
                {
                  "type": "TextBlock",
                  "text": "PASSING",
                  "weight": "Bolder",
                  "color": "Good",
                  "size": "Medium"
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
          "title": "Latest Build:",
          "value": "Run #142: macOS arm64 + Windows x64"
        },
        {
          "title": "Commit SHA:",
          "value": "a7f3e1b (main)"
        },
        {
          "title": "Duration:",
          "value": "3m 42s"
        },
        {
          "title": "Status:",
          "value": "Completed successfully (2/2 jobs green)"
        }
      ]
    },
    {
      "type": "Container",
      "separator": true,
      "items": [
        {
          "type": "TextBlock",
          "text": "Manual Workflow Dispatch",
          "weight": "Bolder"
        },
        {
          "type": "Input.ChoiceSet",
          "id": "workflow_choice",
          "label": "Select Workflow",
          "value": "ci.yml",
          "choices": [
            {
              "title": "Continuous Integration (ci.yml)",
              "value": "ci.yml"
            },
            {
              "title": "Release Build & Pack (release.yml)",
              "value": "release.yml"
            },
            {
              "title": "Windows Headless Tests (windows.yml)",
              "value": "windows.yml"
            }
          ]
        },
        {
          "type": "Input.Text",
          "id": "target_branch",
          "label": "Target Branch",
          "value": "main"
        }
      ]
    }
  ],
  "actions": [
    {
      "type": "Action.Execute",
      "title": "Trigger Workflow",
      "verb": "trigger_workflow"
    },
    {
      "type": "Action.Execute",
      "title": "Rerun Failed Jobs",
      "verb": "rerun_run"
    },
    {
      "type": "Action.Execute",
      "title": "Refresh CI Status",
      "verb": "refresh_ci"
    }
  ]
}
```

---

## 5. Verification & Unit Test Plan

### 5.1 Test Cases (`tests/test_card_adaptive_interface.cpp`)
1. **Serialization (`github_ci_cardAdaptiveTest.JsonSerialization`)**:
   - Instantiate `github_ci_card`.
   - Assert `get_adaptive_card_json()` returns non-empty string.
   - Parse with `helpers::adaptive_cards::parser` and verify root element is `AdaptiveCard` with version `1.5`.
   - Verify all required FactSet fields and inputs exist.
2. **Action Dispatch (`github_ci_cardAdaptiveTest.ActionDispatch`)**:
   - Send simulated `Action.Execute` JSON payload for each supported verb.
   - Assert corresponding card internal state is mutated correctly.
3. **Resilience & Error Handling (`github_ci_cardAdaptiveTest.MalformedActionHandling`)**:
   - Pass malformed or partial JSON to `handle_action()`.
   - Assert no segfault or unhandled exception occurs.

### 5.2 Build & Execution
Run test suite under nix environment:
```bash
nix develop --command cmake --build build --target unit_tests -j2
nix develop --command ./build/tests/unit_tests --gtest_filter="github_ci_cardAdaptiveTest.*"
```

Followed by compiling application and ad-hoc codesigning:
```bash
nix develop --command cmake --build build --target rouen -j2
say "Adaptive card support implemented for GitHub CI Card (`github-ci`)"
```
