# Feature Request: Edge Command Allowlist & Named Profiles for Locked-Down Adaptive Process Execution

- **Date**: 2026-11-01
- **Type**: `feat`
- **Target Sector**: Energy & Utilities / Oil & Gas OT Cybersecurity (ISA/IEC 62443, NERC CIP, API RP 1164)
- **Status**: Open

---

## 1. Problem Statement & Motivation

Currently, [`rouen::cards::adaptive_process_card`](../src/cards/information/adaptive_process_card.hpp) accepts an arbitrary command line string directly in its URI locator:

```text
[pad-permian-14]/adaptive-process:python3 /opt/oilfield/esp_wellhead_bridge.py --well 3H
```

While this is very convenient during local development, an OT security team conducting a penetration test or **ISA/IEC 62443 / NERC CIP** audit on a field gateway will flag arbitrary remote command invocation over `POST /api/cards` or [`mesh_card_proxy`](../src/cards/interface/mesh_card_proxy.hpp) as an arbitrary remote code execution (RCE) vector if an operator console is ever compromised.

---

## 2. Proposed Architecture & Design

### A. Named Profile Registry (`adaptive_process_profiles.json`)
Support a local configuration file on the edge node (e.g., `adaptive_process_profiles.json` or `.env` configuration via [`config_service`](../src/config_service.hpp)) that maps human-readable profile aliases to fixed, administrator-approved command lines and optional SHA-256 script hashes:

```json
{
  "allowArbitraryCommands": false,
  "profiles": {
    "well-3h-esp": {
      "title": "Permian Pad 14 — Well 3H ESP & Choke",
      "command": "python3 /opt/oilfield/esp_wellhead_bridge.py --well 3H",
      "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
    },
    "bess-unit-1": {
      "title": "Substation West 04 — BESS Unit 1",
      "command": "python3 /opt/grid/modbus_bess_controller.py --unit 1"
    }
  }
}
```

### B. Locked-Down Mode (`ADAPTIVE_PROCESS_ALLOWLIST_ONLY=true`)
- When `allowArbitraryCommands` is `false` (or `ADAPTIVE_PROCESS_ALLOWLIST_ONLY=true` in `.env`):
  - `adaptive-process:<locator>` only resolves `<locator>` against the approved keys in `profiles` (e.g., `[pad-permian-14]/adaptive-process:well-3h-esp`).
  - Any attempt to pass an unlisted command string is rejected with a clear security diagnostic on the card (`"Command rejected: Edge node is in allowlist-only mode (ISA/IEC 62443 lockdown)"`) and logged.
  - If `"sha256"` is specified on the profile, Rouen verifies the script file's SHA-256 digest before spawning [`piped_process`](../src/helpers/piped_process.hpp).

### C. Profile Discovery Endpoint (`GET /api/adaptive-process/profiles`)
- Expose the list of approved profile names and titles via `GET /api/adaptive-process/profiles` so [`mesh_card_proxy`](../src/cards/interface/mesh_card_proxy.hpp) navigator cards (`[pad-permian-14]`) can display a one-click catalog of available sensor/RTU cards on that edge node.

---

## 3. Affected Files

- [`src/cards/information/adaptive_process_card.hpp`](../src/cards/information/adaptive_process_card.hpp)
- [`src/cards/interface/mesh_card_proxy.hpp`](../src/cards/interface/mesh_card_proxy.hpp)
- [`src/hosts/api_server_host.hpp`](../src/hosts/api_server_host.hpp)
- [`docs/ADAPTIVE_PROCESS_CARDS.md`](../docs/ADAPTIVE_PROCESS_CARDS.md)

---

## 4. Verification & Unit Test Plan

1. Add unit tests in `tests/` verifying that when allowlist-only mode is enabled, registered profile aliases resolve to their configured command lines while raw shell commands are blocked before reaching `piped_process`.
2. Add unit tests verifying optional SHA-256 script integrity verification prior to process launch.
