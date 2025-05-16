# Rouen Helpers

This directory contains various helper classes and utilities used throughout the Rouen application.

## Available Helpers

| Helper | Description |
|--------|-------------|
| `api_keys.hpp` | Manages API keys for various services |
| `capture_helper.hpp` | Assists with capturing and processing input |
| `cppgpt.hpp` | Integration with GPT APIs |
| `date_picker.hpp` | UI helper for date selection |
| `debug.hpp` | Debugging utilities and logging |
| `deferred_operations.hpp` | Manages operations to be executed later |
| `email_metadata_analyzer.hpp` | Analyzes and processes email metadata |
| `fetch.hpp` | HTTP client for making API requests (built on libcurl) |
| `image_cache.hpp` | Caches and manages images |
| `imgui_include.hpp` | Wrapper for ImGui headers with warning suppression |
| `imgui_helper.hpp` | Utilities for working with ImGui |
| `media_player.hpp` | Interface for media playback (includes play_sound_once for simple sound effects) |
| `mpv_socket.hpp` | Socket-based communication with MPV media player |
| `notify_service.hpp` | Notification service |
| `platform_utils.hpp` | Platform-specific utilities |
| `process_helper.hpp` | Utilities for managing processes |
| `sqlite.hpp` | SQLite database wrapper |
| `sqlite_keyvalue.hpp` | Key-value storage using SQLite |
| `string_helper.hpp` | String manipulation utilities |
| `texture_helper.hpp` | Texture handling for the UI |

## Using the fetch Helper

The `fetch` helper provides HTTP functionality through libcurl:

```cpp
// Basic GET request
http::fetch fetcher;
std::string response = fetcher("https://api.example.com/endpoint");

// POST request with headers
std::string response = fetcher.post(
    "https://api.example.com/endpoint",
    "{"key":"value"}",  // JSON data
    [](auto set_header) {
        set_header("Content-Type: application/json");
        set_header("Authorization: Bearer token123");
    }
);
```

## Handling ImGui Warnings

To suppress specific warnings from ImGui headers (such as `-Wnontrivial-memcall`), use the provided wrapper:

```cpp
// Instead of:
#include <imgui.h>

// Use (with proper relative path):
#include "path/to/helpers/imgui_include.hpp"
```

### Examples for Different Directory Levels

For files in different locations relative to the `helpers` directory, use the appropriate relative path:

```cpp
// For files directly in the /src directory:
#include "helpers/imgui_include.hpp"

// For files in /src/cards:
#include "../helpers/imgui_include.hpp"

// For files in /src/cards/productivity:
#include "../../helpers/imgui_include.hpp"

// For files in /src/cards/information/calendar:
#include "../../../helpers/imgui_include.hpp"

// For files within the helpers directory:
#include "./imgui_include.hpp"
```

### How It Works

The wrapper header (`imgui_include.hpp`) encapsulates all ImGui headers and uses compiler-specific pragmas to disable specific warnings only for those headers:

- For Clang: Disables `-Wnontrivial-memcall` warnings
- For GCC: Disables `-Wclass-memaccess` warnings (equivalent to Clang's warning)

This allows for clean compilation without modifying the ImGui source code directly. The CMake configuration also applies warning suppression flags when building the ImGui library target.

For example, in a file located in `src/cards/information/`:
```cpp
#include "../../helpers/imgui_include.hpp"
```

To automatically update ImGui includes, run:
```bash
./update_imgui_includes.sh
```

This wrapper handles diagnostic suppression for both Clang and GCC. The CMake configuration also 
applies warning suppression flags when building the ImGui library target.

## Playing Simple Sounds

The `media_player` helper provides a static `play_sound_once(path)` function for playing a local sound file (e.g., for alarms or notifications) without tracking playback position or duration.

```cpp
media_player::play_sound_once("img/alarm.mp3");
```

This uses the same MPV-based infrastructure as the main media player, ensuring DRY code and consistent playback.

## Notes

- Most helpers are designed to be self-contained with minimal dependencies on other parts of the application.
- When adding a new helper, consider creating a brief description here.
- For more complex helpers, consider adding detailed documentation within the header file itself.