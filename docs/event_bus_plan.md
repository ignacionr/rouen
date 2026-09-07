# Rouen Event Bus Architectural Plan

This document outlines the architectural specification and implementation roadmap for incorporating a thread-safe, decoupled **Event Bus** into Rouen.

---

## 1. Objectives & Architectural Principles

1. **Decoupled Inter-Card & Host Communication**:
   Cards and backend hosts can publish state changes or completion triggers without holding direct static references or raw pointers to other cards.
2. **Glaze JSON Payload Standard**:
   In alignment with Rouen's data architecture, all event payloads are represented using **Glaze JSON** (`glz::json_t` / `glz::raw_json`), ensuring fast, zero-copy serialization and native struct bindings.
3. **Thread-Safe Queue & Main-Thread Frame Dispatch**:
   Background hosts (Process Host, RSS, Weather, Bybit, Git Sync) publish events asynchronously off the main thread. The Event Bus buffers events in a thread-safe queue and dispatches them on the main thread during `deck::render()`, avoiding UI state race conditions.
4. **RAII Subscription Lifecycle**:
   Cards receive lightweight subscription handles (`event_subscription_handle`) that automatically unregister listeners when a card closes (`card::on_close()`).

---

## 2. Topic Taxonomy & Event Schema

### Event Data Structure (`src/hosts/event_bus_host.hpp`)

```cpp
#include <chrono>
#include <string>
#include <glaze/json/json_t.hpp>

namespace rouen::events {

struct rouen_event {
    std::string topic;                                    // Hierarchical topic string
    std::string source_id;                                // URI or identifier of sender
    glz::json_t payload{};                                // Glaze JSON payload object
    std::chrono::system_clock::time_point timestamp{      // Event creation timestamp
        std::chrono::system_clock::now()
    };
};

} // namespace rouen::events
```

### Topic Hierarchy Standard

Topics follow a colon-delimited namespace convention:

| Topic Category | Format Pattern | Examples |
|---|---|---|
| **Card Events** | `card:<card_type>:<event>` | `card:pomodoro:finished`, `card:calendar:created`, `card:notes:saved` |
| **Host Events** | `host:<host_name>:<event>` | `host:process:exited`, `host:rss:feed_updated`, `host:bybit:alert`, `host:git:synced` |
| **System Events** | `system:<event>` | `system:card_opened`, `system:card_closed`, `system:deck_scrolled`, `system:theme_changed` |

---

## 3. Event Bus Host Specification (`event_bus_host`)

Following Rouen's architecture guide, the Event Bus is implemented as a **Stateful Host** under `src/hosts/event_bus_host.hpp`.

```
                    ┌──────────────────────────────────────────────┐
                    │               event_bus_host                 │
                    │         (Stateful Event Host Manager)        │
                    └──────────────────────┬───────────────────────┘
                                           │
         ┌─────────────────────────────────┼─────────────────────────────────┐
         ▼                                 ▼                                 ▼
┌──────────────────┐             ┌──────────────────┐             ┌──────────────────┐
│   Background     │             │    C++ Cards     │             │ QuickJS / Script │
│  Threads / Hosts │             │  (Publishing)    │             │  (Publishing)    │
└────────┬─────────┘             └────────┬─────────┘             └────────┬─────────┘
         │                                │                                │
         └────────────────────────────────┼────────────────────────────────┘
                                          │ publish(evt)
                                          ▼
                         ┌──────────────────────────────────┐
                         │   Thread-Safe Pending Queue      │
                         │   (std::mutex + std::vector)     │
                         └────────────────┬─────────────────┘
                                          │
                                          │ dispatch_pending_events()
                                          │ (Called inside deck::render())
                                          ▼
                         ┌──────────────────────────────────┐
                         │    Main Thread Subscriber Loop   │
                         └────────────────┬─────────────────┘
                                          │
         ┌────────────────────────────────┼────────────────────────────────┐
         ▼                                ▼                                ▼
┌──────────────────┐             ┌──────────────────┐             ┌──────────────────┐
│ C++ Card Handlers│             │ QuickJS (`Rouen.on`)           │ HTTP SSE Stream  │
└──────────────────┘             └──────────────────┘             └──────────────────┘
```

### Key Class Interface

```cpp
namespace rouen::hosts {

using subscription_id = uint64_t;
using event_callback = std::function<void(const events::rouen_event&)>;

class event_bus_host {
public:
    static event_bus_host& instance();

    // Publish an event (thread-safe, non-blocking)
    void publish(events::rouen_event evt);

    // Subscribe to a topic pattern (supports exact topics or wildcards like "card:pomodoro:*")
    subscription_id subscribe(std::string_view topic_pattern, event_callback cb);

    // Unsubscribe handle
    void unsubscribe(subscription_id id);

    // Called during deck::render() on the main UI thread to dispatch queued events
    void dispatch_pending_events();

private:
    event_bus_host() = default;
    
    std::mutex queue_mutex_;
    std::vector<events::rouen_event> pending_events_;
    
    std::shared_mutex sub_mutex_;
    struct subscriber_entry {
        subscription_id id;
        std::string pattern;
        event_callback callback;
    };
    std::vector<subscriber_entry> subscribers_;
    std::atomic<subscription_id> next_sub_id_{1};
};

} // namespace rouen::hosts
```

---

## 4. Concrete C++ Code Examples

