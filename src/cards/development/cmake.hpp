#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#endif

#include "../interface/card.hpp"

namespace rouen::cards {

    struct cmake_card : public card {
        explicit cmake_card(std::string_view path);

        [[nodiscard]] std::string get_uri() const override;
        bool render() override;

        void read_cmake_file();
        bool run_cmake_action(const std::string& action, const std::string& explanation);
        void cancel_running_action();

    private:
        std::string path_;
        std::filesystem::path build_dir_;
        std::string project_name_;
        std::string project_version_;
        std::vector<std::string> targets_;
        std::string error_message_;
        std::string last_output_;
        std::string last_cmd_;
        std::string last_action_;
        bool cmd_running_ = false;
        std::chrono::steady_clock::time_point start_time_;
#ifdef _WIN32
        DWORD process_pid_ = 0;
#else
        pid_t process_pid_ = 0;
#endif
    };

} // namespace rouen::cards
