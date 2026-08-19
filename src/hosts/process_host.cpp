#include "process_host.hpp"
#include "../models/productivity/process_definition.hpp"
#include "../helpers/debug.hpp"

#include <atomic>
#include <thread>
#include <chrono>
#include <cstring>
#include <cctype>
#include <format>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_set>

#define PROCHOST_ERROR(message) LOG_COMPONENT("PROCHOST", LOG_LEVEL_ERROR, message)
#define PROCHOST_WARN(message) LOG_COMPONENT("PROCHOST", LOG_LEVEL_WARN, message)
#define PROCHOST_INFO(message) LOG_COMPONENT("PROCHOST", LOG_LEVEL_INFO, message)
#define PROCHOST_ERROR_FMT(fmt, ...) PROCHOST_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define PROCHOST_INFO_FMT(fmt, ...) PROCHOST_INFO(debug::format_log(fmt, __VA_ARGS__))

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <iphlpapi.h>
    #include <windows.h>
    #include <tlhelp32.h>
    #include <psapi.h>
    #pragma comment(lib, "Psapi.lib")
    #pragma comment(lib, "iphlpapi.lib")
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <unistd.h>
    #include <fcntl.h>
    #include <signal.h>
    #include <sys/wait.h>
    #include <sys/types.h>
    #include <spawn.h>
    #include <filesystem>
    #if defined(__APPLE__)
        #include <libproc.h>
        #include <sys/proc_info.h>
        #include <sys/sysctl.h>
        #include "../helpers/process_helper.hpp"
    #endif
    extern char **environ;
#endif

namespace rouen::hosts {

using rouen::models::productivity::process_definition;

namespace {
    std::atomic<uint64_t> g_run_counter{0};

    std::string generate_run_id(int64_t definition_id) {
        uint64_t seq = ++g_run_counter;
        return std::format("run-{}-{}", definition_id, seq);
    }

    // Splits a raw stdin/stderr byte chunk into complete lines, keeping the trailing
    // partial line (if any) in `partial` for the next chunk. Bounds the destination
    // buffer so a runaway/chatty process cannot grow memory without limit.
    void append_chunk(std::mutex& data_mutex, std::vector<std::string>& lines, std::string& partial,
                       const char* data, size_t len, size_t max_lines) {
        std::lock_guard<std::mutex> lock(data_mutex);
        partial.append(data, len);
        size_t start = 0;
        while (true) {
            size_t nl = partial.find('\n', start);
            if (nl == std::string::npos) break;
            std::string line = partial.substr(start, nl - start);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(std::move(line));
            start = nl + 1;
        }
        partial.erase(0, start);
        if (lines.size() > max_lines) {
            lines.erase(lines.begin(), lines.begin() + static_cast<long>(lines.size() - max_lines));
        }
    }

    // Heuristic direction classifier shared by every platform's TCP sampler: a
    // LISTEN row is always an inbound listener; otherwise a local port in the
    // ephemeral range (>=32768, the common OS-assigned dynamic port range) means
    // this side dialed out, and a stable/registered local port means a peer
    // connected in to us. Not a guarantee, but matches what netstat-style tools use.
    std::string classify_tcp_direction(const std::string& state, int local_port) {
        if (state == "LISTEN") return "Listening";
        return (local_port >= 32768) ? "Outbound" : "Inbound";
    }

#ifndef _WIN32
    // Basic shell-like tokenizer: splits on whitespace, honors single/double quotes.
    std::vector<std::string> tokenize_arguments(const std::string& args) {
        std::vector<std::string> tokens;
        std::string current;
        bool in_token = false;
        char quote = '\0';
        for (size_t i = 0; i < args.size(); ++i) {
            char c = args[i];
            if (quote != '\0') {
                if (c == quote) { quote = '\0'; }
                else { current += c; }
                continue;
            }
            if (c == '"' || c == '\'') { quote = c; in_token = true; continue; }
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (in_token) { tokens.push_back(current); current.clear(); in_token = false; }
                continue;
            }
            current += c;
            in_token = true;
        }
        if (in_token) tokens.push_back(current);
        return tokens;
    }
#endif
}

struct process_host::running_process {
    std::string run_id;
    int64_t definition_id{0};
    std::string definition_name;

    std::atomic<process_run_state> state{process_run_state::running};
    std::atomic<long> pid{0};

