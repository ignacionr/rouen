#include "directory_watch.hpp"

#include <mutex>
#include <thread>
#include <filesystem>

#if defined(_WIN32)
    #include <windows.h>
#elif defined(__APPLE__)
    #include <CoreServices/CoreServices.h>
    #include <condition_variable>
#else
    #include <sys/inotify.h>
    #include <unistd.h>
    #include <poll.h>
    #include <unordered_map>
    #include <cstdint>
#endif

namespace rouen::helpers {

struct directory_watch::impl {
    std::string directory;
    std::mutex pending_mutex;
    std::vector<std::string> pending_paths;
    std::thread thread;
    bool active{false};

    explicit impl(std::string dir) : directory(std::move(dir)) {
        if (!directory.empty()) start();
    }

    ~impl() { stop(); }

    std::vector<std::string> drain_changes() {
        std::vector<std::string> out;
        std::lock_guard<std::mutex> lock(pending_mutex);
        out.swap(pending_paths);
        return out;
    }

    void push_change(std::string path) {
        std::lock_guard<std::mutex> lock(pending_mutex);
        pending_paths.push_back(std::move(path));
    }

#if defined(_WIN32)
    HANDLE dir_handle{INVALID_HANDLE_VALUE};
    HANDLE stop_event{nullptr};

    void start() {
        std::wstring wdir = std::filesystem::path(directory).wstring();
        dir_handle = CreateFileW(wdir.c_str(), FILE_LIST_DIRECTORY,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (dir_handle == INVALID_HANDLE_VALUE) return;

        stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        thread = std::thread(&impl::run_windows, this);
        active = true;
    }

    void stop() {
        if (stop_event) SetEvent(stop_event);
        if (thread.joinable()) thread.join();
        if (dir_handle != INVALID_HANDLE_VALUE) CloseHandle(dir_handle);
        if (stop_event) CloseHandle(stop_event);
    }

    void run_windows() {
        alignas(alignof(DWORD)) BYTE buffer[65536];
        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        HANDLE wait_handles[2] = { overlapped.hEvent, stop_event };

        for (;;) {
            BOOL queued = ReadDirectoryChangesW(
                dir_handle, buffer, sizeof(buffer), TRUE, // TRUE = watch the whole subtree
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE,
                nullptr, &overlapped, nullptr);
            if (!queued && GetLastError() != ERROR_IO_PENDING) break;

            DWORD wait = WaitForMultipleObjects(2, wait_handles, FALSE, INFINITE);
            if (wait == WAIT_OBJECT_0 + 1) { // stop requested
                CancelIoEx(dir_handle, &overlapped);
                WaitForSingleObject(overlapped.hEvent, INFINITE);
                break;
            }

            DWORD transferred = 0;
            if (!GetOverlappedResult(dir_handle, &overlapped, &transferred, FALSE) || transferred == 0) {
                // Buffer overflowed (events dropped) or a spurious wake; just
                // re-issue the read rather than tracking every corner case --
                // any missed change still shows up the next time it happens.
                ResetEvent(overlapped.hEvent);
                continue;
            }

            auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer);
            for (;;) {
                std::wstring_view name(info->FileName, info->FileNameLength / sizeof(WCHAR));
                std::filesystem::path full = std::filesystem::path(directory) / std::filesystem::path(name);
                push_change(full.string());

                if (info->NextEntryOffset == 0) break;
                info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
                    reinterpret_cast<BYTE*>(info) + info->NextEntryOffset);
            }

            ResetEvent(overlapped.hEvent);
        }

        CloseHandle(overlapped.hEvent);
    }
#elif defined(__APPLE__)
    FSEventStreamRef stream{nullptr};
    CFRunLoopRef run_loop{nullptr};
    std::mutex run_loop_mutex;
    std::condition_variable run_loop_cv;
    bool run_loop_ready{false};

    static void fsevents_callback(ConstFSEventStreamRef, void* client_info, size_t num_events,
                                   void* event_paths, const FSEventStreamEventFlags event_flags[],
                                   const FSEventStreamEventId[]) {
        auto* self = static_cast<impl*>(client_info);
        auto** paths = static_cast<char**>(event_paths);
        for (size_t i = 0; i < num_events; ++i) {
            if (event_flags[i] & kFSEventStreamEventFlagItemIsDir) continue;
            self->push_change(std::string(paths[i]));
        }
    }

    void start() {
        thread = std::thread(&impl::run_macos, this);
        std::unique_lock<std::mutex> lock(run_loop_mutex);
        run_loop_cv.wait(lock, [this] { return run_loop_ready; });
    }

    void stop() {
        if (run_loop) CFRunLoopStop(run_loop);
        if (thread.joinable()) thread.join();
    }

