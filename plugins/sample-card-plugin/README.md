# Sample Plugin: Hello Card

A minimal, working example of a Rouen plugin - see
[docs/PLUGINS.md](../../docs/PLUGINS.md) for the full guide this project
accompanies. It registers one URI schema, `hello`, backed by a card that
greets a name typed into a text box.

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
with a **Hello Plugin** entry, and `hello:<name>` works as a card URI
(e.g. via `POST /api/cards`).

## Layout

- `src/hello_card.hpp` - the card itself, implementing
  `rouen::plugin::plugin_card`.
- `src/plugin_entry.cpp` - the exported `rouen_plugin_init` entry point
  that synchronizes ImGui state and calls `register_card`.
- `CMakeLists.txt` - a standalone build (not part of Rouen's own CMake
  tree - a real plugin is built and versioned independently of the host
  application it targets).