    mutable std::mutex data_mutex; // guards stderr_lines / stats / exit_code below
    std::vector<std::string> stderr_lines;
    process_stats stats;
    std::optional<int> exit_code;
    std::string start_error;

    std::thread stderr_thread;
    std::thread watcher_thread;
    std::atomic<bool> stop_requested{false};

#ifdef _WIN32
    HANDLE process_handle{nullptr};
    HANDLE job_handle{nullptr};
    HANDLE stderr_read{nullptr};
#else
    pid_t os_pid{-1};
    int stderr_fd{-1};
#endif

    ~running_process() {
        // Rouen is the only supervisor of these runs; nothing persists the
        // run_id/pid mapping across restarts, so a still-running child at
        // shutdown would become unmanageable. Terminate it here rather than
        // leaking an orphan.
        if (state.load() == process_run_state::running) {
#ifdef _WIN32
            if (job_handle) TerminateJobObject(job_handle, 1);
            else if (process_handle) TerminateProcess(process_handle, 1);
#else
            if (os_pid > 0) killpg(os_pid, SIGKILL);
#endif
        }
        stop_requested.store(true);
        if (stderr_thread.joinable()) stderr_thread.join();
        if (watcher_thread.joinable()) watcher_thread.join();
#ifdef _WIN32
        if (process_handle) CloseHandle(process_handle);
        if (job_handle) CloseHandle(job_handle);
#endif
    }
};

namespace {
    constexpr size_t kMaxStderrLines = 2000;

#ifdef _WIN32
    std::string build_windows_cmdline(const std::string& exe, const std::string& args) {
        std::string cmd = "\"" + exe + "\"";
        if (!args.empty()) {
            cmd += " ";
            cmd += args;
        }
        return cmd;
    }

    bool spawn_windows(process_host::running_process& rp, const process_definition& def, std::string& err) {
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = nullptr;

        HANDLE stderr_read = nullptr;
        HANDLE stderr_write = nullptr;
        if (!CreatePipe(&stderr_read, &stderr_write, &sa, 0)) {
            err = "CreatePipe failed";
            return false;
        }
        // The read end lives in this process only; do not let a grandchild inherit it.
        SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);

        HANDLE nul_write = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdError = stderr_write;
        si.hStdOutput = (nul_write != INVALID_HANDLE_VALUE) ? nul_write : stderr_write;
        si.hStdInput = nullptr;

        PROCESS_INFORMATION pi{};
        std::string cmdline_str = build_windows_cmdline(def.executable_path, def.arguments);
        std::vector<char> cmdline_buf(cmdline_str.begin(), cmdline_str.end());
        cmdline_buf.push_back('\0');

        BOOL ok = CreateProcessA(
            nullptr,
            cmdline_buf.data(),
            nullptr,
            nullptr,
            TRUE, // inherit handles (stderr/stdout pipes)
            CREATE_NO_WINDOW | CREATE_SUSPENDED,
            nullptr,
            def.working_directory.empty() ? nullptr : def.working_directory.c_str(),
            &si,
            &pi);

        // Parent no longer needs the write ends; the child holds its own copies.
        CloseHandle(stderr_write);
        if (nul_write != INVALID_HANDLE_VALUE) CloseHandle(nul_write);

        if (!ok) {
            CloseHandle(stderr_read);
            err = std::format("CreateProcess failed (error {})", GetLastError());
            return false;
        }

