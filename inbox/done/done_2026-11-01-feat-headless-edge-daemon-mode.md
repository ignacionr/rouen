# Feature Request: Enhance Existing `--daemon` (`-d` / `--headless`) Mode to Host Headless `deck` & `adaptive_process_card` Instances

- **Date**: 2026-11-01
- **Type**: `feat`
- **Target Sector**: Energy & Utilities / Oil & Gas (Upstream Wellpads, Midstream Pipelines, Substations)
- **Status**: Open

---

## 1. Problem Statement & Audit of Existing `--daemon` Mode

[`src/rouen.cpp`](../src/rouen.cpp#L161-L162) **already** unifies `-d`, `--daemon`, `--headless`, and `--headless-mesh` under `daemon_mode = true` ([lines 447–565](../src/rouen.cpp#L447-L565)), which cleanly skips SDL/OpenGL window creation and starts:
- `presence_service` (marked headless)
- `api_server_host` (`http://127.0.0.1:8081`)
- `rouen_mesh_host` (outbound WebSocket mesh + Ed25519 identity)
- `telegram_host` and `UniversalSyncService`

However, **two specific gaps prevent `--daemon` from serving cards (`adaptive-process:`, `number-series:`, etc.) over the REST API and Rouen Mesh**:

1. **No `deck` Registered in `daemon_mode`**:
   - In GUI mode, [`deck`](../src/cards/interface/deck.hpp#L58-L140) is constructed inside `main_wnd::run()`, which registers `"create_card"`, `"get_active_cards"`, `"close_card"`, and `"get_deck_status"` in `registrar`.
   - In `if (daemon_mode)` ([`src/rouen.cpp:447`](../src/rouen.cpp#L447-L565)), `deck` is never instantiated. Consequently, any request to `GET /api/cards`, `POST /api/cards`, `GET /api/cards/adaptive`, or `POST /api/cards/action` (including remote [`mesh_card_proxy`](../src/cards/interface/mesh_card_proxy.hpp) requests from a control room) fails with `{"error":"Active cards service not available"}` ([`src/hosts/api_server_host.cpp:1071`](../src/hosts/api_server_host.cpp#L1069-L1073)).
2. **`adaptive_process_card` State Ingestion & REST Overrides Are Tied to `render()`**:
   - [`rouen::cards::adaptive_process_card`](../src/cards/information/adaptive_process_card.hpp) currently pulls new `stdout` JSON lines via [`apply_pending_update()`](../src/cards/information/adaptive_process_card.hpp#L119-L130) inside `render()`, and does not yet override `card::get_adaptive_card_json()` or `card::handle_action(std::string_view)`.
   - Therefore, even if created on a headless deck, `/api/cards/adaptive` and `/api/cards/action` cannot read the child process's emitted Adaptive Card JSON or forward operator `Action.Execute` / `Action.Submit` payloads to the child process's `stdin`.

---

## 2. Proposed Architecture & Design (Repurposing `--daemon`)

Rather than introducing a separate flag or binary, we enhance the existing `if (daemon_mode)` path in [`src/rouen.cpp`](../src/rouen.cpp#L447-L565):

### A. Instantiate a Headless-Safe `deck` in `daemon_mode` (`src/rouen.cpp` & `src/cards/interface/deck.hpp`)
- Ensure `deck(SDL_Renderer* sdl_renderer)` in [`src/cards/interface/deck.hpp`](../src/cards/interface/deck.hpp#L58) is completely safe when `sdl_renderer == nullptr` (skipping any ImGui/SDL texture calls during construction/destruction).
- Load plugins via `plugin_host::instance().load_all_plugins()` and instantiate `deck daemon_deck{nullptr};` inside `if (daemon_mode)` before starting `api_server_host` and `rouen_mesh_host`.
- Add a lightweight `daemon_deck.tick_headless()` method called inside the existing `while (s_daemon_running.load())` loop ([`src/rouen.cpp:542`](../src/rouen.cpp#L542-L544)) to process deferred card creations/deletions (`deferred_operations`) without an ImGui frame loop.

### B. Implement `get_adaptive_card_json()` and `handle_action()` on `adaptive_process_card`
In [`src/cards/information/adaptive_process_card.hpp`](../src/cards/information/adaptive_process_card.hpp):
1. **Override `get_adaptive_card_json() const override`**:
   - Call `const_cast<adaptive_process_card*>(this)->apply_pending_update()` (or parse incoming `stdout` lines directly under `mutex_`) and return the latest valid compact Adaptive Card JSON line emitted by the child process (or a diagnostic status card if the process is still starting or exited with `stderr`).
2. **Override `handle_action(std::string_view action_json) override`**:
   - Forward the incoming JSON action payload directly to `post_to_process(std::string(action_json))` so `POST /api/cards/action` and remote [`mesh_card_proxy`](../src/cards/interface/mesh_card_proxy.hpp) actions write to the child process's `stdin`.
3. **Background Cadence Tick (`refreshIntervalMs`)**:
   - When `bound_` specifies `"refreshIntervalMs"`, allow `tick_headless()` (or `/api/cards/adaptive` polling) to trigger periodic `{"verb":"tick"}` refreshes even when `render()` is not running.

---

## 3. Affected Files

- [`src/rouen.cpp`](../src/rouen.cpp) (lines 447–565 `if (daemon_mode)`)
- [`src/cards/interface/deck.hpp`](../src/cards/interface/deck.hpp)
- [`src/cards/information/adaptive_process_card.hpp`](../src/cards/information/adaptive_process_card.hpp)

---

## 4. Verification & Unit Test Plan

1. Add a unit test in `tests/` instantiating `adaptive_process_card` without an ImGui/SDL context, verifying that `get_adaptive_card_json()` returns the child process's `stdout` Adaptive Card JSON and `handle_action()` writes the action JSON to the child's `stdin` and updates the card state—**without ever calling `render()`**.
2. Verify that running `rouen --daemon` serves `GET /api/cards`, `POST /api/cards` (`{"uri":"adaptive-process:..."}`), `GET /api/cards/adaptive`, and `POST /api/cards/action` over both localhost (`:8081`) and `mesh_card_proxy`.
