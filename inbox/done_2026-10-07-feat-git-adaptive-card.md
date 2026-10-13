# Feature Request: Adaptive Cards Presentation and Two-Way Action Handling for Git Card (`git`)

**Date**: October 7, 2026  
**From**: Rouen Desktop Client Engineering Team  
**To**: Rouen Architecture & Core Card Maintainers  
**Status**: Open (Feature Specification RFC)  
**Target Components**:
- [`src/cards/development/git.hpp`](file:///Users/ignaciorodriguez/src/rouen/src/cards/development/git.hpp)
- [`src/cards/development/git.cpp`](file:///Users/ignaciorodriguez/src/rouen/src/cards/development/git.cpp)
- Adaptive Card Renderer Helper: [`src/helpers/adaptive_cards/renderer.hpp`](file:///Users/ignaciorodriguez/src/rouen/src/helpers/adaptive_cards/renderer.hpp)
- Adaptive Card Parser Helper: [`src/helpers/adaptive_cards/parser.hpp`](file:///Users/ignaciorodriguez/src/rouen/src/helpers/adaptive_cards/parser.hpp)
- Unit & Regression Tests: `tests/test_card_adaptive_interface.cpp`

**File Location**: [`inbox/2026-10-07-feat-git-adaptive-card.md`](file:///Users/ignaciorodriguez/src/rouen/inbox/2026-10-07-feat-git-adaptive-card.md)  

---

## 1. Executive Summary & Rationale

### 1.1 Overview
The `git` card currently renders its interface exclusively through immediate-mode ImGui controls in `render()`. While responsive locally, it lacks implementation of the two virtual card interoperability methods defined in [`src/cards/interface/card.hpp`](file:///Users/ignaciorodriguez/src/rouen/src/cards/interface/card.hpp):
1. `virtual std::string get_adaptive_card_json() const`: Exports declarative state as standard Adaptive Cards 1.5 JSON.
2. `virtual void handle_action(std::string_view action_json)`: Processes inbound user action payloads (`Action.Execute` and `Action.Submit`) to mutate card state.

Furthermore, this card does not yet provide an in-card **Adaptive Card Presentation Override** (`adaptive_view_mode` toggle), preventing users from experiencing the declarative card layout natively inside the Rouen deck.

### 1.2 System Motivations
- **Remote Mesh Operability (`mesh://<client-id>/git`)**: Enables remote Rouen nodes (macOS, Windows, Linux) to interact with `git` without heavy video streaming or RDP.
- **Web UI & Mobile Parity**: Allows the Rouen Web client and companion apps to render native responsive cards using official Adaptive Card renderers.
- **AI Agent & MCP Automation**: Allows LLM agents (via Model Context Protocol or Telegram host) to inspect current state in structured JSON and execute card actions programmatically.
- **Declarative User Experience**: Gives desktop users an alternative clean presentation style matching modern card aesthetics.

### Story 1: As a software engineer using Rouen Mesh from a remote macOS machine
> **As a software engineer using Rouen Mesh from a remote macOS machine**,
> I want to interact with the Git card on my Windows build server (`mesh://ws-ir-01/git`) via an Adaptive Card,
> so that I can review uncommitted changes, stage files, run AI summaries, and commit without opening an interactive terminal or RDP session..

### Story 2: As a desktop power user
> **As a desktop power user**,
> I want to toggle between the classic ImGui view and an elegant Adaptive Card presentation,
> so that I have a clean, declarative dashboard view of repository health, branch status, and recent commit history..


---

## 2. Technical Architecture & Component Changes

### 2.1 C++ Class Header Adjustments
In [`src/cards/development/git.hpp`](file:///Users/ignaciorodriguez/src/rouen/src/cards/development/git.hpp), declare the virtual overrides and adaptive card caching state:

```cpp
// In git: public card
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
bool git::render(rouen::ui::ui_context& ui) {
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
- `std::string repo_path`
- `std::string current_branch`
- `std::vector<std::string> branches`
- `std::vector<file_status_entry> changed_files`
- `std::string last_ai_summary`
- `std::chrono::system_clock::time_point last_ai_summary_time`
- `bool is_clean`

---

## 3. Two-Way Action Specification

The card will parse incoming JSON actions dispatched by user interactions in `handle_action`:

| Verb | Display Title | Input Parameters | Expected Side Effects & State Mutation |
| :--- | :--- | :--- | :--- |
| `git_commit` | **Commit Staged Changes** | `commit_message (string)` | Creates a new git commit with the specified commit message. |
| `git_pull` | **Pull Remote Changes** | `none` | Fetches and merges/rebases changes from the tracked upstream remote branch. |
| `git_push` | **Push to Remote** | `none` | Pushes local branch commits to the upstream origin repository. |
| `git_checkout` | **Switch Branch** | `branch_name (choice)` | Checks out the selected local or remote branch. |
| `git_stage_file` | **Stage File** | `file_path (string)` | Stages a modified or untracked file to index. |
| `git_ai_summary` | **Generate AI Summary** | `none` | Dispatches asynchronous LLM summary generation for the current working tree diff. |


Action handling parses the JSON verb via Glaze:
```cpp
void git::handle_action(std::string_view action_json) {
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
        std::cerr << "[git] Action error: " << e.what() << std::endl;
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
  "refreshIntervalMs": 5000,
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
                  "text": "\ud83d\udce6",
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
                  "text": "Git Repository Dashboard",
                  "weight": "Bolder",
                  "size": "Large"
                },
                {
                  "type": "TextBlock",
                  "text": "/Users/ignaciorodriguez/src/rouen",
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
                  "text": "main",
                  "weight": "Bolder",
                  "color": "Accent",
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
          "title": "Branch:",
          "value": "main (tracking origin/main)"
        },
        {
          "title": "Working Tree:",
          "value": "3 modified files, 1 untracked"
        },
        {
          "title": "Upstream Sync:",
          "value": "2 commits ahead, 0 behind"
        },
        {
          "title": "Latest Commit:",
          "value": "a7f3e1b - feat(git): add adaptive card action handling"
        }
      ]
    },
    {
      "type": "Container",
      "style": "default",
      "separator": true,
      "items": [
        {
          "type": "TextBlock",
          "text": "AI Summary of Working Changes",
          "weight": "Bolder",
          "color": "Accent"
        },
        {
          "type": "TextBlock",
          "text": "- Added `get_adaptive_card_json()` and `handle_action()` to Git card.\n- Integrated two-way branch checkout and commit dispatch.\n- Implemented `adaptive_view_mode` toggle.",
          "wrap": true,
          "size": "Small"
        }
      ]
    },
    {
      "type": "Container",
      "separator": true,
      "items": [
        {
          "type": "TextBlock",
          "text": "Stage & Commit",
          "weight": "Bolder"
        },
        {
          "type": "Input.Text",
          "id": "commit_message",
          "placeholder": "Enter conventional commit message (e.g. feat(git): ...)",
          "isMultiline": false
        },
        {
          "type": "Input.ChoiceSet",
          "id": "branch_name",
          "label": "Switch Branch",
          "value": "main",
          "choices": [
            {
              "title": "main",
              "value": "main"
            },
            {
              "title": "feature/adaptive-cards",
              "value": "feature/adaptive-cards"
            },
            {
              "title": "bugfix/mesh-tunnel",
              "value": "bugfix/mesh-tunnel"
            }
          ]
        }
      ]
    }
  ],
  "actions": [
    {
      "type": "Action.Execute",
      "title": "Commit",
      "verb": "git_commit"
    },
    {
      "type": "Action.Execute",
      "title": "Pull",
      "verb": "git_pull"
    },
    {
      "type": "Action.Execute",
      "title": "Push",
      "verb": "git_push"
    },
    {
      "type": "Action.Execute",
      "title": "AI Summary",
      "verb": "git_ai_summary"
    }
  ]
}
```

---

## 5. Verification & Unit Test Plan

### 5.1 Test Cases (`tests/test_card_adaptive_interface.cpp`)
1. **Serialization (`gitAdaptiveTest.JsonSerialization`)**:
   - Instantiate `git`.
   - Assert `get_adaptive_card_json()` returns non-empty string.
   - Parse with `helpers::adaptive_cards::parser` and verify root element is `AdaptiveCard` with version `1.5`.
   - Verify all required FactSet fields and inputs exist.
2. **Action Dispatch (`gitAdaptiveTest.ActionDispatch`)**:
   - Send simulated `Action.Execute` JSON payload for each supported verb.
   - Assert corresponding card internal state is mutated correctly.
3. **Resilience & Error Handling (`gitAdaptiveTest.MalformedActionHandling`)**:
   - Pass malformed or partial JSON to `handle_action()`.
   - Assert no segfault or unhandled exception occurs.

### 5.2 Build & Execution
Run test suite under nix environment:
```bash
nix develop --command cmake --build build --target unit_tests -j2
nix develop --command ./build/tests/unit_tests --gtest_filter="gitAdaptiveTest.*"
```

Followed by compiling application and ad-hoc codesigning:
```bash
nix develop --command cmake --build build --target rouen -j2
say "Adaptive card support implemented for Git Card (`git`)"
```
