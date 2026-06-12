# AI Agent Coding Guidelines & Insights

Welcome! This guide outlines key patterns, architecture insights, and build system details to help AI coding agents working on the Rouen project get up to speed quickly and avoid common pitfalls.

---

## 1. Build System & Toolchain

* **Nix Development Shell:** Rouen uses Nix flakes for environment configuration. 
  * Always execute builds, runs, or test commands wrapped in `nix develop` (e.g., `nix develop --command cmake --build build-cmake-tools`).
  * Nix packages Clang 19, SDL2, Curl, OpenSSL, Google Test, Glaze, and SQLite.
* **C++23 Standards:** The project utilizes latest C++23 features (e.g., `std::format`, `std::string_view` matches). On macOS, the deployment target is set to `15.4` to ensure compile compatibility with std::format support.
* **CMake Presets:**
  * Configure build: `cmake --preset default`
  * Configure test suite: `cmake -S . -B build-tests -DBUILD_TESTS=ON`
  * Run tests: `ctest --test-dir build-tests --output-on-failure`
* **Local Installation:** On macOS, the target builds an app bundle under `build-cmake-tools/rouen.app`. Installing it locally to `/Applications/rouen.app` is done via `cmake --install build-cmake-tools`.

---

## 2. Core Architectural Patterns & Pitfalls

### A. Media Player Processes & Unix Signal Handling
* **Process Spawning:** In `src/helpers/media_player_item.hpp`, processes like `mpv` are spawned using `fork()` and `execvp()` rather than `sh -c` shell wrappers. This gives exact control over process PIDs.
* **Process Session:** To decouple child processes from the parent process terminal, the child calls `setsid()` immediately after forking.
* **Graceful Termination & Zombie Prevention:**
  1. Always attempt to write a grace command (e.g. `{"command":["quit"]}\n`) to the player socket first.
  2. Wait up to 100ms using non-blocking `waitpid(player_pid, nullptr, WNOHANG)`.
  3. Send `SIGKILL` if the process fails to exit within the window.
  4. Always call `waitpid` to reap the process so it does not turn into a system zombie.

### B. YouTube Channel RSS Feed Resolution
* YouTube handles/usernames (e.g., `@ChannelName`) and general channel page URLs must be resolved into valid RSS URLs before being processed.
* Use `resolveYoutubeUrl()` in `src/hosts/rss_host.hpp` to retrieve the page HTML and parse the channel ID via regex (matching `channel_id`, `browseId`, `channelId`, etc.) to get `https://www.youtube.com/feeds/videos.xml?channel_id=...`.

### C. HTML Media Extraction Regex Matching
* Regex extraction patterns in `src/helpers/html_media_extractor.hpp` (e.g., for YouTube or Vimeo) capture the video ID in a sub-group (captured group `1`).
* **Warning:** When iterating match groups, `iter->str(1)` will only contain the ID (e.g., `dQw4w9WgXcQ`), not the host domain. Always check the full matched string `iter->str(0)` to identify the streaming service (e.g., matching `youtube` or `vimeo`) rather than checking the extracted ID string.

---

## 3. General Development Guidelines

* **Preserve Documentation:** If your code edits change any documented features or configuration flags, update the corresponding `README.md` files immediately.
* **Code Quality & DRY:** Adhere to the DRY (Don't Repeat Yourself) principle. Keep functions reusable, well-scoped, and follow modern C++ best practices.
