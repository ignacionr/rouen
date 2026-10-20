# Feature Request: Seamless Mesh Synchronization of LLM Configurations and AI Personas

## Summary

Implement seamless, real-time, bidirectional synchronization of LLM provider configurations (`llm_configs.json`) and AI Personas across all Rouen Mesh-connected instances (`UniversalSyncHost`, `rouen_mesh_host`, `PersonaManager`, and `LLMConfigManager`).

Currently, persona changes are exported to individual JSON files in `cache/personas/<slug>.json` and published to the Mesh persistent registry (`sync/v1/personas/<slug>.json`), while LLM configurations are published to `sync/v1/config/llm_configs.json`. However, synchronization between active peer nodes is not seamless due to three critical architectural bottlenecks:
1. **Coupling to Desktop Window State (`import_config`)**: `llm_configs.json` is only imported if `import_config` is enabled, which is frequently bypassed during sync routines to prevent overwriting local desktop window coordinates, scale factors, and visual themes (`rouen.ini`).
2. **Blind File Overwrites vs. Granular Profile Merging**: Importing `llm_configs.json` blindly overwrites the entire file on disk, which can clobber machine-local API keys, custom local endpoints (e.g., Apple MLX on port 8098), or concurrent profile modifications.
3. **Absence of Reactive WebSocket Ingestion**: When a peer publishes an update to `sync/v1/personas/*` or `sync/v1/config/llm_configs.json`, receiving nodes update their in-memory `registry_entries_` map, but do not reactively decrypt and apply changes into `PersonaManager` and `LLMConfigManager` until a full 5-minute periodic sync or manual pull occurs.

This specification defines the architecture, data flow, reactive event hooks, and safe merging logic required to achieve instant, zero-friction, conflict-free synchronization of AI models and personas across all connected mesh workstations and mobile nodes.

---

## Background & Architecture Tracing

