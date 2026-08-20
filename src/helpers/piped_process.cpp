#include "piped_process.hpp"

#include <format>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

    // Appends a chunk of freshly-read bytes to `partial` and dispatches
    // every complete line (trailing '\r' from CRLF output stripped) to
    // `callback`, leaving any trailing partial line in `partial` for the
    // next call.
    void dispatch_lines(std::string& partial, char const* data, size_t len,
                         std::function<void(std::string const&)> const& callback) {
        partial.append(data, len);
        size_t start = 0;
        while (true) {
            size_t const newline_pos = partial.find('\n', start);
            if (newline_pos == std::string::npos) {
                break;
            }
            std::string line = partial.substr(start, newline_pos - start);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (callback) {
                callback(line);
            }
            start = newline_pos + 1;
        }
        partial.erase(0, start);
    }

} // namespace

namespace rouen::helpers {

    piped_process::piped_process(
        std::string const& command_line,
        std::function<void(std::string const&)> on_stdout_line,
        std::function<void(std::string const&)> on_stderr_line,
        std::function<void(int)> on_exit
    )
        : on_stdout_line_{std::move(on_stdout_line)},
          on_stderr_line_{std::move(on_stderr_line)},
          on_exit_{std::move(on_exit)} {
#if defined(_WIN32)
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = nullptr;

        HANDLE stdin_read{nullptr};
        HANDLE stdout_write{nullptr};
        HANDLE stderr_write{nullptr};

        bool const pipes_ok =
            CreatePipe(&stdin_read, &stdin_write_, &sa, 0) &&
            CreatePipe(&stdout_read_, &stdout_write, &sa, 0) &&
            CreatePipe(&stderr_read_, &stderr_write, &sa, 0);

        if (!pipes_ok) {
            spawn_error_ = "CreatePipe failed";
            if (stdin_read) CloseHandle(stdin_read);
            if (stdin_write_) CloseHandle(stdin_write_);
            if (stdout_read_) CloseHandle(stdout_read_);
            if (stdout_write) CloseHandle(stdout_write);
            if (stderr_read_) CloseHandle(stderr_read_);
            if (stderr_write) CloseHandle(stderr_write);
            stdin_write_ = nullptr;
            stdout_read_ = nullptr;
            stderr_read_ = nullptr;
            return;
        }

        // The ends this process keeps for itself must not leak into any
        // other child process this application spawns later.
        SetHandleInformation(stdin_write_, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(stdout_read_, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(stderr_read_, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdInput = stdin_read;
        si.hStdOutput = stdout_write;
        si.hStdError = stderr_write;

        std::vector<char> cmdline_buf(command_line.begin(), command_line.end());
        cmdline_buf.push_back('\0');

        PROCESS_INFORMATION pi{};
        BOOL const ok = CreateProcessA(
            nullptr, cmdline_buf.data(), nullptr, nullptr,
            TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

        // The child holds its own copies of these; this process has no
        // further use for them.
        CloseHandle(stdin_read);
        CloseHandle(stdout_write);
        CloseHandle(stderr_write);

        if (!ok) {
            spawn_error_ = std::format("CreateProcess failed (error {})", GetLastError());
            CloseHandle(stdin_write_);
            CloseHandle(stdout_read_);
            CloseHandle(stderr_read_);
            stdin_write_ = nullptr;
            stdout_read_ = nullptr;
            stderr_read_ = nullptr;
            return;
        }

        process_handle_ = pi.hProcess;
        CloseHandle(pi.hThread);
#else
        int stdin_pipe[2]{-1, -1};
        int stdout_pipe[2]{-1, -1};
        int stderr_pipe[2]{-1, -1};

        auto close_all = [&]() {
            for (int fd : {stdin_pipe[0], stdin_pipe[1], stdout_pipe[0], stdout_pipe[1], stderr_pipe[0], stderr_pipe[1]}) {
                if (fd >= 0) close(fd);
            }
        };

        if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
            spawn_error_ = std::format("pipe() failed: {}", strerror(errno));
            close_all();
            return;
        }

        pid_t const child_pid = fork();
        if (child_pid < 0) {
            spawn_error_ = std::format("fork() failed: {}", strerror(errno));
            close_all();
            return;
        }

        if (child_pid == 0) {
            // Child process: wire the pipe ends to the standard streams,
            // then hand off to a shell so the caller's command line gets
            // ordinary shell quoting/argument-splitting instead of a
            // hand-rolled tokenizer here.
            dup2(stdin_pipe[0], STDIN_FILENO);
            dup2(stdout_pipe[1], STDOUT_FILENO);
            dup2(stderr_pipe[1], STDERR_FILENO);
            close_all();
            execl("/bin/sh", "sh", "-c", command_line.c_str(), static_cast<char*>(nullptr));
            _exit(127); // exec failed
        }

        // Parent: keep only its own ends.
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        stdin_write_ = stdin_pipe[1];
        stdout_read_ = stdout_pipe[0];
        stderr_read_ = stderr_pipe[0];
        pid_ = child_pid;
#endif

        spawned_ = true;
        stdin_open_ = true;
        stdout_thread_ = std::thread(&piped_process::stdout_reader_loop, this);
        stderr_thread_ = std::thread(&piped_process::stderr_reader_loop, this);
        wait_thread_ = std::thread(&piped_process::wait_loop, this);
    }

    piped_process::~piped_process() {
        close_stdin();
        terminate();
        if (stdout_thread_.joinable()) stdout_thread_.join();
        if (stderr_thread_.joinable()) stderr_thread_.join();
        if (wait_thread_.joinable()) wait_thread_.join();
#if defined(_WIN32)
        if (process_handle_) {
            CloseHandle(process_handle_);
            process_handle_ = nullptr;
        }
#endif
    }

    bool piped_process::write_line(std::string const& line) {
        std::lock_guard<std::mutex> const lock(stdin_mutex_);
        if (!stdin_open_) {
            return false;
        }

        std::string payload = line;
        payload.push_back('\n');
        size_t written = 0;

#if defined(_WIN32)
        if (!stdin_write_) return false;
        while (written < payload.size()) {
            DWORD chunk = 0;
            if (!WriteFile(stdin_write_, payload.data() + written,
                           static_cast<DWORD>(payload.size() - written), &chunk, nullptr) || chunk == 0) {
                return false;
            }
            written += chunk;
        }
#else
        if (stdin_write_ < 0) return false;
        while (written < payload.size()) {
            ssize_t const chunk = write(stdin_write_, payload.data() + written, payload.size() - written);
            if (chunk <= 0) return false;
            written += static_cast<size_t>(chunk);
        }
#endif
        return true;
    }

    void piped_process::close_stdin() {
        std::lock_guard<std::mutex> const lock(stdin_mutex_);
        if (!stdin_open_) {
            return;
        }
        stdin_open_ = false;
#if defined(_WIN32)
        if (stdin_write_) {
            CloseHandle(stdin_write_);
            stdin_write_ = nullptr;
        }
#else
        if (stdin_write_ >= 0) {
            close(stdin_write_);
            stdin_write_ = -1;
        }
#endif
    }

    void piped_process::terminate() {
#if defined(_WIN32)
        if (process_handle_) {
            TerminateProcess(process_handle_, 1);
        }
#else
        if (pid_ > 0) {
            kill(pid_, SIGKILL);
        }
#endif
    }

    void piped_process::stdout_reader_loop() {
        std::string partial;
        char buf[4096];
#if defined(_WIN32)
        DWORD n = 0;
        while (stdout_read_ && ReadFile(stdout_read_, buf, sizeof(buf), &n, nullptr) && n > 0) {
            dispatch_lines(partial, buf, n, on_stdout_line_);
        }
        if (stdout_read_) {
            CloseHandle(stdout_read_);
            stdout_read_ = nullptr;
        }
#else
        ssize_t n = 0;
        while (stdout_read_ >= 0 && (n = read(stdout_read_, buf, sizeof(buf))) > 0) {
            dispatch_lines(partial, buf, static_cast<size_t>(n), on_stdout_line_);
        }
        if (stdout_read_ >= 0) {
            close(stdout_read_);
            stdout_read_ = -1;
        }
#endif
    }

    void piped_process::stderr_reader_loop() {
        std::string partial;
        char buf[4096];
#if defined(_WIN32)
        DWORD n = 0;
        while (stderr_read_ && ReadFile(stderr_read_, buf, sizeof(buf), &n, nullptr) && n > 0) {
            dispatch_lines(partial, buf, n, on_stderr_line_);
        }
        if (stderr_read_) {
            CloseHandle(stderr_read_);
            stderr_read_ = nullptr;
        }
#else
        ssize_t n = 0;
        while (stderr_read_ >= 0 && (n = read(stderr_read_, buf, sizeof(buf))) > 0) {
            dispatch_lines(partial, buf, static_cast<size_t>(n), on_stderr_line_);
        }
        if (stderr_read_ >= 0) {
            close(stderr_read_);
            stderr_read_ = -1;
        }
#endif
    }

    void piped_process::wait_loop() {
#if defined(_WIN32)
        if (!process_handle_) return;
        WaitForSingleObject(process_handle_, INFINITE);
        DWORD exit_code = 0;
        GetExitCodeProcess(process_handle_, &exit_code);
        if (on_exit_) on_exit_(static_cast<int>(exit_code));
#else
        if (pid_ <= 0) return;
        int status = 0;
        waitpid(pid_, &status, 0);
        int const exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        if (on_exit_) on_exit_(exit_code);
#endif
    }

} // namespace rouen::helpers
