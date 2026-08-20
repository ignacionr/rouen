#pragma once

// 1. Standard includes in alphabetic order
#include <functional>
#include <mutex>
#include <string>
#include <thread>

// 2. Libraries used in the project, in alphabetic order
// None in this file

// 3. All other includes
// None in this file

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/types.h>
#endif

namespace rouen::helpers {

    // Spawns a child process with its stdin, stdout, and stderr each
    // piped to this process, dispatching complete lines read from
    // stdout/stderr on dedicated background threads. Used by cards that
    // treat an external executable as a live, interactive data source
    // (see src/cards/information/adaptive_process_card.hpp).
    //
    // This is not a general process manager - see
    // src/hosts/process_host.hpp for monitoring/orchestrating already
    // has that job. This class exists because none of the process
    // spawning already in the tree keeps stdin writable (process_host
    // discards a child's stdout/stdin entirely; every popen() call site
    // is one-directional).
    //
    // Callbacks run on background reader/waiter threads, never on the
    // thread that constructed this object - callers must synchronize
    // their own state against those threads themselves.
    //
    // The destructor closes stdin, forcibly terminates the process, and
    // joins every background thread before returning, so no callback
    // can fire after this object is destroyed. There is deliberately no
    // "detach and let it run" option.
    class piped_process {
    public:
        piped_process(
            std::string const& command_line,
            std::function<void(std::string const& line)> on_stdout_line,
            std::function<void(std::string const& line)> on_stderr_line,
            std::function<void(int exit_code)> on_exit
        );

        ~piped_process();

        piped_process(piped_process const&) = delete;
        piped_process& operator=(piped_process const&) = delete;

        // Writes one line (a trailing '\n' is appended) to the
        // process's stdin and flushes. Returns false if stdin is
        // already closed or the write failed.
        bool write_line(std::string const& line);

        // Closes the write end of stdin, signaling EOF to the child. A
        // well-behaved process should exit soon afterward. Safe to call
        // more than once.
        void close_stdin();

        // Forcibly terminates the process if it is still running. Safe
        // to call even if the process has already exited.
        void terminate();

        [[nodiscard]] bool spawn_succeeded() const { return spawned_; }
        [[nodiscard]] std::string const& spawn_error() const { return spawn_error_; }

    private:
        void stdout_reader_loop();
        void stderr_reader_loop();
        void wait_loop();

        std::function<void(std::string const&)> on_stdout_line_;
        std::function<void(std::string const&)> on_stderr_line_;
        std::function<void(int)> on_exit_;

        bool spawned_{false};
        std::string spawn_error_;
        bool stdin_open_{false};
        std::mutex stdin_mutex_;

        std::thread stdout_thread_;
        std::thread stderr_thread_;
        std::thread wait_thread_;

#if defined(_WIN32)
        HANDLE process_handle_{nullptr};
        HANDLE stdin_write_{nullptr};
        HANDLE stdout_read_{nullptr};
        HANDLE stderr_read_{nullptr};
#else
        pid_t pid_{-1};
        int stdin_write_{-1};
        int stdout_read_{-1};
        int stderr_read_{-1};
#endif
    };

} // namespace rouen::helpers
