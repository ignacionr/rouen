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
| `media_player.hpp` | Interface for media playback |
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

## Notes

- Most helpers are designed to be self-contained with minimal dependencies on other parts of the application.
- When adding a new helper, consider creating a brief description here.
- For more complex helpers, consider adding detailed documentation within the header file itself.