        // Assign to a job object before resuming so grandchildren spawned early
        // are still captured, and so the whole tree dies if this handle is closed.
        HANDLE job = CreateJobObjectA(nullptr, nullptr);
        if (job) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
            jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
            if (!AssignProcessToJobObject(job, pi.hProcess)) {
                CloseHandle(job);
                job = nullptr;
            }
        }

        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);

        rp.process_handle = pi.hProcess;
        rp.job_handle = job;
        rp.stderr_read = stderr_read;
        rp.pid.store(static_cast<long>(pi.dwProcessId));
        return true;
    }

    std::string tcp_state_to_string_windows(DWORD state) {
        switch (state) {
            case MIB_TCP_STATE_CLOSED: return "CLOSED";
            case MIB_TCP_STATE_LISTEN: return "LISTEN";
            case MIB_TCP_STATE_SYN_SENT: return "SYN_SENT";
            case MIB_TCP_STATE_SYN_RCVD: return "SYN_RCVD";
            case MIB_TCP_STATE_ESTAB: return "ESTABLISHED";
            case MIB_TCP_STATE_FIN_WAIT1: return "FIN_WAIT1";
            case MIB_TCP_STATE_FIN_WAIT2: return "FIN_WAIT2";
            case MIB_TCP_STATE_CLOSE_WAIT: return "CLOSE_WAIT";
            case MIB_TCP_STATE_CLOSING: return "CLOSING";
            case MIB_TCP_STATE_LAST_ACK: return "LAST_ACK";
            case MIB_TCP_STATE_TIME_WAIT: return "TIME_WAIT";
            case MIB_TCP_STATE_DELETE_TCB: return "DELETE_TCB";
            default: return "UNKNOWN";
        }
    }

    std::string ipv4_to_string_windows(DWORD addr_network_order) {
        IN_ADDR addr{};
        addr.S_un.S_addr = addr_network_order;
        char buf[INET_ADDRSTRLEN] = {0};
        if (inet_ntop(AF_INET, &addr, buf, sizeof(buf))) return buf;
        return "";
    }

    // IPv4-only for v1; GetExtendedTcpTable also has an AF_INET6 form we don't query yet.
    std::vector<tcp_connection_info> sample_tcp_connections_windows(DWORD pid) {
        std::vector<tcp_connection_info> result;

        ULONG size = 0;
        GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
        if (size == 0) return result;

        std::vector<uint8_t> buf(size);
        if (GetExtendedTcpTable(buf.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) != NO_ERROR) {
            return result;
        }

        auto* table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buf.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            const auto& row = table->table[i];
            if (row.dwOwningPid != pid) continue;

            tcp_connection_info info;
            info.local_address = ipv4_to_string_windows(row.dwLocalAddr);
            info.local_port = static_cast<int>(ntohs(static_cast<u_short>(row.dwLocalPort)));
            info.remote_address = ipv4_to_string_windows(row.dwRemoteAddr);
            info.remote_port = static_cast<int>(ntohs(static_cast<u_short>(row.dwRemotePort)));
            info.state = tcp_state_to_string_windows(row.dwState);
            info.direction = classify_tcp_direction(info.state, info.local_port);
            result.push_back(std::move(info));
        }
        return result;
    }

    process_stats sample_stats_windows(const process_host::running_process& rp) {
        process_stats stats;
        DWORD pid = static_cast<DWORD>(rp.pid.load());

        DWORD handle_count = 0;
        if (rp.process_handle && GetProcessHandleCount(rp.process_handle, &handle_count)) {
            stats.open_handle_count = handle_count;
        }

        PROCESS_MEMORY_COUNTERS pmc{};
        pmc.cb = sizeof(pmc);
        if (rp.process_handle && GetProcessMemoryInfo(rp.process_handle, &pmc, sizeof(pmc))) {
            stats.memory_bytes = static_cast<long long>(pmc.WorkingSetSize);
        }

        stats.tcp_connections = sample_tcp_connections_windows(pid);

        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            THREADENTRY32 te{};
            te.dwSize = sizeof(te);
            int count = 0;
            if (Thread32First(snap, &te)) {
                do {
                    if (te.th32OwnerProcessID == pid) count++;
                } while (Thread32Next(snap, &te));
            }
            CloseHandle(snap);
            stats.thread_count = count;
        }

        if (rp.job_handle) {
            std::vector<uint8_t> buf(sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + sizeof(ULONG_PTR) * 63);
            auto* info = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(buf.data());
            if (QueryInformationJobObject(rp.job_handle, JobObjectBasicProcessIdList, info,
                                          static_cast<DWORD>(buf.size()), nullptr)) {
                int total = static_cast<int>(info->NumberOfProcessIdsInList);
                stats.subprocess_count = total > 0 ? total - 1 : 0; // exclude the root process itself
            }
        }

        return stats;
    }
