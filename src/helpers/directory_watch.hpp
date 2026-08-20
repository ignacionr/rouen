#pragma once

#include <string>
#include <vector>
#include <memory>

namespace rouen::helpers {

    // Watches a directory tree for file changes using the OS's native
    // notification API -- ReadDirectoryChangesW on Windows, FSEvents on macOS,
    // inotify on Linux -- so watching stays cheap no matter how many entries the
    // tree holds; nothing here rescans the directory on a timer. Recursive on
    // all three platforms (Windows and FSEvents recurse natively; the Linux
    // backend adds one inotify watch per subdirectory, including new ones
    // created after the fact, since inotify has no native recursive mode).
    //
    // Platform headers/threads live in directory_watch.cpp so including this
    // header stays cheap for every card that wants to watch a directory.
    class directory_watch {
    public:
        explicit directory_watch(std::string directory);
        ~directory_watch();

        directory_watch(const directory_watch&) = delete;
        directory_watch& operator=(const directory_watch&) = delete;

        // Returns paths touched since the last call and clears the pending list.
        // May contain duplicates or paths that no longer exist (e.g. deleted or
        // renamed away); callers should dedupe and check existence before acting.
        [[nodiscard]] std::vector<std::string> drain_changes();

        // False when the native watch never actually started (bad path, no
        // permission, ...) so callers can surface that instead of silently
        // showing an empty change list forever.
        [[nodiscard]] bool active() const;

    private:
        struct impl;
        std::unique_ptr<impl> impl_;
    };

} // namespace rouen::helpers
