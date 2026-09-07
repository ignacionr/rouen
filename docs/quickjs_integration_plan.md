# QuickJS Integration & In-Process User Extensions Plan

This document details the design and implementation plan for embedding **QuickJS** into Rouen to provide fast, lightweight, in-process JavaScript user extensions and card automation.

---

## 1. Objectives & Strategic Rationale

1. **In-Process Automation Engine**:
   Provide Rouen with a sub-millisecond, embeddable JavaScript runtime (QuickJS by Fabrice Bellard) to automate card creation, workspace layouts, data transformations, and event-driven triggers without external network or IPC overhead.
2. **Zero Host Dependencies**:
   Unlike `adaptive-process:` cards which require system-installed interpreters (Python, Node.js), QuickJS compiles directly into Rouen's binary (~250 KB added size). Users get instant scripting on clean macOS or Windows installations.
3. **Glaze JSON Interoperability**:
   Seamlessly translate between Rouen's C++ Glaze structures (`glz::json_t` / JSON strings) and QuickJS objects (`JSValue`) via fast JSON parsing/stringifying primitives (`JS_ParseJSON` and `JS_JSONStringify`).
4. **First-Class Event Bus & Plugin Coupling**:
   Expose Rouen's Event Bus directly to JavaScript scripts (`Rouen.on(...)`, `Rouen.emit(...)`), and ensure dynamic DLL plugins (`.dll`/`.dylib` plugins loaded from disk) are fully accessible to JavaScript scripts without extra glue code.

---

## 2. QuickJS Engine Embedding Specification

QuickJS will be included as a clean, lightweight submodule/FetchContent under `external/quickjs/` or compiled directly via CMake (`quickjs.c`, `libunicode.c`, `cutils.c`, `libregexp.c`).

### CMake Integration (`CMakeLists.txt`)

```cmake
# QuickJS C Engine Integration
set(QUICKJS_SOURCES
    external/quickjs/quickjs.c
    external/quickjs/libunicode.c
    external/quickjs/cutils.c
    external/quickjs/libregexp.c
)
add_library(quickjs STATIC ${QUICKJS_SOURCES})
target_include_directories(quickjs PUBLIC external/quickjs)
```

---

## 3. Host Manager Specification (`quickjs_host`)

Following Rouen's architecture guide, the engine is wrapped in a **Stateful Host** (`src/hosts/quickjs_host.hpp`).

```
┌────────────────────────────────────────────────────────────────────────┐
│                             quickjs_host                               │
│                 (Manages JSRuntime* & JSContext*)                      │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
       ┌────────────────────────────┼────────────────────────────┐
       ▼                            ▼                            ▼
┌──────────────┐             ┌──────────────┐             ┌──────────────┐
│ Rouen Global │             │ Glaze Bridge │             │ Execution    │
│  Namespace   │             │ (C++ ↔ JS)   │             │ Timeout Guard│
└──────┬───────┘             └──────┬───────┘             └──────┬───────┘
       │                            │                            │
       ▼                            ▼                            ▼
┌────────────────────────────────────────────────────────────────────────┐
│                         QuickJS JSContext                              │
│  Rouen.cards.*  │ Rouen.on() │ Rouen.emit() │ Rouen.plugins │ Rouen.fetch() │
└────────────────────────────────────────────────────────────────────────┘
```

### Key Class Interface

```cpp
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <glaze/json/json_t.hpp>
#include "quickjs.h"

namespace rouen::hosts {

class quickjs_host {
public:
    static quickjs_host& instance();

    bool initialize();
    void shutdown();

    // Evaluate JavaScript string, returning result as stringified Glaze JSON
    std::string eval_script(std::string_view js_code, const char* filename = "script.js");

    // Invoke a named function inside context with Glaze JSON arguments
    std::string call_function(const std::string& func_name, const glz::json_t& args);

    // Main thread tick processing for pending JS jobs/microtasks
    void execute_pending_jobs();

private:
    quickjs_host() = default;
    ~quickjs_host();

    JSRuntime* rt_{nullptr};
    JSContext* ctx_{nullptr};

    void bind_rouen_namespace();
    static JSValue js_rouen_create_card(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_rouen_emit_event(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_rouen_on_event(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_rouen_call_plugin(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue js_rouen_fetch(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
};

} // namespace rouen::hosts
```

---

## 4. Glaze JSON $\leftrightarrow$ QuickJS Interoperability Bridge

