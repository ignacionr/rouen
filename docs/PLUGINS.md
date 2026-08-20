# Plugin Guide

Rouen can load new card types and URI registrations from dynamically-linked
libraries (`.dll` on Windows; the mechanism is not Windows-specific, but the
sample plugin has so far only been built and verified on Windows). A plugin
registers one or more URI schemas with the card factory - exactly the same
registration a built-in card gets in
[factory.hpp](file:///Users/ignaciorodriguez/src/rouen/src/cards/interface/factory.hpp)
- except it happens at process startup from a library Rouen discovers on
disk, instead of at compile time.

This guide covers how loading works, the constraints that come with it, and
how to build your own plugin from the sample in `plugins/sample-card-plugin/`.

---

## Quick start

1. Build the sample plugin (Windows, Visual Studio 2022 generator):
   ```
   cd plugins/sample-card-plugin
   cmake -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release -- /m:2
   ```
2. Copy the resulting `build/bin/Release/hello_card_plugin.dll` into either:
   - `<directory containing rouen.exe>/plugins/`, or
   - `%APPDATA%/Rouen/plugins/` (created automatically on first run).
3. Start Rouen. Open the application menu - a new **Plugins** category
   appears with a **Hello Plugin** entry. Opening it, or hitting
   `POST /api/cards` with `{"uri": "hello:Ada"}`, creates the plugin's card.

If the plugin fails to load, Rouen keeps running; check the console output
(a debug build allocates one - see `docs/DEVELOPMENT.md`) for a `[plugin]`
prefixed line explaining why.

---

## What a plugin can register

Two things, both through the single `host_services::register_card` callback
handed to the plugin at load time:

- **A card type**: a subclass of `rouen::plugin::plugin_card` that draws
  itself with ImGui.
- **A URI schema**: the prefix before `:` in a card URI (e.g. `hello` in
  `hello:Ada`). Once registered, that schema works everywhere built-in
  schemas do - `POST /api/cards`, the `create_card` registrar service, and
  workspace-layout persistence (`rouen.ini`) - with no additional plugin
  code. Optionally, a display name adds the schema to the deck's
  **Plugins** launcher menu (see `menu.hpp`); leave it out for schemas the
  plugin only wants reachable by URI.

A single plugin can call `register_card` more than once to register several
related schemas.

---

## Why the plugin API looks the way it does

The natural instinct is to let a plugin `#include "card.hpp"` and subclass
`card` directly, the same way an in-tree card does. That does not work, for
reasons worth understanding before you write plugin code of your own:

1. **`registrar.hpp`'s state does not cross the DLL boundary.** It is a
   template with function-local statics (`getTypeMap<T>()`, `getMutex()`).
   Those are header-only, so each module (the host EXE, a plugin DLL) that
   instantiates `registrar::get<T>`/`registrar::add<T>` gets its *own*,
   separate copy of that static storage. A plugin calling
   `registrar::get<std::function<void()>>("signal_card_close_handled")`
   - which `card::run_focused_handlers()` does internally - would look in
   the plugin's own empty map, not the host's, and get a different (wrong,
   or missing) answer.
2. **Several `card` methods call into functions that only exist in the host
   binary.** `card_decorations::render_decorations`,
   `theme_manager::apply_theme_to_card`, and `card::register_mcp_functions`/
   `unregister_mcp_functions` are declared in headers but *defined* in
   `.cpp` files that are compiled only into `rouen.exe`. Since `card.hpp`'s
   methods that call them are inline, including `card.hpp` from a plugin
   would try to compile those call sites into the plugin DLL too - and the
   DLL linker has no way to resolve them (an EXE does not export symbols to
   other modules by default).

Both problems have real fixes (exporting selected host symbols via
`ENABLE_EXPORTS` and an import library; centralizing `registrar`'s storage
behind an exported accessor), but they add real complexity for a first
version. Instead, the plugin boundary is kept deliberately narrow:

- `plugin-sdk/rouen_plugin_api.hpp` has **no dependency on any Rouen
  internal header** - not `card.hpp`, not `registrar.hpp`, not
  `theme_manager.hpp`. The one exception is ImGui itself, because a plugin
  card's entire job is to draw with it.
- The host wraps every plugin card in `rouen::cards::plugin_card_adapter`
  (`src/cards/interface/plugin_card_adapter.hpp`), a real `card` subclass
  that lives entirely in host code. It forwards rendering, URI handling,
  and close notification to the plugin, and gets decorations, theming,
  focus handling, and MCP registration for free because *it* is compiled
  into the host, not the plugin.

If you are extending the plugin API itself (not just writing a plugin),
treat "does this header transitively pull in a Rouen internal header
besides ImGui?" as the check that the design still holds.

---

## The ImGui context problem (and why `render()` exists)

This is the one subtlety in the API worth understanding in full, because
getting it wrong does not fail loudly - it corrupts ImGui's internal state
and can crash the process, sometimes several frames after the code path
that actually caused it.

ImGui keeps its state (the current context, the ID stack, allocator
functions, ...) in a plain global variable inside `imgui.cpp`. When the
plugin DLL statically links its own copy of ImGui (as the sample plugin
does), **that global belongs to the plugin's module, separate from the
host's own copy of the same global**. Calling `ImGui::SetCurrentContext()`
from host code has no effect on the plugin's copy, and vice versa - this is
ImGui's own documented behavior for multi-module setups, not a Rouen
quirk.

A one-time `ImGui::SetCurrentContext(host_context)` call from the plugin at
load time (which `rouen_plugin_init` should still do, to have a valid
context before the first frame) is not enough on its own, because **Rouen
does not always render into the same context**. The `/api/cards/snapshot`
REST endpoint (`docs/USAGE.md`) creates a second, temporary `ImGuiContext`
to render an off-screen capture, points the *host's* ImGui state at it for
a couple of frames, and switches back. A plugin that only synchronized once
at load time would keep drawing into the stale, original context during
that window - while the host thinks it is capturing into the temporary
one. That mismatch corrupts the shared ID/window stack; it is exactly what
caused a hard crash during this feature's own development, reproduced by:
creating a plugin card, then requesting `POST /api/cards/snapshot`.

The fix lives in `rouen::plugin::plugin_card::render(ImGuiContext*)`:

```cpp
virtual void render(ImGuiContext* active_context) {
    ImGui::SetCurrentContext(active_context);
    draw();
}
```

This method is virtual with a default body, and **that body executes as
code compiled into the plugin DLL** - not because of anything special about
`render()` itself, but because C++ builds a derived class's vtable in
whichever module instantiates that class. Since `hello_card` (or any
plugin card) is only ever constructed inside the plugin, its vtable -
including the unoverridden `render` slot, pointing at this inline default
- is emitted by the plugin's own compiler invocation. The host's adapter
calls `impl_->render(ImGui::GetCurrentContext())` every frame; that
`ImGui::GetCurrentContext()` call executes in *host* code, so it correctly
reports whichever context the host is rendering into at that moment
(main or the temporary snapshot one) - and the virtual dispatch carries
that context into the plugin's own module-local `SetCurrentContext` call.

**The practical upshot for plugin authors: implement `draw()`, never
override `render()`.** The synchronization is not something you need to
think about beyond that.

---

## Requirements for a compatible plugin

A plugin **must** be built with:

- The **same ImGui version** as the Rouen build it targets - currently
  `v1.91.7`, pinned in both the repository root `CMakeLists.txt`
  (`FetchContent_Declare(imgui ...)`) and
  `plugins/sample-card-plugin/CMakeLists.txt`. `ImGuiContext`'s memory
  layout is only guaranteed identical when both sides are built from the
  same source.
- The **same compiler and C++ standard** (MSVC, C++23, matching the
  toolset Rouen itself is built with).
- **Dynamic CRT linkage** (`/MD` or `/MDd`, i.e.
  `MSVC_RUNTIME_LIBRARY "MultiThreaded[Debug]DLL"`), matching
  `cmake/windows.cmake`. This is what lets a `std::unique_ptr<plugin_card>`
  allocated inside the plugin be safely destroyed by host code (and vice
  versa) - both sides share the same CRT heap.

These constraints are why the SDK header explicitly warns about them rather
than trying to paper over them with a C-only ABI: a C-only boundary would
avoid the CRT/STL requirement, but at the cost of no `std::function`,
`std::string`, or `std::unique_ptr` in the plugin API - a much less
pleasant authoring experience for what is, in practice, an in-house
extension mechanism where controlling the toolchain on both sides is
realistic.

---

## Writing a plugin

### 1. Implement `rouen::plugin::plugin_card`

```cpp
#include <rouen_plugin_api.hpp>

class my_card final : public rouen::plugin::plugin_card {
public:
    explicit my_card(std::string_view locator) { handle_uri(locator); }

    void draw() override {
        ImGui::TextUnformatted("Hello from my plugin!");
    }

    std::string title() const override { return "My Card"; }
    std::string uri() const override { return "my-schema:" + locator_; }
    void handle_uri(std::string_view locator) override { locator_ = locator; }

private:
    std::string locator_;
};
```

Only `draw()`, `title()`, and `uri()` are required. `handle_uri()` and
`on_close()` have empty defaults.

### 2. Export `rouen_plugin_init`

```cpp
#include <rouen_plugin_api.hpp>
#include "my_card.hpp"

ROUEN_PLUGIN_EXPORT bool rouen_plugin_init(rouen::plugin::host_services const* services) {
    if (!services || services->abi_version != rouen::plugin::abi_version) {
        return false; // incompatible host, refuse to load
    }

    ImGui::SetCurrentContext(services->imgui_context);
    ImGui::SetAllocatorFunctions(services->imgui_alloc_func, services->imgui_free_func, services->imgui_alloc_user_data);

    services->register_card(
        "my-schema",
        [](std::string_view locator) -> std::unique_ptr<rouen::plugin::plugin_card> {
            return std::make_unique<my_card>(locator);
        },
        "My Card" // shown in the deck's "Plugins" menu; pass "" to omit
    );

    return true;
}
```

`ROUEN_PLUGIN_EXPORT` expands to `extern "C" __declspec(dllexport)` on
Windows (and the ELF/Mach-O visibility equivalent elsewhere), so the symbol
name `rouen_plugin_init` is exported unmangled - that fixed name is what
`plugin_host` looks up after loading the library.

### 3. Build it

See `plugins/sample-card-plugin/CMakeLists.txt` for a complete, working
example: it `FetchContent`s the pinned ImGui tag, compiles only ImGui's
core sources (a plugin never owns a window/renderer backend, so the
SDL3/GPU backend sources the host compiles are neither needed nor
linkable here), and links them into a `SHARED` library.

### 4. Install it

Drop the built library into one of the two directories `plugin_host`
scans at startup (`src/hosts/plugin_host.cpp`):

- `<directory containing rouen executable>/plugins/` - for libraries
  bundled with an installation.
- `%APPDATA%/Rouen/plugins/` on Windows (the platform user-data directory
  elsewhere) - created automatically on first run, for user-installed
  plugins.

Both are scanned once, at startup, before any card is created from a
persisted workspace layout or the command line - so a persisted URI whose
schema comes from a plugin resolves correctly on the next launch too.

---

## `host_services` reference

Passed to `rouen_plugin_init` as a `host_services const*`
(`plugin-sdk/rouen_plugin_api.hpp`):

| Field | Purpose |
|---|---|
| `abi_version` | Compared against `rouen::plugin::abi_version`; mismatches must fail loading. |
| `imgui_context` | The host's `ImGuiContext*`. Sync to it once at load (see above). |
| `imgui_alloc_func`, `imgui_free_func`, `imgui_alloc_user_data` | Pass straight to `ImGui::SetAllocatorFunctions` once at load. |
| `register_card(schema, factory, display_name)` | Registers a URI schema. `factory` is a `card_factory_fn` (`std::function<std::unique_ptr<plugin_card>(std::string_view)>`). Call as many times as you have schemas. |
| `log(message)` | Prints a line prefixed with the plugin's file name to the host's console output. |

---

## Limitations (v1)

- **Loaded plugins are never unloaded**, even at shutdown. This is
  intentional, not an oversight: the card factory's schema dictionary is a
  process-lifetime container that ends up holding `std::function` closures
  whose code lives inside the plugin library. Freeing the library before
  the process exits (or even during exit-time static destruction) would
  leave those closures pointing at unmapped memory. `dynamic_library.hpp`
  documents this and deliberately has no `unload()`.
- **No MCP function exposure yet.** `card::get_mcp_functions()` carries
  `std::function`/`std::string`-heavy types that would need more of the
  ABI surface than seemed worth it for a first version; a plugin card
  cannot currently expose MCP tools the way a built-in card can.
- **No hot reload.** A plugin is only discovered at startup.
- **Sample plugin verified on Windows only.** The ABI itself
  (`plugin-sdk/rouen_plugin_api.hpp`) and the loader
  (`src/helpers/dynamic_library.cpp`) both have Linux/macOS code paths
  (`dlopen`/`dlsym`, `.so`/`.dylib`), but only the Windows path has an
  accompanying sample and has been exercised end-to-end.

---

## Troubleshooting

- **"does not export rouen_plugin_init"**: the export macro didn't apply,
  or the function isn't `extern "C"`. Double check `ROUEN_PLUGIN_EXPORT`
  is on the function definition, not just a forward declaration.
  On Windows, `dumpbin /exports your_plugin.dll` should list
  `rouen_plugin_init` unmangled.
- **Plugin loads but the card renders garbage, or the app becomes unstable
  after opening the card**: almost always an ImGui version or ABI
  mismatch. Re-check the ImGui tag and CRT linkage against
  `plugins/sample-card-plugin/CMakeLists.txt`.
- **Plugin loads, registers, but never appears in the menu**: the
  `display_name` argument to `register_card` was empty. The schema still
  works by URI (`POST /api/cards`), it just won't be listed.