### Example A: Publishing Events from a Card (Pomodoro Timer)

```cpp
// Inside src/cards/productivity/pomodoro.cpp
#include "hosts/event_bus_host.hpp"
#include <glaze/glaze.hpp>

void pomodoro_card::on_timer_complete() {
    // Construct structured Glaze JSON payload
    glz::json_t payload{};
    payload["session_type"] = "work_interval";
    payload["duration_minutes"] = 25;
    payload["task_label"] = current_task_name_;
    payload["completed_at"] = get_iso_timestamp();

    // Publish event asynchronously onto the Event Bus
    rouen::hosts::event_bus_host::instance().publish({
        .topic = "card:pomodoro:finished",
        .source_id = get_uri(),
        .payload = std::move(payload)
    });
}
```

### Example B: Publishing Events from a Background Host (Process Host Crash Monitor)

```cpp
// Inside src/hosts/process_host.cpp (Runs on a background monitoring thread)
#include "hosts/event_bus_host.hpp"
#include <glaze/glaze.hpp>

void process_host::handle_process_exit(const process_handle& proc, int exit_code, const std::string& stderr_log) {
    glz::json_t payload{};
    payload["process_id"] = proc.id;
    payload["command_line"] = proc.cmd;
    payload["exit_code"] = exit_code;
    payload["stderr_snippet"] = stderr_log;

    // Thread-safe publish from background thread to Event Bus queue
    rouen::hosts::event_bus_host::instance().publish({
        .topic = (exit_code == 0) ? "host:process:completed" : "host:process:exited",
        .source_id = "host:process_orchestration",
        .payload = std::move(payload)
    });
}
```

### Example C: Subscribing to Events in a Card (Alarm Card)

```cpp
// Inside src/cards/productivity/alarm_card.hpp
#include "hosts/event_bus_host.hpp"

class alarm_card : public card {
private:
    rouen::hosts::subscription_id pomodoro_sub_id_{0};

public:
    alarm_card() {
        // Subscribe to Pomodoro completion events
        pomodoro_sub_id_ = rouen::hosts::event_bus_host::instance().subscribe(
            "card:pomodoro:finished",
            [this](const rouen::events::rouen_event& evt) {
                // Safely executed on the main UI thread during deck::render()
                std::string task_name = "Session";
                if (evt.payload.contains("task_label") && evt.payload["task_label"].is_string()) {
                    task_name = evt.payload["task_label"].get<std::string>();
                }
                this->trigger_overlay_alarm(std::format("Pomodoro Completed: {}", task_name));
            }
        );
    }

    void on_close() override {
        // Clean up subscription upon card close
        if (pomodoro_sub_id_ > 0) {
            rouen::hosts::event_bus_host::instance().unsubscribe(pomodoro_sub_id_);
            pomodoro_sub_id_ = 0;
        }
    }
};
```

### Example D: Wildcard Topic Subscription (System Logger)

```cpp
// Subscribing to all host events (e.g. host:process:*, host:rss:*, host:bybit:*)
auto logger_sub_id = rouen::hosts::event_bus_host::instance().subscribe(
    "host:*",
    [](const rouen::events::rouen_event& evt) {
        std::string json_str;
        glz::write_json(evt.payload, json_str);
        std::cout << std::format("[EVENT LOG] Topic: {} | Source: {} | Payload: {}\n", 
                                 evt.topic, evt.source_id, json_str);
    }
);
```

---

## 5. HTTP REST API Event Streaming (Server-Sent Events)

The Event Bus will also bridge into `api_server_host`:
* **Endpoint**: `GET /api/events/stream`
* **Protocol**: Server-Sent Events (SSE) over Mongoose HTTP.
* **Payload**: Formatted Glaze JSON strings pushed to connected HTTP clients.

### Python External Client Example

```python
# External script listening to live Rouen events over HTTP SSE
import requests
import json

response = requests.get('http://127.0.0.1:8081/api/events/stream', stream=True)
for line in response.iter_lines():
    if line.startswith(b'data: '):
        event_data = json.loads(line[6:].decode('utf-8'))
        print(f"[{event_data['timestamp']}] Topic: {event_data['topic']} -> {event_data['payload']}")
```

---

## 6. Implementation Roadmap

### Phase 1: Core Host Infrastructure
* Implement `src/hosts/event_bus_host.hpp` and `src/hosts/event_bus_host.cpp`.
* Define `rouen_event` struct with `glz::json_t` payload integration.
* Add unit tests for subscription matching and multi-threaded event queuing.

### Phase 2: Frame Dispatch & Deck Hooks
* Add `event_bus_host::instance().dispatch_pending_events()` call to `deck::render()` in `src/cards/interface/deck.hpp`.
* Add RAII subscription helper wrapper `scoped_event_subscription` for cleaner card integration.

### Phase 3: Core Card & Host Instrumentation
* Instrument **Pomodoro Card**: Emit `card:pomodoro:finished`.
* Instrument **Process Host**: Emit `host:process:exited` and `host:process:started`.
* Instrument **RSS Host**: Emit `host:rss:feed_updated`.
* Instrument **Deck**: Emit `system:card_opened` and `system:card_closed`.

### Phase 4: HTTP SSE Server Integration
* Expose `GET /api/events/stream` route in `api_server_host.cpp` to stream live Glaze JSON events to external consumers.