To ensure zero friction when exchanging data between C++ structs and JavaScript objects:

1. **C++ to QuickJS (`glz::json_t` $\rightarrow$ `JSValue`)**:
   ```cpp
   JSValue glaze_to_jsvalue(JSContext* ctx, const glz::json_t& json_obj) {
       std::string json_str;
       glz::write_json(json_obj, json_str);
       return JS_ParseJSON(ctx, json_str.c_str(), json_str.length(), "<glaze>");
   }
   ```

2. **QuickJS to C++ (`JSValue` $\rightarrow$ `glz::json_t`)**:
   ```cpp
   glz::json_t jsvalue_to_glaze(JSContext* ctx, JSValue val) {
       JSValue json_str_val = JS_JSONStringify(ctx, val, JS_UNDEFINED, JS_UNDEFINED);
       const char* str = JS_ToCString(ctx, json_str_val);
       glz::json_t result{};
       if (str) {
           glz::read_json(result, str);
           JS_FreeCString(ctx, str);
       }
       JS_FreeValue(ctx, json_str_val);
       return result;
   }
   ```

---

## 5. Dynamic DLL Plugin Interoperability & Script Accessibility

Dynamic C++ DLL plugins (loaded via `plugin-sdk/rouen_plugin_api.hpp`) must play seamlessly with QuickJS scripts:

### 1. Transparent URI Schema Invocation
Plugins register URI schemas dynamically (e.g. `hello:Ada` or `jira:PROJ-123`). Because QuickJS routes card creation through Rouen's unified Card Factory, **scripts instantiate dynamic plugin cards identically to built-in cards**:
```javascript
// Instant script access to dynamically loaded DLL plugin cards
Rouen.cards.create("hello:Ada");
Rouen.cards.create("jira:PROJ-123");
```

### 2. Plugin Function Call Interface (`Rouen.plugins`)
QuickJS scripts can directly query registered dynamic plugins and invoke their exposed functions/actions:
```javascript
// Query active dynamic DLL plugins loaded from disk
const plugins = Rouen.plugins.list(); 
// Returns: [{ id: "hello_plugin", schemas: ["hello", "hello-adaptive"] }]

// Direct execution of dynamic DLL plugin functions with Glaze JSON params
const result = await Rouen.plugins.call("hello_plugin", "say_hello", { name: "QuickJS" });
Rouen.log("Plugin response: " + result.greeting);
```

### 3. Bidirectional Plugin Event Bus Coupling
The plugin C++ SDK (`host_services` struct in `rouen_plugin_api.hpp`) is updated with thread-safe Event Bus hooks:
```cpp
// In rouen_plugin_api.hpp host_services struct:
void (*publish_event)(const char* topic, const char* json_payload);
uint64_t (*subscribe_event)(const char* topic_pattern, void (*callback)(const char* topic, const char* json_payload, void* user_data), void* user_data);
void (*unsubscribe_event)(uint64_t sub_id);
```

This allows C++ DLL plugins to emit events that JavaScript scripts react to, and vice-versa:

```
┌───────────────────────────┐                    ┌───────────────────────────┐
│     C++ DLL Plugin        │                    │   QuickJS Script Macro    │
│  (`hello_card_plugin.dll`)│                    │  (`dev_setup.js`)         │
└─────────────┬─────────────┘                    └─────────────▲─────────────┘
              │                                                │
              │ publish_event("card:hello:greeted", ...)       │ Rouen.on("card:hello:greeted")
              ▼                                                │
┌──────────────────────────────────────────────────────────────┴────────────┐
│                             Rouen Event Bus                               │
│                         (src/hosts/event_bus_host)                        │
└───────────────────────────────────────────────────────────────────────────┘
```

---

## 6. Concrete JavaScript Scripting Examples

### Example A: Interacting with Dynamic DLL Plugin Cards (`scripts/macros/plugin_automation.js`)

```javascript
// scripts/macros/plugin_automation.js

Rouen.log("[Macro] Plugin Automation script started");

// 1. Launch a dynamic DLL plugin card registered by hello_card_plugin.dll
Rouen.cards.create("hello:Ada");

// 2. React when the dynamic DLL plugin card fires an event
Rouen.on("card:hello:greeted", (evt) => {
    Rouen.log(`[Plugin Event] Hello card greeted user: ${evt.payload.name}`);
    
    // Automatically trigger Jira plugin card
    Rouen.cards.create("jira:RECENT", { title: "Follow up with " + evt.payload.name });
});
```