In Rouen, AI capabilities are distributed across desktop cards ([`AIChat`](file:///Users/inz/src/rouen/src/cards/information/ai_chat.hpp)), background daemons ([`TelegramHost`](file:///Users/inz/src/rouen/src/hosts/telegram_host.hpp)), and mesh remote procedure calls.

```mermaid
flowchart TD
    subgraph Node_A["Mesh Node A (Mac Mini / Roaming)"]
        UI_A["AIChat / Settings Card / PersonaArchitect"] -->|Mutates Persona or LLM Profile| PM_A["PersonaManager / LLMConfigManager"]
        PM_A -->|sync_item()| USH_A["UniversalSyncHost"]
        USH_A -->|Encrypt Envelope & Publish| MESH_A["rouen_mesh_host (WebSocket)"]
    end

    subgraph Relay["Rouen Mesh Relay & Ephemeral Registry"]
        REG["sync/v1/personas/<slug>.json\nsync/v1/config/llm_configs.json"]
    end

    subgraph Node_B["Mesh Node B (Remote Workstation / Laptop)"]
        MESH_B["rouen_mesh_host (WebSocket)"] -->|REGISTRY_SET frame| REACTIVE["Reactive Mesh Sync Hook (NEW)"]
        REACTIVE -->|Decrypt & Ingest| USH_B["UniversalSyncHost"]
        USH_B -->|Granular Persona Upsert| PM_B["PersonaManager"]
        USH_B -->|Smart Profile Merge| LCM_B["LLMConfigManager"]
        PM_B & LCM_B -->|Notify Reload Event| UI_B["AIChat / Settings / TelegramHost"]
    end

    MESH_A -->|REGISTRY_SET| REG
    REG -->|Push Notification| MESH_B
```

### Current Code Paths and Limitations

1. **`src/hosts/universal_sync_host.cpp` (Lines 134–154)**:
   ```cpp
   if (import_config) {
       UniversalSyncHost::copy_file_if_exists(cache_dir / "config" / "rouen.ini", ...);
       UniversalSyncHost::copy_file_if_exists(cache_dir / "config" / "themes.json", ...);
       UniversalSyncHost::copy_file_if_exists(cache_dir / "config" / "llm_configs.json", ...);
   }
   ```
   - If `sync_in` or `sync_in_mesh` is called with `import_config = false` (or when syncing datasets on a node with customized window placement), `llm_configs.json` is omitted entirely.
   - Persona and LLM synchronization must be categorized as core AI intelligence assets, decoupled from desktop shell layout settings.

2. **`src/hosts/llm_host.cpp` (Lines 374–455)**:
   - `LLMConfigManager::save_configs()` calls `notify_sync("config", "llm_configs.json", json_str, false)`.
   - When received on the target node, there is no entity-level merging logic. If Node A adds a new profile (e.g., `Gemini 4 Argon`), a blind overwrite would replace Node B's configuration, potentially deleting Node B's local custom profiles or wiping out Node B's API keys if Node A had empty credentials.

3. **`src/hosts/rouen_mesh_host.cpp` (Lines 1527–1554)**:
   - When an incoming `REGISTRY_SET` frame arrives via WebSocket:
   ```cpp
   case mesh::frame_type::REGISTRY_SET: {
       mesh::registry_set_dto set_req{};
       if (glz::read_json(set_req, frame.payload) == glz::error_code::none) {
           registry_entries_[set_req.key] = mesh::registry_entry_dto{...};
           if (set_req.key == "config/OPENWEATHER_KEY") { ... }
           process_incoming_notifications();
       }
   }
   ```
   - For `sync/v1/*` keys, only the in-memory registry map is updated. The node does not automatically decrypt the payload or ingest it into the local database/cache until the next periodic sync timer fires (up to 300 seconds later).

---

## Detailed Requirements

### A. Decouple AI Configurations from Desktop Window Layout
- Move `llm_configs.json` synchronization out of the `if (import_config)` gate in `UniversalSyncHost::import_all_local_datasets`.
- Ensure AI models (`llm_configs.json`) and Personas (`personas/`) are synchronized on every sync pass (`sync_in`, `sync_in_mesh`, `sync_twoway`, and periodic worker), regardless of whether `import_config` is `true` or `false`.

### B. Smart Granular Merging in `LLMConfigManager`
Implement `LLMConfigManager::merge_from_json(const std::string& json_content)`:
1. Parse the incoming `LLMConfigSaveModel` payload.
2. For each remote profile `remote_cfg`:
   - If a profile with the same name already exists locally:
     - Update `model_name`, `provider`, `base_url`, and `thinking_level` to match the incoming configuration.
     - **Credential Preservation Rule**: If the local entry has a non-empty `api_key` and the incoming entry is empty, preserve the local `api_key`. If the incoming entry contains a non-empty key (e.g., an updated cluster key), adopt the non-empty key.
   - If the profile does not exist locally:
     - Append the new profile to `configs_`.
     - If the new profile's `api_key` is empty, attempt to populate it from local environment variables via `ConfigService::get_env_optional()`.
3. If the incoming payload specifies a `default_config_name` that exists locally or was just imported, adopt it if the current local default is missing or points to generic fallbacks.
4. Save the merged configuration and trigger `registrar::call("llm_config_reload")`.

### C. Reactive Mesh Registry Event Hooks (Zero-Delay Sync)
Extend `rouen_mesh_host::handle_incoming_frame` for `mesh::frame_type::REGISTRY_SET`:
1. Check if `set_req.key.starts_with("sync/v1/personas/")`:
   - Extract the relative slug path (e.g., `personas/autonomous-engineer.json`).
   - Decrypt the envelope using `SyncCryptoService::instance().decrypt_envelope(set_req.value)`.
   - Write or remove the JSON file in `cache_dir / "personas" / ...`.
   - Immediately invoke `PersonaManager::instance().import_from_directory(cache_dir / "personas")`.
   - Emit an application-wide event via `registrar::call("personas_updated")` so all active `AIChat` cards update their persona dropdown without requiring a restart.
2. Check if `set_req.key == "sync/v1/config/llm_configs.json"`:
   - Decrypt the envelope using `SyncCryptoService::instance().decrypt_envelope(set_req.value)`.
   - Invoke `LLMConfigManager::instance().merge_from_json(decrypted->plaintext)`.
   - Emit `registrar::call("llm_config_reload")`.

### D. Real-Time Mutation Hooks
1. Ensure every persona mutation in `PersonaManager` (`add_persona`, `update_persona`, `delete_persona`, `select_persona`) continues to invoke `sync_persona_item()` and `sync_active_persona()`.
2. Ensure every LLM config mutation in `LLMConfigManager` (`add_config`, `update_config`, `delete_config`, `set_default_config_name`) invokes `save_configs()` which triggers `sync_item("config", "llm_configs.json", ...)`.

---

## Technical Implementation Plan

1. **`src/hosts/llm_host.hpp` & `src/hosts/llm_host.cpp`**:
   - Add `bool merge_from_json(std::string_view json_content)` to `LLMConfigManager`.
   - Implement intelligent merge logic with credential preservation.
   - Register a callable lambda in `registrar`: `"llm_config_merge"`.

2. **`src/hosts/universal_sync_host.cpp`**:
   - In `import_all_local_datasets()`:
     - Move `llm_configs.json` import logic outside `if (import_config)` into an unconditional AI config import block.
     - Call `LLMConfigManager::instance().merge_from_json(...)` instead of raw `copy_file_if_exists`.
   - In `export_all_local_datasets()`:
     - Ensure `cache_dir / "config" / "llm_configs.json"` is always exported.

3. **`src/hosts/rouen_mesh_host.cpp`**:
   - In the `REGISTRY_SET` handler (line 1527), add reactive dispatch:
     ```cpp
     if (set_req.key.starts_with("sync/v1/personas/") || set_req.key == "sync/v1/config/llm_configs.json") {
         auto sync_dispatch = registrar::try_get<std::function<void(std::string_view, std::string_view)>>("mesh_sync_registry_update");
         if (sync_dispatch) {
             (*sync_dispatch)(set_req.key, set_req.value);
         }
     }
     ```
   - Register `"mesh_sync_registry_update"` in `UniversalSyncHost` constructor to process incoming envelopes asynchronously or synchronously on arrival.

4. **Testing (`tests/test_universal_mesh_sync.cpp`)**:
   - Add test case `test_reactive_persona_sync`:
     - Simulate Node A updating a persona.
     - Inject `REGISTRY_SET` into Node B.
     - Verify Node B's `PersonaManager` updates immediately.
   - Add test case `test_llm_config_smart_merge`:
     - Verify merging preserving local API keys while adopting new model identifiers.

---

## Acceptance Criteria

- [ ] Modifying any Persona on one Rouen instance immediately reflects on peer mesh instances within < 1 second.
- [ ] Modifying or adding a named LLM profile propagates to peer instances without overwriting local machine credentials or wiping custom endpoints.
- [ ] Two-way sync passes run without desynchronization, conflict loops, or missing LLM configurations, even when `import_config` is set to `false`.
- [ ] Automated unit tests in `tests/test_universal_mesh_sync.cpp` validate reactive persona updates and credential-preserving config merges.