#else
    bool spawn_posix(process_host::running_process& rp, const process_definition& def, std::string& err) {
        int stderr_pipe[2];
        if (pipe(stderr_pipe) != 0) {
            err = "pipe() failed";
            return false;
        }

        std::vector<std::string> arg_tokens = tokenize_arguments(def.arguments);
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(def.executable_path.c_str()));
        for (auto& a : arg_tokens) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);

        pid_t pid = fork();
        if (pid < 0) {
            err = "fork() failed";
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);
            return false;
        }

        if (pid == 0) {
            // Child: own process group so the whole tree can be killed together.
            setpgid(0, 0);
            dup2(stderr_pipe[1], STDERR_FILENO);
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) { dup2(devnull, STDOUT_FILENO); close(devnull); }
            if (!def.working_directory.empty()) {
                if (chdir(def.working_directory.c_str()) != 0) _exit(127);
            }
            execvp(def.executable_path.c_str(), argv.data());
            _exit(127); // exec failed
        }

        close(stderr_pipe[1]);
        rp.os_pid = pid;
        rp.stderr_fd = stderr_pipe[0];
        rp.pid.store(static_cast<long>(pid));
        return true;
    }

    #if defined(__APPLE__)
    // Parses `lsof -a -p <pid> -iTCP -n -P` output rather than hand-rolling the
    // libproc socket_fdinfo struct layout (soi_proto unions vary enough across
    // SDKs that a text-based parse is the more portable choice here). NAME column
    // looks like "192.168.1.5:54321->93.184.216.34:443 (ESTABLISHED)" for an
    // established connection, or "*:8080 (LISTEN)" for a listener.
    std::vector<tcp_connection_info> sample_tcp_connections_macos(pid_t pid) {
        std::vector<tcp_connection_info> result;
        std::string cmd = std::format("lsof -a -p {} -iTCP -n -P 2>/dev/null", pid);
        std::string output = ProcessHelper::executeCommand(cmd);
        if (output.empty()) return result;

        std::istringstream stream(output);
        std::string line;
        std::getline(stream, line); // header row
        while (std::getline(stream, line)) {
            auto state_open = line.rfind('(');
            auto state_close = line.rfind(')');
            if (state_open == std::string::npos || state_close == std::string::npos || state_close < state_open) continue;
            std::string state = line.substr(state_open + 1, state_close - state_open - 1);

            std::string endpoints = line.substr(0, state_open);
            // Trim trailing whitespace left over before the "(STATE)" suffix.
            while (!endpoints.empty() && std::isspace(static_cast<unsigned char>(endpoints.back()))) endpoints.pop_back();
            // The endpoints token is the last whitespace-separated field before the state.
            auto last_space = endpoints.find_last_of(" \t");
            std::string name_field = (last_space == std::string::npos) ? endpoints : endpoints.substr(last_space + 1);

            std::string local_part = name_field;
            std::string remote_part;
            if (auto arrow = name_field.find("->"); arrow != std::string::npos) {
                local_part = name_field.substr(0, arrow);
                remote_part = name_field.substr(arrow + 2);
            }

            auto split_host_port = [](const std::string& s, std::string& host, int& port) {
                auto colon = s.find_last_of(':');
                if (colon == std::string::npos) { host = s; port = 0; return; }
                host = s.substr(0, colon);
                try { port = std::stoi(s.substr(colon + 1)); } catch (...) { port = 0; }
            };

            tcp_connection_info info;
            split_host_port(local_part, info.local_address, info.local_port);
            if (!remote_part.empty()) split_host_port(remote_part, info.remote_address, info.remote_port);
            info.state = state;
            info.direction = classify_tcp_direction(state, info.local_port);
            result.push_back(std::move(info));
        }
        return result;
    }

    process_stats sample_stats_macos(const process_host::running_process& rp) {
        process_stats stats;
        pid_t pid = rp.os_pid;

        proc_taskallinfo task_info{};
        if (proc_pidinfo(pid, PROC_PIDTASKALLINFO, 0, &task_info, sizeof(task_info)) > 0) {
            stats.thread_count = static_cast<int>(task_info.ptinfo.pti_threadnum);
            stats.memory_bytes = static_cast<long long>(task_info.ptinfo.pti_resident_size);
        }

        int fd_buf_size = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, nullptr, 0);
        if (fd_buf_size > 0) {
            stats.open_handle_count = fd_buf_size / static_cast<int>(sizeof(proc_fdinfo));
        }

        stats.tcp_connections = sample_tcp_connections_macos(pid);

        int num_pids = proc_listallpids(nullptr, 0);
        if (num_pids > 0) {
            std::vector<pid_t> pids(static_cast<size_t>(num_pids) + 32);
            int actual = proc_listallpids(pids.data(), static_cast<int>(pids.size() * sizeof(pid_t)));
            int child_count = 0;
            for (int i = 0; i < actual; ++i) {
                if (pids[static_cast<size_t>(i)] == pid) continue;
                proc_bsdinfo bsd_info{};
                if (proc_pidinfo(pids[static_cast<size_t>(i)], PROC_PIDTBSDINFO, 0, &bsd_info, sizeof(bsd_info)) > 0) {
                    if (static_cast<pid_t>(bsd_info.pbi_ppid) == pid) child_count++;
                }
            }
            stats.subprocess_count = child_count;
        }

        return stats;
    }
    #else
    std::string linux_tcp_state_to_string(int code) {
        switch (code) {
            case 0x01: return "ESTABLISHED";
            case 0x02: return "SYN_SENT";
            case 0x03: return "SYN_RECV";
            case 0x04: return "FIN_WAIT1";
            case 0x05: return "FIN_WAIT2";
            case 0x06: return "TIME_WAIT";
            case 0x07: return "CLOSE";
            case 0x08: return "CLOSE_WAIT";
            case 0x09: return "LAST_ACK";
            case 0x0A: return "LISTEN";
            case 0x0B: return "CLOSING";
            default: return "UNKNOWN";
        }
    }

    // /proc/net/tcp stores each address as the raw 32-bit word's hex bytes without
    // correcting for host endianness, so on little-endian x86/ARM the byte order
    // needs reversing to read as a normal dotted-decimal address.
    std::string decode_linux_ipv4_hex(const std::string& hex) {
        if (hex.size() != 8) return "";
        unsigned b[4];
        for (int i = 0; i < 4; ++i) {
            b[i] = static_cast<unsigned>(std::stoul(hex.substr(static_cast<size_t>(i) * 2, 2), nullptr, 16));
        }
        return std::format("{}.{}.{}.{}", b[3], b[2], b[1], b[0]);
    }

    std::unordered_set<std::string> collect_socket_inodes_linux(pid_t pid) {
        std::unordered_set<std::string> inodes;
        std::error_code ec;
        auto fd_dir = std::filesystem::path("/proc") / std::to_string(pid) / "fd";
        std::filesystem::directory_iterator it(fd_dir, std::filesystem::directory_options::skip_permission_denied, ec);
        std::filesystem::directory_iterator end;
        while (!ec && it != end) {
            std::error_code link_ec;
            auto target = std::filesystem::read_symlink(it->path(), link_ec);
            if (!link_ec) {
                std::string t = target.string();
                if (t.starts_with("socket:[") && !t.empty() && t.back() == ']') {
                    inodes.insert(t.substr(8, t.size() - 9));
                }
            }
            it.increment(ec);
        }
        return inodes;
    }

    // IPv4 only for v1 (/proc/net/tcp); /proc/net/tcp6 addresses need a different decode.
    std::vector<tcp_connection_info> sample_tcp_connections_linux(pid_t pid) {
        std::vector<tcp_connection_info> result;
        auto inodes = collect_socket_inodes_linux(pid);
        if (inodes.empty()) return result;

        std::ifstream file("/proc/net/tcp");
        if (!file) return result;
        std::string line;
        std::getline(file, line); // header row
        while (std::getline(file, line)) {
            std::istringstream ss(line);
            std::string sl, local, remote, st, tx_rx, tr_tm, retrnsmt, uid_field, timeout_field, inode_str;
            if (!(ss >> sl >> local >> remote >> st >> tx_rx >> tr_tm >> retrnsmt >> uid_field >> timeout_field >> inode_str)) {
                continue;
            }
            if (!inodes.contains(inode_str)) continue;

            auto local_colon = local.find(':');
            auto remote_colon = remote.find(':');
            if (local_colon == std::string::npos || remote_colon == std::string::npos) continue;

            tcp_connection_info info;
            info.local_address = decode_linux_ipv4_hex(local.substr(0, local_colon));
            info.local_port = static_cast<int>(std::stoul(local.substr(local_colon + 1), nullptr, 16));
            info.remote_address = decode_linux_ipv4_hex(remote.substr(0, remote_colon));
            info.remote_port = static_cast<int>(std::stoul(remote.substr(remote_colon + 1), nullptr, 16));
            info.state = linux_tcp_state_to_string(static_cast<int>(std::stoul(st, nullptr, 16)));
            info.direction = classify_tcp_direction(info.state, info.local_port);
            result.push_back(std::move(info));
        }
        return result;
    }

    process_stats sample_stats_linux(const process_host::running_process& rp) {
        process_stats stats;
        pid_t pid = rp.os_pid;

        std::ifstream status_file(std::filesystem::path("/proc") / std::to_string(pid) / "status");
        std::string status_line;
        while (status_file && std::getline(status_file, status_line)) {
            if (status_line.starts_with("VmRSS:")) {
                std::istringstream rss_stream(status_line.substr(6));
                long long kb = 0;
                if (rss_stream >> kb) stats.memory_bytes = kb * 1024;
                break;
            }
        }

        stats.tcp_connections = sample_tcp_connections_linux(pid);

        std::error_code ec;
        auto task_dir = std::filesystem::path("/proc") / std::to_string(pid) / "task";
        if (std::filesystem::exists(task_dir, ec)) {
            int count = 0;
            std::filesystem::directory_iterator it(task_dir, ec);
            std::filesystem::directory_iterator end;
            while (!ec && it != end) { count++; it.increment(ec); }
            stats.thread_count = count;
        }

        auto fd_dir = std::filesystem::path("/proc") / std::to_string(pid) / "fd";
        if (std::filesystem::exists(fd_dir, ec)) {
            int count = 0;
            std::filesystem::directory_iterator it(fd_dir, ec);
            std::filesystem::directory_iterator end;
            while (!ec && it != end) { count++; it.increment(ec); }
            stats.open_handle_count = count;
        }

        int child_count = 0;
        std::filesystem::directory_iterator it("/proc", std::filesystem::directory_options::skip_permission_denied, ec);
        std::filesystem::directory_iterator end;
        while (!ec && it != end) {
            const std::string fname = it->path().filename().string();
            bool numeric = !fname.empty() && std::all_of(fname.begin(), fname.end(), ::isdigit);
            if (numeric) {
                std::ifstream stat_file(it->path() / "stat");
                std::string line;
                if (stat_file && std::getline(stat_file, line)) {
                    // Field 4 (ppid) comes after the ')' that closes the process name.
                    auto close_paren = line.rfind(')');
                    if (close_paren != std::string::npos) {
                        std::istringstream rest(line.substr(close_paren + 2));
                        std::string state_field;
                        pid_t ppid = -1;
                        rest >> state_field >> ppid;
                        if (ppid == pid) child_count++;
                    }
                }
            }
            it.increment(ec);
        }
        stats.subprocess_count = child_count;

        return stats;
    }
    #endif

    process_stats sample_stats_posix(const process_host::running_process& rp) {
    #if defined(__APPLE__)
        return sample_stats_macos(rp);
    #else
        return sample_stats_linux(rp);
    #endif
    }
