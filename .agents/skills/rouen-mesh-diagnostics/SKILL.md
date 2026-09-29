---
name: rouen-mesh-diagnostics
description: Diagnostic runbook and step-by-step procedures for investigating Rouen Mesh WebSocket connectivity, pairing issues, Ed25519 challenge handshake failures, configuration desynchronization, and cloud relay daemon health.
---

# Rouen Mesh Diagnostics Runbook

Use this skill when Rouen is failing to connect to the mesh network, when the status reports `Disconnected: Connection closed`, when handshake verification fails (HTTP 401 Unauthorized), or when diagnosing peer discovery and virtual route tunneling.

---

## Architecture Overview

```
+-------------------------------------------------------------+
| Rouen Desktop Application (Client Node)                     |
|                                                             |
| - Config: $HOME/Applications/Rouen.app/Contents/Resources/  |
| - REST API: http://127.0.0.1:8081                           |
| - Ed25519 Identity: ROUEN_MESH_PUBLIC_KEY / PRIVATE_KEY     |
+------------------------------+------------------------------+
                               |
                   TLS 1.3 WSS Handshake
     GET /ws/connect?client_id=...&timestamp=...&signature=...
                               |
                               v
+-------------------------------------------------------------+
| Cloud Companion Cluster (rouen-service)                     |
| - Hosts: s1.inz.dev (2.56.241.245), s2.inz.dev (2.29.36.145) |
| - Domain: rouen.inz.dev:443                                 |
| - Paired Database: /etc/rouen-service/paired_keys.json       |
| - Policy: enforce_handshake = true                          |
+-------------------------------------------------------------+
```

1. **Client Identity**: Authentication uses pure Ed25519 asymmetric signatures. When connecting to `wss://rouen.inz.dev/ws/connect`, the client transmits:
   - `client_id`: registered node name (e.g., `rouen-ignacios-macbook-air`).
   - `timestamp`: current UTC Unix epoch in milliseconds.
   - `signature`: 128-char hex signature of `"{client_id}:{timestamp}"` signed by the device's private key.
2. **Server Verification**: `rouen-service` checks if `client_id` exists in `/etc/rouen-service/paired_keys.json` and verifies the signature using the registered public key.
3. **If Handshake Fails**: The server responds with `HTTP 401 Unauthorized` and severs the TCP connection.

---

## Quick Automated Diagnostic

Run the bundled diagnostic script to check process status, REST API status, configuration alignment, and remote server registrations:

```bash
.agents/skills/rouen-mesh-diagnostics/scripts/diagnose_mesh.sh
```

---

## Step-by-Step Diagnostic Procedures

### 1. Check Local Process and REST API Status

Rouen exposes an embedded REST server on port 8081 (`/openapi.json` provides the complete specification).

Query the current mesh state:
```bash
curl -s http://127.0.0.1:8081/api/mesh/status | python3 -m json.tool
```

Key fields to check:
- `connected`: `true` if WebSocket binary streaming session is active.
- `status_message`: Reveals disconnection reasons (e.g. `Disconnected: Connection closed` or `Unauthorized`).
- `client_id`: Check whether this matches the intended machine node ID.
- `public_key`: **Crucial.** Compare this with the public key registered on the cloud server.

Check open sockets on the running process:
```bash
lsof -n -P -p $(pgrep -f "Contents/MacOS/rouen") | grep -E "TCP|UDP"
```
*Note: If socket shows `SYN_SENT` or rapid socket churn on port 443, Rouen is trapped in a reconnect loop due to repeated handshake rejections.*

---

### 2. Inspect Bundle Configuration vs. Repository `.env`

**Common Pitfall (The Ephemeral Keypair Trap):**
When Rouen runs as a macOS bundle (`Rouen.app`), `ConfigService` loads `.env` from:
`$HOME/Applications/Rouen.app/Contents/Resources/.env`.

If `ROUEN_MESH_PRIVATE_KEY` or `ROUEN_MESH_PUBLIC_KEY` is missing or empty in that file:
```cpp
// src/hosts/rouen_mesh_host.cpp
if (config_.public_key.empty() || config_.private_key.empty()) {
    generate_keypair(config_.public_key, config_.private_key);
}
```
Rouen automatically generates a **new, ephemeral, in-memory keypair** on startup!
This ephemeral key will **never** match the registered public key on `rouen-service`, causing all connection attempts to fail with `401 Unauthorized`.

