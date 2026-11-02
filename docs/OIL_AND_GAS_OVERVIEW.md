# Rouen for the Oil & Gas Industry (Upstream, Midstream & Downstream)

Oil and gas operations—from unconventional multi-well pads and artificial lift systems to midstream compressor stations, custody-transfer pipelines, and downstream LNG/refinery analyzer shelters—confront two acute operational hurdles:

1. **Extreme Remote Connectivity & Strict OT Security (API RP 1164 & ISA/IEC 62443)**: Wellpads, block valves, and offshore platforms connect over high-latency, metered VSAT, Starlink, cellular CGNAT, or 900 MHz radio networks. Under **API RP 1164** and the Purdue Model (Levels 1–3), opening inbound ports to field RTUs or edge gateways is strictly prohibited.
2. **Oilfield Protocol Diversity**: Field instrumentation spans decades of specialized protocols—**Enron Modbus, Emerson ROC, Bristol BSAP, HART-IP, OPC-UA, DNP3, WITS0/WITSML**, and proprietary serial gas chromatographs.

**Rouen** bridges field assets to central production and gas-control rooms using two native C++20 primitives: **Rouen Mesh** (outbound-only WebSocket federation) and **Adaptive Process Cards** (language-agnostic `stdio` RTU and sensor bridges).

---

## 1. Outbound-Only WebSockets (`rouen_mesh_host`) for Wellpads & Pipelines

Implemented in [`rouen::mesh::rouen_mesh_host`](../src/hosts/rouen_mesh_host.hpp), every Rouen field gateway establishes a **single outbound TLS WebSocket (`wss://`)** connection on port `443`:

* **Zero Inbound Firewall Rules**: Field nodes behind Starlink CGNAT, cellular modems, or Purdue Level 3 DMZ firewalls initiate outbound connections only—eliminating fragile site-to-site IPsec VPNs across hundreds of wellpads.
* **Mutual Ed25519 Cryptographic Identity**: Every field gateway and operator console authenticates via **Ed25519 challenge-response signatures** ([`CRYPTOGRAPHIC_HANDSHAKE_SPEC.md`](CRYPTOGRAPHIC_HANDSHAKE_SPEC.md)), removing shared static credentials on unmanned sites.
* **Low-Overhead Binary Multiplexing**: Compact JSON card states, ephemeral registry heartbeats (`REGISTRY_SET`), and on-demand diagnostic tunnels (`ROUTE_OPEN`) share a single persistent connection, preserving precious satellite and cellular data quotas.

---

## 2. Adaptive Process Cards (`adaptive_process_card`) for Oilfield Protocols

Implemented in [`rouen::cards::adaptive_process_card`](../src/cards/information/adaptive_process_card.hpp) and documented in [`ADAPTIVE_PROCESS_CARDS.md`](ADAPTIVE_PROCESS_CARDS.md), the `adaptive-process:<command>` URI scheme turns **any script or binary in any language** into a live, interactive SCADA/production card over standard `stdin`, `stdout`, and `stderr`:

* **Telemetry via `stdout`**: A Python, Go, or Rust script polling an Emerson FloBoss RTU, ABB Totalflow, or ESP variable speed drive prints one line of compact Adaptive Card JSON to `stdout` to update the operator UI.
* **Setpoint & Valve Control via `stdin`**: Operator actions (`Action.Execute`, such as adjusting an automated production choke, changing ESP frequency, or triggering a surface safety valve shut-in) are serialized as a single JSON line to the script's `stdin`.
* **Fault Isolation**: Managed via [`rouen::helpers::piped_process`](../src/helpers/piped_process.hpp), a hung RS-485 serial bus or unresponsive RTU never blocks the gateway; `stderr` is captured live in-card for remote troubleshooting.

---

## 3. The "Killer Combo": Mesh Card Proxy + Adaptive Process on the Wellpad

Combining [`rouen::cards::mesh_card_proxy`](../src/cards/interface/mesh_card_proxy.hpp) with `adaptive-process:` lets a production engineer or gas controller open a live, two-way card executing directly on a remote wellpad gateway:

```text
[pad-permian-14]/adaptive-process:python3 /opt/oilfield/esp_wellhead_bridge.py --well 3H
```