    void run_macos() {
        CFStringRef dir_ref = CFStringCreateWithCString(kCFAllocatorDefault, directory.c_str(), kCFStringEncodingUTF8);
        CFArrayRef paths = CFArrayCreate(kCFAllocatorDefault, reinterpret_cast<const void**>(&dir_ref), 1, &kCFTypeArrayCallBacks);

        FSEventStreamContext context{};
        context.info = this;

        stream = FSEventStreamCreate(kCFAllocatorDefault, &impl::fsevents_callback, &context,
                                      paths, kFSEventStreamEventIdSinceNow, 0.3,
                                      kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer);
        CFRelease(paths);
        CFRelease(dir_ref);

        if (stream) {
            run_loop = CFRunLoopGetCurrent();
            FSEventStreamScheduleWithRunLoop(stream, run_loop, kCFRunLoopDefaultMode);
            FSEventStreamStart(stream);
        }

        {
            std::lock_guard<std::mutex> lock(run_loop_mutex);
            active = (stream != nullptr);
            run_loop_ready = true;
        }
        run_loop_cv.notify_one();

        if (stream) {
            CFRunLoopRun(); // returns once stop() calls CFRunLoopStop() from another thread
            FSEventStreamStop(stream);
            FSEventStreamInvalidate(stream);
            FSEventStreamRelease(stream);
            stream = nullptr;
        }
    }
#else
    int inotify_fd{-1};
    int stop_pipe[2]{-1, -1}; // self-pipe so poll() wakes promptly on stop
    std::unordered_map<int, std::string> watch_dirs; // wd -> absolute dir path; only touched by run_linux()

    static constexpr uint32_t kWatchMask =
        IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_MODIFY;

    void add_watch_recursive(const std::filesystem::path& dir) {
        int wd = inotify_add_watch(inotify_fd, dir.c_str(), kWatchMask);
        if (wd < 0) return;
        watch_dirs[wd] = dir.string();

        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec) break;
            bool is_dir = entry.is_directory(ec);
            if (!ec && is_dir) add_watch_recursive(entry.path());
        }
    }

    void start() {
        inotify_fd = inotify_init1(IN_NONBLOCK);
        if (inotify_fd < 0) return;

        if (pipe(stop_pipe) != 0) {
            close(inotify_fd);
            inotify_fd = -1;
            return;
        }

        add_watch_recursive(std::filesystem::path(directory));
        if (watch_dirs.empty()) {
            close(inotify_fd);
            inotify_fd = -1;
            close(stop_pipe[0]);
            close(stop_pipe[1]);
            stop_pipe[0] = stop_pipe[1] = -1;
            return;
        }

        thread = std::thread(&impl::run_linux, this);
        active = true;
    }

    void stop() {
        if (stop_pipe[1] >= 0) {
            char b = 1;
            [[maybe_unused]] ssize_t written = write(stop_pipe[1], &b, 1);
        }
        if (thread.joinable()) thread.join();
        for (const auto& [wd, dir] : watch_dirs) inotify_rm_watch(inotify_fd, wd);
        if (inotify_fd >= 0) close(inotify_fd);
        if (stop_pipe[0] >= 0) close(stop_pipe[0]);
        if (stop_pipe[1] >= 0) close(stop_pipe[1]);
    }

    void run_linux() {
        constexpr size_t event_size = sizeof(inotify_event);
        constexpr size_t buf_len = 64 * (event_size + 256);
        std::vector<char> buffer(buf_len);

        struct pollfd fds[2];
        fds[0].fd = inotify_fd; fds[0].events = POLLIN; fds[0].revents = 0;
        fds[1].fd = stop_pipe[0]; fds[1].events = POLLIN; fds[1].revents = 0;

        for (;;) {
            int ready = poll(fds, 2, -1);
            if (ready <= 0) continue;
            if (fds[1].revents & POLLIN) break; // stop requested

            ssize_t len = read(inotify_fd, buffer.data(), buffer.size());
            if (len <= 0) continue;

            ssize_t offset = 0;
            while (offset < len) {
                auto* event = reinterpret_cast<inotify_event*>(buffer.data() + offset);
                auto dir_it = watch_dirs.find(event->wd);
                if (event->len > 0 && dir_it != watch_dirs.end()) {
                    std::filesystem::path full = std::filesystem::path(dir_it->second) / event->name;
                    if (event->mask & IN_ISDIR) {
                        // A new (or moved-in) subdirectory needs its own watch,
                        // since inotify never recurses on its own.
                        if (event->mask & (IN_CREATE | IN_MOVED_TO)) add_watch_recursive(full);
                    } else {
                        push_change(full.string());
                    }
                }
                offset += static_cast<ssize_t>(event_size + event->len);
            }
        }
    }
#endif
};

directory_watch::directory_watch(std::string directory) : impl_(std::make_unique<impl>(std::move(directory))) {}
directory_watch::~directory_watch() = default;

std::vector<std::string> directory_watch::drain_changes() { return impl_->drain_changes(); }
bool directory_watch::active() const { return impl_->active; }

} // namespace rouen::helpers