#### Verification:
Compare project repository `.env` against the deployed app bundle `.env`:
```bash
diff -u /Users/inz/src/rouen/.env "$HOME/Applications/Rouen.app/Contents/Resources/.env"
```

Verify these variables exist and are populated:
```env
ROUEN_MESH_CLIENT_ID=rouen-ignacios-macbook-air
ROUEN_MESH_PAIRED=1
ROUEN_MESH_SERVER_URL=wss://rouen.inz.dev/ws/connect
ROUEN_MESH_PUBLIC_KEY=<64_hex_chars>
ROUEN_MESH_PRIVATE_KEY=<64_hex_chars>
ROUEN_MESH_AUTH_MODE=challenge_preferred
```

---

### 3. Verify Cloud Service Health and Paired Keys

Connect to the companion cloud host (`s1.inz.dev` or `s2.inz.dev`):

1. **Check daemon status and recent handshake logs:**
   ```bash
   ssh root@s1.inz.dev "systemctl status rouen-service.service --no-pager"
   ```
   Look for log lines like:
   - `[Auth] Handshake challenge accepted for paired client '...'`
   - `[Auth] Invalid Ed25519 signature for client: ...`
   - `[Auth] Device is not paired with this service`

2. **Inspect registered paired public keys:**
   ```bash
   ssh root@s1.inz.dev "cat /etc/rouen-service/paired_keys.json"
   ```
   Confirm that your `client_id` exists in the JSON object and that its hex public key matches the `ROUEN_MESH_PUBLIC_KEY` in your `.env`.

---

### 4. Standalone Protocol Verification (Python Reference Client)

To verify the network, TLS certificate, and Ed25519 keypair independently of the C++ GUI process, use the zero-dependency verification script in `rouen-service`:

```bash
ROUEN_HOST="rouen.inz.dev" \
ROUEN_CLIENT_ID="rouen-ignacios-macbook-air" \
ROUEN_MESH_PRIVATE_KEY="<your_private_key_hex>" \
python3 ../rouen-service/scripts/test_client.py
```

Expected output on success:
```text
[+] WebSocket Handshake OK! Successfully connected to rouen-service as '...'.
[+] Received HEARTBEAT_PONG from rouen-service!
[+] REGISTRY_SET Reply: {"status":"ok",...}
[+] CLIENT_LIST_RESP Reply: [...]
```

---

## Remediation Workflows

### Scenario A: Bundle `.env` Lost Keys During Build/Export
If `/Users/inz/src/rouen/.env` has the valid paired keys but `$HOME/Applications/Rouen.app/Contents/Resources/.env` has empty values:

1. Copy keys into the bundle configuration:
   ```bash
   cp /Users/inz/src/rouen/.env "$HOME/Applications/Rouen.app/Contents/Resources/.env"
   ```
2. Trigger reconnect through the local REST API without restarting Rouen:
   ```bash
   curl -s -X POST http://127.0.0.1:8081/api/mesh/disconnect
   # If needing full reload of keys, restart Rouen:
   pkill -f "Contents/MacOS/rouen"
   open "$HOME/Applications/Rouen.app"
   ```

### Scenario B: Device Needs New Pairing
If a device has never been paired or its identity was reset:

1. Generate a 6-digit one-time pairing code from the server admin interface or API:
   ```bash
   ssh root@s1.inz.dev "curl -s -X POST http://127.0.0.1:80/admin/api/pairing-code?token=rouen-cluster-secret-inz"
   ```
2. Submit pairing code to Rouen's REST API:
   ```bash
   curl -s -X POST http://127.0.0.1:8081/api/mesh/pair \
     -H "Content-Type: application/json" \
     -d '{"pairing_code":"<6_DIGIT_CODE>","server_url":"wss://rouen.inz.dev/ws/connect"}'
   ```
3. Once paired, immediately synchronize the generated keys from `$HOME/Applications/Rouen.app/Contents/Resources/.env` back to `/Users/inz/src/rouen/.env` so future re-deployments preserve them.
