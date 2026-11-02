# Feature Request: Store-and-Forward SQLite Ring Buffer for Intermittent Satellite & Cellular Links

- **Date**: 2026-11-01
- **Type**: `feat`
- **Target Sector**: Oil & Gas (Remote Wellpads, Offshore Platforms, Midstream Pipelines) & Rural DERs
- **Status**: Open

---

## 1. Problem Statement & Motivation

In upstream and midstream Oil & Gas, field sites rely on **Starlink, VSAT, BGAN satellite, cellular CGNAT, or 900 MHz radio** links that experience rain fade, satellite handoffs, and multi-minute dropouts.

Currently:
1. [`rouen::cards::adaptive_process_card`](../src/cards/information/adaptive_process_card.hpp) only retains the single most recent JSON line in `pending_card_json_`.
2. [`rouen::mesh::rouen_mesh_host`](../src/hosts/rouen_mesh_host.hpp) drops outbound frames if the WebSocket connection is currently disconnected.
3. If an ESP trips on undercurrent or wellhead tubing/casing pressure spikes during a 3-minute satellite outage, those intermediate telemetry samples and transient alarm states are lost before the control room reconnects.

---

## 2. Proposed Architecture & Design

### A. Structured Telemetry Envelope in `adaptive-process` stdout
Allow an `adaptive-process` JSON line to include an optional top-level `"telemetry"` object alongside the Adaptive Card UI definition:

```json
{
  "type": "AdaptiveCard",
  "version": "1.5",
  "refreshIntervalMs": 2000,
  "telemetry": {
    "timestamp_ms": 1793586000000,
    "series": {
      "pad14.well3h.tubing_psi": 485.2,
      "pad14.well3h.casing_psi": 620.0,
      "pad14.well3h.oil_bopd": 1420.5
    }
  },
  "body": [ ... ]
}
```

### B. Local SQLite Store-and-Forward Ring Buffer (`src/helpers/telemetry_spool.hpp`)
- Create a lightweight SQLite-backed ring buffer (`telemetry_spool`) on the edge node with configurable retention (e.g., `MAX_SPOOL_ROWS = 100000` or 72 hours of 1-second wellpad scans).
- Every sample emitted in `"telemetry"` is appended with a monotonically increasing `seq_id` and `synced = 0` flag.

### C. Automatic Mesh Backfill on Reconnect (`rouen_mesh_host`)
- Add a mesh frame type or registry/REST synchronization endpoint (`POST /api/telemetry/backfill`) so that when `rouen_mesh_host` completes its Ed25519 handshake after a link outage, unsynced telemetry batches (`synced = 0`) are compressed (`frame_flags::COMPRESSED`) and streamed to the central historian / [`number_series_card`](../src/cards/information/number_series_card.hpp), then marked `synced = 1`.

---

## 3. Affected Files

- `src/helpers/telemetry_spool.hpp` (new)
- [`src/cards/information/adaptive_process_card.hpp`](../src/cards/information/adaptive_process_card.hpp)
- [`src/cards/information/number_series_card.hpp`](../src/cards/information/number_series_card.hpp)
- [`src/hosts/rouen_mesh_host.hpp`](../src/hosts/rouen_mesh_host.hpp)

---

## 4. Verification & Unit Test Plan

1. Add unit tests in `tests/` verifying that `telemetry_spool` persists timestamped series data across simulated disconnects, enforces ring-buffer capacity limits, and drains unsynced batches in sequence order upon reconnection.
2. Verify that `number_series_card` seamlessly merges backfilled historical points without duplicate timestamps or gaps.
