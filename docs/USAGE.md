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
* `BYBIT_API_KEY` & `BYBIT_SECRET_KEY`: Cryptocurency asset and spot portfolio tracking.
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

* **Directory Explorer**: `dir:/absolute/path/to/folder` opens a folder browser card.
* **Markdown Notes**: 
  - `notes:` opens the note management index.
  - `notes:my-note` opens or creates a note named "my-note".
* **Trello Boards**:
  - `trello:` opens Trello board search and card listings.
  - `trello-board:<board-id>` directly opens a dedicated column board viewer.
* **Adaptive Cards (Round 1)**:
  - `adaptive-card` opens the built-in Adaptive Card test card with a dropdown to choose Round 1/2/3/4 sample JSON sets.
  - `adaptive-card:<card_json_path>|<context_json_path>` loads a card JSON file plus a context JSON file for `${var}` substitution.
  - The card includes a **JSON** tab showing the loaded card JSON, context JSON, and bound JSON output used by the renderer.
  - Round 2 adds `Container`, `ColumnSet`, `Column`, and `FactSet`, with nested `${user.profile.name}` style bindings.
  - Round 3 adds `Input.Text`, `Input.Toggle`, and `Action.OpenUrl` with expression-based URL templating.
  - Round 4 adds `$data` repeats plus `Action.Submit` and `Action.ShowCard`; submit payloads and input state are inspectable in the card UI.

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

## Troubleshooting

### SSL/TLS Certificate Handshake Errors
* If you see CURL handshake errors on corporate proxies or custom servers, set `ROUEN_SSL_MODE=relaxed` in your `.env` config file.

### Audio or Video Not Playing
* Rouen relies on a local installation of the `mpv` binary. Verify `mpv` is available in your shell's PATH, or set the explicit path inside the Settings card.

### API Key Updates Not Reflecting
* Values inside `.env` or system profiles are cached. Use the Settings card's **Refresh Cache** button, or restart the application to reload variables.
