#pragma once

#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <cstdint>

namespace rouen::models::productivity {
    struct process_definition;
}

namespace rouen::hosts {

    // A single TCP connection or listening socket owned by the monitored process.
    struct tcp_connection_info {
        std::string local_address;
        int local_port{0};
        std::string remote_address;
        int remote_port{0};
        std::string state;      // e.g. "LISTEN", "ESTABLISHED", "TIME_WAIT"
        std::string direction;  // "Listening", "Inbound", or "Outbound" (heuristic, see process_host.cpp)
    };

    struct process_stats {
        std::optional<int> thread_count;
        std::optional<int> subprocess_count;
        std::optional<long long> open_handle_count;
        std::optional<long long> memory_bytes; // resident/working-set memory footprint
        std::vector<tcp_connection_info> tcp_connections;
    };

    enum class process_run_state {
        running,
        exited,
        failed_to_start,
    };

    // Immutable snapshot handed to cards; never holds OS handles.
    struct process_run_snapshot {
        std::string run_id;
        int64_t definition_id{0};
        std::string definition_name;
        process_run_state state{process_run_state::failed_to_start};
        long pid{0};
        process_stats stats;
        std::optional<int> exit_code;
        std::vector<std::string> stderr_lines;
        std::string start_error;
    };

    /**
     * Owns every spawned process for the lifetime of the application. Cards are pure
     * views over this host: closing a monitor card never touches the underlying
     * process, only the Kill action (or app shutdown) does.
     */
    class process_host {
    public:
        static process_host& instance();

        // Platform-specific handles; defined in process_host.cpp. Public so the free
        // functions implementing spawn/monitor logic there can name the type, even
        // though it is never meaningfully usable outside this translation unit.
        struct running_process;

        // Always spawns a brand-new run (never reuses/matches an existing one), returning its run_id.
        std::string start(const rouen::models::productivity::process_definition& def);

        // Most recently started run_id for a definition, if the host has one on record.
        std::optional<std::string> latest_run_id(int64_t definition_id) const;

        bool has_active_run(int64_t definition_id) const;

        std::optional<process_run_snapshot> snapshot(const std::string& run_id) const;

        void kill(const std::string& run_id);

        ~process_host();

    private:
        process_host() = default;
        process_host(const process_host&) = delete;
        process_host& operator=(const process_host&) = delete;

        mutable std::mutex mutex_;
        std::unordered_map<std::string, std::shared_ptr<running_process>> runs_;
        std::unordered_map<int64_t, std::string> latest_run_by_definition_;
    };

} // namespace rouen::hosts
