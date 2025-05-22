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
| `platform_utils.hpp` | Platform-specific utilities including resource path management |
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

## Using Resource Path Utility

The `platform_utils` helper provides a `get_resource_path` function to locate resource files in both development and bundled application environments:

```cpp
// Get the path to a resource file
auto presets_path = rouen::platform::get_resource_path("presets.txt");

// Get the path to a resource in a subdirectory
auto alarm_path = rouen::platform::get_resource_path("alarm.mp3", "img");

// Use the path with standard C++ file operations
std::ifstream file(presets_path);
```

This handles the different file locations between:
- Development environment (working directory)
- macOS application bundle (Resources directory)
- Linux packages

## Playing Simple Sounds

The `media_player` helper provides a static `play_sound_once(path)` function for playing a local sound file (e.g., for alarms or notifications) without tracking playback position or duration.

```cpp
// The path is automatically resolved through the resource path utility
media_player::play_sound_once("img/alarm.mp3");
```

This uses the same MPV-based infrastructure as the main media player, ensuring DRY code and consistent playback.

## Media Player Implementation

The `media_player.hpp` provides a robust interface for playing audio content through MPV:

```cpp
// Create a media player instance and play a URL
media_player::item media;
media.url = "https://example.com/podcast.mp3";
media.playMedia();

// Check status and control playback
if (media.is_playing) {
    // Get current position and duration
    double position = media.position;
    double duration = media.duration;
    
    // Stop media when done
    media.stopMedia();
}
```

### Media Player Features

- **URL Validation and Sanitization**: Automatically validates URLs and sanitizes them to handle special characters
- **Process Management**: Creates and manages MPV processes with proper cleanup
- **Socket Communication**: Exchanges commands and status information with MPV via Unix sockets
- **Playback Position Tracking**: Continuously monitors playback position and duration
- **Volume Control**: Set and adjust playback volume in real time via MPV socket (ImGui slider in UI, or programmatically)
- **Error Recovery**: Includes automatic reconnection and fault tolerance mechanisms
- **Timeouts and Watchdogs**: Prevents hangs with timeout management and watchdog processes
- **Network Optimization**: Configures MPV with optimal network streaming settings
- **Detailed Logging**: Provides comprehensive diagnostic information for troubleshooting

#### Example: Setting Volume

You can set the playback volume (0-100) at runtime:

```cpp
media_player::item media;
media.playMedia();
media.setVolume(75); // Set volume to 75%
```

In the UI, a volume slider is available when media is playing.

### Streaming Media Guidelines

When playing streaming media like podcasts or internet radio:

1. **URL Format**: Ensure URLs are properly formatted, including appropriate protocol prefixes
2. **Timeout Settings**: Allow sufficient time for slow connections (built-in timeouts handle this automatically)
3. **Network Issues**: The player will attempt to reconnect automatically if network issues occur
4. **Special Characters**: URL sanitization handles most special characters, but avoid using unusual characters in URLs

### Simple Sound Effects

For simple one-off sound effects (like alarms or notifications), use the simplified interface:

```cpp
// Play a sound effect once
media_player::play_sound_once("path/to/sound.mp3");
```

This uses the same MPV-based infrastructure as the main media player, ensuring DRY code and consistent playback.

## Notes

- Most helpers are designed to be self-contained with minimal dependencies on other parts of the application.
- When adding a new helper, consider creating a brief description here.
- For more complex helpers, consider adding detailed documentation within the header file itself.