#endif

    void stderr_reader_loop(std::shared_ptr<process_host::running_process> rp) {
        std::string partial;
        char buf[4096];
#ifdef _WIN32
        DWORD n = 0;
        while (rp->stderr_read && ReadFile(rp->stderr_read, buf, sizeof(buf), &n, nullptr) && n > 0) {
            append_chunk(rp->data_mutex, rp->stderr_lines, partial, buf, n, kMaxStderrLines);
        }
        if (rp->stderr_read) { CloseHandle(rp->stderr_read); rp->stderr_read = nullptr; }
#else
        ssize_t n;
        while (rp->stderr_fd >= 0 && (n = read(rp->stderr_fd, buf, sizeof(buf))) > 0) {
            append_chunk(rp->data_mutex, rp->stderr_lines, partial, buf, static_cast<size_t>(n), kMaxStderrLines);
        }
        if (rp->stderr_fd >= 0) { close(rp->stderr_fd); rp->stderr_fd = -1; }
#endif
        if (!partial.empty()) {
            std::lock_guard<std::mutex> lock(rp->data_mutex);
            rp->stderr_lines.push_back(partial);
        }
    }

    void watcher_loop(std::shared_ptr<process_host::running_process> rp) {
        int tick = 0;
        constexpr int ticks_per_stats_sample = 5; // ~1s at the 200ms poll interval below
        while (!rp->stop_requested.load()) {
            bool exited = false;
            int code = -1;
#ifdef _WIN32
            DWORD wait_result = WaitForSingleObject(rp->process_handle, 200);
            if (wait_result == WAIT_OBJECT_0) {
                DWORD exit_code = 0;
                GetExitCodeProcess(rp->process_handle, &exit_code);
                code = static_cast<int>(exit_code);
                exited = true;
            }
#else
            int status = 0;
            pid_t res = waitpid(rp->os_pid, &status, WNOHANG);
            if (res == rp->os_pid) {
                code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                exited = true;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
#endif
            if (exited) {
                std::lock_guard<std::mutex> lock(rp->data_mutex);
                rp->exit_code = code;
                rp->state.store(process_run_state::exited);
                break;
            }

            if (++tick >= ticks_per_stats_sample) {
                tick = 0;
#ifdef _WIN32
                process_stats sampled = sample_stats_windows(*rp);
#else
                process_stats sampled = sample_stats_posix(*rp);
#endif
                std::lock_guard<std::mutex> lock(rp->data_mutex);
                rp->stats = sampled;
            }
        }
    }
} // namespace