### Example B: Reactive Process Crash Handler (`scripts/macros/process_auto_debugger.js`)

```javascript
// scripts/macros/process_auto_debugger.js

Rouen.log("[QuickJS Macro] Process Crash Debugger initialized");

Rouen.on("host:process:exited", (evt) => {
    const payload = evt.payload;
    
    if (payload.exit_code !== 0) {
        Rouen.log(`[ALERT] Process ${payload.command_line} exited with code ${payload.exit_code}`);
        
        const promptText = `Process '${payload.command_line}' crashed with exit code ${payload.exit_code}.\n` +
                           `Captured stderr log:\n\`\`\`\n${payload.stderr_snippet}\n\`\`\`\n` +
                           `Please analyze the crash reason and suggest a fix.`;
        
        Rouen.cards.create("ai-chat", { prompt: promptText });
        Rouen.deck.scrollToSection(1);
    }
});
```

### Example C: Dynamic JavaScript Adaptive Card (`scripts/user_cards/weather_card.js`)

```javascript
// scripts/user_cards/weather_card.js

let currentCity = "Rouen";
let currentTemp = "18°C";
let weatherCondition = "Partly Cloudy";

function onRender() {
    return {
        type: "AdaptiveCard",
        version: "1.5",
        body: [
            {
                type: "TextBlock",
                text: `☀️ Weather in ${currentCity}`,
                size: "Large",
                weight: "Bolder"
            },
            {
                type: "FactSet",
                facts: [
                    { title: "Temperature:", value: currentTemp },
                    { title: "Condition:", value: weatherCondition }
                ]
            },
            {
                type: "Input.Text",
                id: "new_city",
                placeholder: "Enter new city (e.g. Paris, London)"
            }
        ],
        actions: [
            {
                type: "Action.Submit",
                title: "Update Weather",
                data: { action: "refresh" }
            }
        ]
    };
}

async function onSubmit(formData) {
    if (formData.new_city) {
        currentCity = formData.new_city;
        
        try {
            const res = await Rouen.fetch(`https://wttr.in/${encodeURIComponent(currentCity)}?format=j1`);
            const data = await res.json();
            currentTemp = data.current_condition[0].temp_C + "°C";
            weatherCondition = data.current_condition[0].weatherDesc[0].value;
        } catch (e) {
            weatherCondition = "Error fetching data";
        }
    }
    return onRender();
}
```

---

## 7. Execution Safety & Timeout Guards

To protect Rouen's ImGui main loop from infinite loops in user scripts:
* **Interrupt Handler (`JS_SetInterruptHandler`)**:
  Counts execution duration and interrupts any JS script exceeding **3 seconds** of continuous CPU execution.
* **Memory Limits (`JS_SetMemoryLimit`)**:
  Cap QuickJS heap size to **32 MB** per context to prevent runaway memory usage.

---

## 8. Implementation Roadmap

### Phase 1: Engine Build Integration
* Add QuickJS C source files under `external/quickjs/`.
* Configure CMake target `quickjs` in `CMakeLists.txt`.

### Phase 2: Host Implementation & Glaze Bridge
* Create `src/hosts/quickjs_host.hpp` and `.cpp`.
* Implement `glaze_to_jsvalue` and `jsvalue_to_glaze` marshaling primitives.
* Implement CPU timeout and memory limit guards.

### Phase 3: Global Namespace Bindings & Plugin Integration
* Bind `Rouen.cards.*`, `Rouen.deck.*`, `Rouen.on()`, `Rouen.emit()`, `Rouen.plugins.*`, and `Rouen.log()`.
* Add `publish_event` / `subscribe_event` function pointers to `plugin-sdk/rouen_plugin_api.hpp`.
* Connect `Rouen.on` and `Rouen.emit` to `event_bus_host`.

### Phase 4: Dynamic JS Card Registration (`js_card`)
* Register `js:` URI handler in card `factory.cpp`.
* Create `js_card` adapter rendering Adaptive Card JSON returned from QuickJS scripts.

### Phase 5: Example User Scripts & Documentation
* Add sample user scripts under `scripts/user_cards/`.
* Update [docs/PLUGINS.md](PLUGINS.md) and [docs/USAGE.md](USAGE.md).
