# Sample Plugin: Hello Card

A minimal, working example of a Rouen plugin - see
[docs/PLUGINS.md](../../docs/PLUGINS.md) for the full guide this project
accompanies. It registers two URI schemas, demonstrating both plugin
styles from a single library:

- `hello` - an ImGui-drawn `plugin_card` that greets a name typed into a
  text box.
- `hello-adaptive` - a declarative `adaptive_card_plugin`: the exact same
  kind of greeting, but defined as an Adaptive Card JSON template with no
  ImGui code at all.

## Build (Windows, Visual Studio 2022)

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release -- /m:2
```

This produces `build/bin/Release/hello_card_plugin.dll`.

## Install

Copy the DLL into either:

- `<directory containing rouen.exe>/plugins/`, or
- `%APPDATA%/Rouen/plugins/`

Then start Rouen. The application menu gets a new **Plugins** category
with **Hello Plugin** and **Hello Adaptive Card** entries. Both also work
as card URIs via `POST /api/cards` - `hello:<name>` and `hello-adaptive`.

## Layout

- `src/hello_card.hpp` - the ImGui-drawn card, implementing
  `rouen::plugin::plugin_card`.
- `src/hello_adaptive_card.hpp` - the declarative card, implementing
  `rouen::plugin::adaptive_card_plugin`.
- `src/plugin_entry.cpp` - the exported `rouen_plugin_init` entry point
  that synchronizes ImGui state (needed only for the `plugin_card` style)
  and calls `register_card` and `register_adaptive_card`.
- `CMakeLists.txt` - a standalone build (not part of Rouen's own CMake
  tree - a real plugin is built and versioned independently of the host
  application it targets).