process_host& process_host::instance() {
    static process_host host;
    return host;
}

process_host::~process_host() = default;

std::string process_host::start(const process_definition& def) {
    auto rp = std::make_shared<running_process>();
    rp->run_id = generate_run_id(def.id);
    rp->definition_id = def.id;
    rp->definition_name = def.name;

    std::string err;
#ifdef _WIN32
    bool ok = spawn_windows(*rp, def, err);
#else
    bool ok = spawn_posix(*rp, def, err);
#endif

    if (!ok) {
        rp->state.store(process_run_state::failed_to_start);
        rp->start_error = err;
        PROCHOST_ERROR_FMT("Failed to start '{}' ({}): {}", def.name, def.executable_path, err);
    } else {
        PROCHOST_INFO_FMT("Started '{}' as run {} (pid {})", def.name, rp->run_id, rp->pid.load());
        rp->stderr_thread = std::thread(stderr_reader_loop, rp);
        rp->watcher_thread = std::thread(watcher_loop, rp);
    }

    std::string run_id = rp->run_id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        runs_[run_id] = rp;
        latest_run_by_definition_[def.id] = run_id;
    }
    return run_id;
}

std::optional<std::string> process_host::latest_run_id(int64_t definition_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = latest_run_by_definition_.find(definition_id);
    if (it == latest_run_by_definition_.end()) return std::nullopt;
    return it->second;
}