* **Fieldbus Stays on the Pad**: Raw Enron Modbus, ROC, and HART polling stays strictly on the wellpad LAN/serial bus. Only compact JSON UI states and authenticated operator commands traverse the WebSocket mesh.
* **Native C++20 Edge Footprint**: Rouen compiles to a single native binary with minimal RAM/CPU usage, running effortlessly on ruggedized Class I Div 2 DIN-rail Linux/Windows edge gateways.

---

## 4. Reference Implementation: Wellhead ESP & Choke Controller (`esp_wellhead_bridge.py`)

```python
#!/usr/bin/env python3
# Launch remotely over Mesh: [pad-permian-14]/adaptive-process:python3 /opt/oilfield/esp_wellhead_bridge.py --well 3H
import json, sys, time

well = {
    "name": "Permian Pad 14 — Well 3H (ESP + Auto Choke)",
    "tubing_psi": 485.0,
    "casing_psi": 620.0,
    "oil_bpd": 1420.0,
    "gas_mcfd": 2150.0,
    "esp_hz": 56.5,
    "choke_pct": 42.0,
    "ssv_open": True,
}

def emit_card(note="Nominal"):
    status = "FLOWING (ESP ACTIVE)" if well["ssv_open"] else "SHUT-IN (SSV CLOSED)"
    card = {
        "type": "AdaptiveCard", "version": "1.5", "refreshIntervalMs": 2000,
        "body": [
            {"type": "TextBlock", "text": well["name"], "size": "large", "weight": "bolder"},
            {"type": "FactSet", "facts": [
                {"title": "Well Status",          "value": status},
                {"title": "Tubing / Casing Press", "value": f"{well['tubing_psi']:.0f} psi / {well['casing_psi']:.0f} psi"},
                {"title": "Est. Rates (24h EFM)", "value": f"{well['oil_bpd']:.0f} BOPD | {well['gas_mcfd']:.0f} MCFD"},
                {"title": "ESP Drive / Choke",    "value": f"{well['esp_hz']:.1f} Hz | Choke: {well['choke_pct']:.0f} %"},
                {"title": "Last Scan",            "value": f"{time.strftime('%H:%M:%S')} ({note})"}
            ]},
            {"type": "Input.Number", "id": "choke_pct", "title": "Target Choke Opening (%)", "value": well["choke_pct"]}
        ],
        "actions": [
            {"type": "Action.Execute", "title": "Apply Choke %", "verb": "set_choke"},
            {"type": "Action.Execute", "title": "Toggle SSV Shut-In", "verb": "toggle_ssv"}
        ]
    }
    print(json.dumps(card), flush=True)

emit_card("Initial poll")
for raw in sys.stdin:
    msg = json.loads(raw.strip() or "{}")
    verb, data = msg.get("verb", ""), msg.get("data", msg)
    if verb == "set_choke":
        well["choke_pct"] = max(0.0, min(100.0, float(data.get("choke_pct", well["choke_pct"]))))
    elif verb == "toggle_ssv":
        well["ssv_open"] = not well["ssv_open"]
    well["oil_bpd"] = (well["choke_pct"] * 33.8) if well["ssv_open"] else 0.0
    emit_card(verb or "Live RTU scan")
```

---

## 5. High-Value Oil & Gas Use Cases

| Segment | Operational Challenge | Rouen Mesh + Adaptive Process Advantage |
| :--- | :--- | :--- |
| **Upstream: Artificial Lift & Wellpads** | Hundreds of ESP, Gas Lift, and Rod Pump wells behind cellular CGNAT or Starlink. | Outbound-only `wss://` links every wellpad to production engineers with zero VPN tunnels or inbound ports. |
| **Midstream: Pipelines & Compressors** | Metered satellite links and API RP 1164 isolation across remote block valves and EFM skids. | Ultra-lightweight JSON-line telemetry and authenticated valve/setpoint control over persistent binary frames. |
| **Drilling & Completions (OFS)** | Aggregating WITS0/WITSML rig floor feeds, frac iron pressures, and live cameras across mobile spreads. | Combines Rouen's native **Number Series** and **Camera** cards with **Adaptive Process** WITS/Modbus bridges. |
| **Downstream: LNG & Refinery Analyzers** | Integrating gas chromatographs, H2S/LEL sensors, and vibration rigs without modifying core DCS logic. | Isolated child-process drivers provide side-channel engineering visibility with zero risk to safety systems. |
