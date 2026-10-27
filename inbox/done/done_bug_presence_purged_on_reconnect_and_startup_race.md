# Bug: Ephemeral Presence Records Purged on Reconnect and Lost at Startup

## Summary

Nodes on mobile or intermittent connections (such as macOS laptops on Wi-Fi / sleep-wake cycles) stop advertising presence across the Rouen Mesh, while always-on Windows workstations continue to report presence indefinitely.

Live mesh tracing confirmed that:
1. `presence/{client_id}` is stored in `rouen-service` as an **ephemeral** registry entry.
2. When a node temporarily disconnects (e.g. lid closed, sleep, network switch), the server purges all ephemeral keys for that client (`purge_client_entries`).
3. Upon reconnection, `rouen_mesh_host::on_ws_connected()` re-registers local services (`publish_local_services()`), but **fails to re-publish presence**.
4. In [`presence_service.hpp`](file:///Users/inz/src/rouen/src/helpers/presence_service.hpp), the idle worker only publishes if `!is_idle_reported_ && is_locally_idle()`. Once marked idle (`is_idle_reported_ == true`), it never publishes again unless a new local UI event triggers `record_interaction()`.
5. Additionally, the initial publication in `presence_service::start()` occurs *before* the WebSocket connection is established, silently dropping the `"startup"` presence frame.

---

## Affected Code

### 1. `on_ws_connected` does not re-publish presence

In [`src/hosts/rouen_mesh_host.cpp`](file:///Users/inz/src/rouen/src/hosts/rouen_mesh_host.cpp#L352-L358):

```cpp
    // Self-healing: verify all active mapped route listeners are listening
    verify_and_restore_listeners();

    // Publish own local services and request fresh online clients and peer services
    publish_local_services();
    refresh_connected_clients();
    refresh_peer_services();
    refresh_server_routes();
    refresh_registry();
```

`presence_service::instance().publish_presence("reconnect", true)` is never called here.

### 2. Startup race condition drops initial presence frame

In [`src/rouen.cpp`](file:///Users/inz/src/rouen/src/rouen.cpp#L549-L562):

```cpp
    // Initialize the presence service and register in service registrar
    rouen::services::presence_service::instance().start();
    ...
    if (auto_connect_mesh) {
        ...
        mesh_host.start();
```

When `presence_service::start()` executes:
```cpp
    publish_presence("startup", true);
```
`send_frame_over_ws()` is called, but `active_ws_conn_` is still `nullptr`. The frame is silently dropped (`if (!active_ws_conn_) return;`).

### 3. Server-side ephemeral purge on client disconnect

In [`rouen-service/src/server.cpp`](file:///Users/inz/src/rouen-service/src/server.cpp#L1244):

```cpp
    size_t purged_reg = registry_mgr_->purge_client_entries(client_id);
```

When a client drops TCP/WebSocket connection (e.g. Mac sleeping or roaming), all ephemeral keys (`presence/{client_id}` and `presence/last_active`) are immediately expunged from the server registry.

### 4. Absence of OS-level input hook

In [`src/main_wnd_loop.cpp`](file:///Users/inz/src/rouen/src/main_wnd_loop.cpp#L532-L552):

Interactions are only recorded when SDL3 receives events directed at Rouen's windows. Working in other macOS applications does not trigger `record_interaction()`. Because Windows workstations like `ir-01` and `ir-02` stay connected continuously without sleep, their initial 5-minute `idle_transition` remains permanently registered on the server, whereas macOS loses its entry on the first sleep/disconnect cycle.

---

## Steps to Reproduce

1. Start Rouen on macOS (`rouen-ignacios-macbook-air`) and allow it to run for >5 minutes until it enters idle state (`is_idle_reported_ == true`).
2. Close laptop lid or toggle Wi-Fi to force a WebSocket disconnect/reconnect cycle.
3. Query the mesh presence from a remote node (e.g., `curl http://localhost:18084/api/presence` on `lt-ir-01` or `s2.inz.dev`).
4. Inspect the returned `presences` list.

### Expected Behavior
`rouen-ignacios-macbook-air` should appear in the mesh presence list with status `"idle"` or `"active"`.

### Actual Behavior
`rouen-ignacios-macbook-air` is completely missing from the mesh registry on remote nodes and on the mesh server. (Local queries on the Mac mask the bug because [`get_all_presences()`](file:///Users/inz/src/rouen/src/helpers/presence_service.hpp#L366-L368) synthesizes `get_local_presence()` if missing locally).

---

## Suggested Fix

1. In [`src/hosts/rouen_mesh_host.cpp`](file:///Users/inz/src/rouen/src/hosts/rouen_mesh_host.cpp#L353), invoke `rouen::services::presence_service::instance().publish_presence("connect", true);` inside `on_ws_connected()`.
2. In [`src/helpers/presence_service.hpp`](file:///Users/inz/src/rouen/src/helpers/presence_service.hpp), periodically re-publish presence during registry refresh (e.g. heartbeat or every 30–60s) if the client has not seen a recent publish, preventing desynchronization across server restarts or cluster failovers.
