# Feature Request: Select-Before-Operate (SBO) Safety Interlocks, Operator-Aware RBAC, and Signed Command Audit Log

- **Date**: 2026-11-01
- **Type**: `feat`
- **Target Sector**: Oil & Gas (API RP 1164 Pipeline Security, PHMSA CRM) & Electric Utilities (NERC CIP, ISA/IEC 62443)
- **Prerequisite**: **[`2026-11-01-feat-oidc-sso-operator-identity.md`](2026-11-01-feat-oidc-sso-operator-identity.md)** (OIDC / OAuth 2.0 SSO Operator Identity & Offline Edge JWT Verification)
- **Status**: Open

---

## 1. Problem Statement & Motivation

When operating physical infrastructure—such as actuating a wellhead Surface Safety Valve (SSV), changing an ESP frequency, adjusting a pipeline pressure control valve, or opening a substation breaker—three safety and compliance controls are mandatory under **API RP 1164**, **PHMSA**, and **NERC CIP**:

1. **Verified Human Operator Identity Prerequisite**: Control actions cannot be authorized solely by machine identity (`rouen_mesh_host` Ed25519 node keys) because multiple humans share control room consoles. This feature builds directly on [`2026-11-01-feat-oidc-sso-operator-identity.md`](2026-11-01-feat-oidc-sso-operator-identity.md) (`rouen::services::auth_service`), requiring a valid, non-expired **OIDC Operator JWT (`verified_operator_identity`)** before any state-changing action is permitted.
2. **Select-Before-Operate (SBO) Two-Step Interlock**: A single accidental click on an HMI button must never immediately actuate a physical valve or breaker.
3. **Role-Based Access Control (RBAC) & Dual-Identity Command Audit Trail**: Production analysts, management, and third-party vendors require **Read-Only** telemetry views (`scada.viewer`), while only authorized Gas Controllers or Lease Operators (`scada.operator`) may dispatch control actions—and every command must record **both** the authenticated human operator (`sub`, `email`, `roles`) and the originating machine (`node_id`, `ed25519_pubkey`).

---

## 2. Proposed Architecture & Design

### A. Prerequisite Check: Require Verified Operator Identity (`auth_service`)
When `ROUEN_REQUIRE_OPERATOR_AUTH=1` is enabled on an edge node or operator workspace:
- Any non-`tick` `POST /api/cards/action` request (local or proxied over [`rouen_mesh_host`](../src/hosts/rouen_mesh_host.hpp)) must carry a valid `X-Rouen-Operator-JWT` verified by `auth_service::verify_jwt()` ([`2026-11-01-feat-oidc-sso-operator-identity.md`](2026-11-01-feat-oidc-sso-operator-identity.md)).
- If no operator is signed in (or the operator's shift token has expired), the action is rejected with `401 Unauthorized: Active SSO operator session required` and the UI prompts the operator to sign in via the `auth:` card.

### B. Operator- & Node-Aware Role-Based Access Control (RBAC)
In [`src/hosts/api_server_host.cpp`](../src/hosts/api_server_host.cpp) and [`src/cards/information/adaptive_process_card.hpp`](../src/cards/information/adaptive_process_card.hpp):
- Support role requirements at both the endpoint level and on individual Adaptive Card actions (`"requiredRole": "scada.operator"`):
  - **`scada.viewer`**: Permitted to call `GET /api/cards` and `GET /api/cards/adaptive` (including automatic `verb="tick"` polling), but blocked from `POST /api/cards/action` (`403 Forbidden: Operator role 'scada.operator' required`).
  - **`scada.operator`**: Permitted to execute operational actions (`set_choke`, `apply_setpoint`, `ack_alarm`).
  - **`scada.safety_engineer` / `scada.admin`**: Permitted to execute destructive/safety overrides (`esd_shutin`, `shelve_alarm`, profile changes).

### C. Select-Before-Operate (`requiresConfirmation`) Modal with Operator Attribution
Extend [`src/helpers/adaptive_cards/parser.hpp`](../src/helpers/adaptive_cards/parser.hpp) and [`renderer.hpp`](../src/helpers/adaptive_cards/renderer.hpp) to support safety interlock metadata on `Action.Execute` and `Action.Submit`:

```json
{
  "type": "Action.Execute",
  "title": "ESD Shut-In Well 3H",
  "verb": "esd_shutin",
  "style": "destructive",
  "requiredRole": "scada.operator",
  "requiresConfirmation": true,
  "confirmationPrompt": "Confirm Emergency Shut-In of Permian Pad 14 Well 3H (SSV will close immediately):",
  "requireReasonNote": true
}
```
- When clicked, the ImGui renderer checks that an operator with `requiredRole` is currently signed in, then opens a modal **Select-Before-Operate (ARMED)** dialog displaying:
  - **Authenticated Operator**: `Jane Doe (jdoe@permian-energy.com) [scada.operator]`
  - **Target Asset & Command**: `[pad-permian-14] ESD Shut-In Well 3H`
  - **Mandatory Reason Note Input** (`"operatorReason"`, when `requireReasonNote: true`)
  - **10-Second Arming Countdown** (auto-disarms if not confirmed within 10s)
  - Explicit **CONFIRM & EXECUTE** and **ABORT** buttons.

### D. Dual-Identity Append-Only Command Audit Log
Whenever a control action is attempted (permitted or denied), append a tamper-evident record to SQLite `command_audit_log` (and `logs/command_audit.jsonl`) capturing **both** human and machine identities:
- `timestamp_utc`
- **Human Operator Identity** (from OIDC JWT): `operator_sub`, `operator_email`, `operator_name`, `operator_roles`, `jwt_jti`
- **Machine Identity** (from Rouen Mesh): `source_node_id`, `source_ed25519_pubkey`
- **Command Context**: `target_card_uri`, `verb`, `payload_json`, `operator_reason`, and `decision` (`EXECUTED`, `DENIED_UNAUTHENTICATED`, `DENIED_INSUFFICIENT_ROLE`).

---

## 3. Affected Files

- [`src/helpers/adaptive_cards/parser.hpp`](../src/helpers/adaptive_cards/parser.hpp)
- [`src/helpers/adaptive_cards/renderer.hpp`](../src/helpers/adaptive_cards/renderer.hpp)
- `src/helpers/command_audit_log.hpp` (new)
- [`src/hosts/api_server_host.cpp`](../src/hosts/api_server_host.cpp)
- [`src/hosts/rouen_mesh_host.hpp`](../src/hosts/rouen_mesh_host.hpp)

---

## 4. Verification & Unit Test Plan

1. Add parser unit tests in `tests/` verifying `requiresConfirmation`, `confirmationPrompt`, `requireReasonNote`, and `requiredRole` deserialization on `Action.Execute` and `Action.Submit`.
2. Add RBAC + SSO integration unit tests confirming that:
   - Unauthenticated requests (`POST /api/cards/action` without a valid `X-Rouen-Operator-JWT`) fail with `401 Unauthorized` and log `DENIED_UNAUTHENTICATED`.
   - Authenticated requests with `roles: ["scada.viewer"]` fail with `403 Forbidden` when invoking an action requiring `"scada.operator"` and log `DENIED_INSUFFICIENT_ROLE`.
   - Authenticated requests with `roles: ["scada.operator"]` succeed, forward the verified `"_operator"` + `"operatorReason"` payload to `adaptive_process_card`, and record `EXECUTED` in `command_audit_log`.