bool process_host::has_active_run(int64_t definition_id) const {
    auto run_id = latest_run_id(definition_id);
    if (!run_id) return false;
    auto snap = snapshot(*run_id);
    return snap && snap->state == process_run_state::running;
}

std::optional<process_run_snapshot> process_host::snapshot(const std::string& run_id) const {
    std::shared_ptr<running_process> rp;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = runs_.find(run_id);
        if (it == runs_.end()) return std::nullopt;
        rp = it->second;
    }

    process_run_snapshot result;
    result.run_id = rp->run_id;
    result.definition_id = rp->definition_id;
    result.definition_name = rp->definition_name;
    result.state = rp->state.load();
    result.pid = rp->pid.load();
    result.start_error = rp->start_error;

    std::lock_guard<std::mutex> lock(rp->data_mutex);
    result.stats = rp->stats;
    result.exit_code = rp->exit_code;
    result.stderr_lines = rp->stderr_lines;
    return result;
}

void process_host::kill(const std::string& run_id) {
    std::shared_ptr<running_process> rp;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = runs_.find(run_id);
        if (it == runs_.end()) return;
        rp = it->second;
    }
    if (rp->state.load() != process_run_state::running) return;

#ifdef _WIN32
    if (rp->job_handle) TerminateJobObject(rp->job_handle, 1);
    else if (rp->process_handle) TerminateProcess(rp->process_handle, 1);
#else
    if (rp->os_pid > 0) killpg(rp->os_pid, SIGKILL);
#endif
    PROCHOST_INFO_FMT("Kill requested for run {}", run_id);
}

} // namespace rouen::hosts
