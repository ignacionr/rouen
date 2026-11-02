# Feature Request: Industrial Alarm Management Lifecycle (ISA-18.2 / API RP 1167) & Mesh Annunciator Card

- **Date**: 2026-11-01
- **Type**: `feat`
- **Target Sector**: Oil & Gas Control Rooms (API RP 1167) & Process/Power Industries (ISA-18.2 / IEC 62682)
- **Status**: Open

---

## 1. Problem Statement & Motivation

In Oil & Gas pipeline control rooms (governed by **API RP 1167** and PHMSA CRM rules) and process plants (**ISA-18.2**), alarms are not simple fire-and-forget toast notifications. Every process alarm must follow a strict state machine so operators never miss a critical condition (such as high H2S gas detection, tank high-high level, or pipeline leak/pressure drop):

- **Unacknowledged Active (`UNACK_ALM`)**: Audible/visual flashing alert requiring operator acknowledgment.
- **Acknowledged Active (`ACK_ALM`)**: Operator has acknowledged the alarm; indicator remains steady until the physical condition clears.
- **Unacknowledged Return-to-Normal (`UNACK_RTN`)**: The physical sensor returned to normal before the operator acknowledged the transient excursion (latched until acknowledged so transients aren't missed).
- **Normal / Cleared (`NORMAL`)**: Condition is normal and acknowledged.
- **Shelved / Suppressed (`SHELVED`)**: Temporarily suppressed by an operator with a mandatory expiration timer and reason.

---

## 2. Proposed Architecture & Design

### A. First-Class `"alarms"` Array in Adaptive Process Payloads
Allow any `adaptive-process` stdout JSON payload to declare active process alarm conditions:

```json
{
  "type": "AdaptiveCard",
  "version": "1.5",
  "alarms": [
    {
      "id": "pad14.well3h.h2s_high",
      "severity": "critical",
      "title": "Pad 14 H2S Concentration High",
      "value": "14.2 ppm (Limit: 10.0 ppm)",
      "latched": true
    }
  ],
  "body": [ ... ]
}
```

### B. ISA-18.2 State Machine & Ephemeral Mesh Registry Propagation
- Create `src/helpers/alarm_manager.hpp` implementing the ISA-18.2 state transitions (`UNACK_ALM`, `ACK_ALM`, `UNACK_RTN`, `SHELVED`, `NORMAL`).
- Automatically publish active edge alarms to the Rouen Mesh ephemeral registry under `alarms/<client_id>` via [`rouen_mesh_host`](../src/hosts/rouen_mesh_host.hpp).

### C. Mesh Alarm Annunciator Card (`alarms:` URI)
- Create a dedicated card (`src/cards/system/alarm_annunciator_card.hpp`, registered at `alarms:`) that displays a consolidated, priority-sorted (Critical / High / Medium / Advisory) alarm table across local and paired mesh nodes.
- Provide one-click **Acknowledge**, **Shelve (15m / 1h)**, and **Jump to Asset Card** (`[node]/adaptive-process:...`) actions, sending acknowledgment events (`{"verb":"ack_alarm","alarm_id":"..."}`) back to the originating edge process.

---

## 3. Affected Files

- `src/helpers/alarm_manager.hpp` (new)
- `src/cards/system/alarm_annunciator_card.hpp` (new)
- [`src/cards/information/adaptive_process_card.hpp`](../src/cards/information/adaptive_process_card.hpp)
- [`src/cards/interface/factory.hpp`](../src/cards/interface/factory.hpp)

---

## 4. Verification & Unit Test Plan

1. Add unit tests in `tests/` covering all ISA-18.2 state transitions (`NORMAL` $\rightarrow$ `UNACK_ALM` $\rightarrow$ `UNACK_RTN` $\rightarrow$ `NORMAL` and `UNACK_ALM` $\rightarrow$ `ACK_ALM` $\rightarrow$ `NORMAL`, plus timed shelving expiry).
2. Verify that an `adaptive-process` emitting an `"alarms"` array triggers the `alarm_manager` and forwards operator acknowledgments back to the child process's `stdin`.
