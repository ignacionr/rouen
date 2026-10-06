#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <mutex>
#include <optional>
#include <algorithm>
#include <regex>
#include "glaze_include.hpp"
#include "process_helper.hpp"

namespace rouen::helpers {

struct CompileCommandEntry {
    std::string directory;
    std::string command;
    std::string file;
    std::vector<std::string> arguments;

    struct glaze {
        using T = CompileCommandEntry;
        static constexpr auto value = glz::object(
            "directory", &T::directory,
            "command", &T::command,
            "file", &T::file,
            "arguments", &T::arguments
        );
    };
};

#if defined(__APPLE__)
inline constexpr const char* CURRENT_HOST_PLATFORM = "macos";
#elif defined(_WIN32)
inline constexpr const char* CURRENT_HOST_PLATFORM = "windows";
#else
inline constexpr const char* CURRENT_HOST_PLATFORM = "linux";
#endif

struct DiscoveredToolchain {
    std::string platform{CURRENT_HOST_PLATFORM}; // "macos", "linux", "windows"
    bool has_nix{false};
    bool is_nix_workspace{false};
    bool has_cmake{false};
    bool has_ninja{false};
    bool has_clang{false};
    bool has_gcc{false};
    bool has_msvc{false};
    std::string cpp_compiler;         // "clang++", "g++", or "cl"
    std::string c_compiler;           // "clang", "gcc", or "cl"
    std::string build_system;         // "ninja", "cmake", or "make"
    std::string compile_commands_path;
    size_t compile_commands_count{0};
    bool has_cmake_presets{false};
    bool has_build_ninja{false};
    std::string build_directory;

    struct glaze {
        using T = DiscoveredToolchain;
        static constexpr auto value = glz::object(
            "platform", &T::platform,
            "has_nix", &T::has_nix,
            "is_nix_workspace", &T::is_nix_workspace,
            "has_cmake", &T::has_cmake,
            "has_ninja", &T::has_ninja,
            "has_clang", &T::has_clang,
            "has_gcc", &T::has_gcc,
            "has_msvc", &T::has_msvc,
            "cpp_compiler", &T::cpp_compiler,
            "c_compiler", &T::c_compiler,
            "build_system", &T::build_system,
            "compile_commands_path", &T::compile_commands_path,
            "compile_commands_count", &T::compile_commands_count,
            "has_cmake_presets", &T::has_cmake_presets,
            "has_build_ninja", &T::has_build_ninja,
            "build_directory", &T::build_directory
        );
    };
};

class ToolchainService {
public:
    static ToolchainService& instance() {
        static ToolchainService s_instance;
        return s_instance;
    }

    /**
     * Check whether an executable command is reachable in PATH.
     */
    static bool has_command(const std::string& cmd_name) {
        if (cmd_name.empty()) return false;
        std::string check_cmd = std::format("which {} 2>/dev/null", cmd_name);
        std::string output = ProcessHelper::executeCommand(check_cmd);
        // Trim whitespace
        while (!output.empty() && (output.back() == '\n' || output.back() == '\r' || output.back() == ' ')) {
            output.pop_back();
        }
        return !output.empty() && std::filesystem::exists(output);
    }

    /**
     * Discover available toolchains and environment attributes for a workspace.
     */
    DiscoveredToolchain discover_toolchain(const std::string& workspace_path = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string root = workspace_path.empty() ? std::filesystem::current_path().string() : workspace_path;
        root = ProcessHelper::expandTilde(root);

        DiscoveredToolchain tc;
        tc.has_nix = has_command("nix");
        
        // Check for nix flake / shell markers
        std::error_code ec;
        if (std::filesystem::exists(std::filesystem::path(root) / "flake.nix", ec) ||
            std::filesystem::exists(std::filesystem::path(root) / "shell.nix", ec)) {
            tc.is_nix_workspace = true;
        }

        // Compilers
        tc.has_clang = has_command("clang++");
        tc.has_gcc = has_command("g++");
        tc.has_msvc = has_command("cl");

        if (tc.has_clang) {
            tc.cpp_compiler = "clang++";
            tc.c_compiler = "clang";
        } else if (tc.has_gcc) {
            tc.cpp_compiler = "g++";
            tc.c_compiler = "gcc";
        } else if (tc.has_msvc) {
            tc.cpp_compiler = "cl";
            tc.c_compiler = "cl";
        }

        // Build systems
        tc.has_ninja = has_command("ninja");
        tc.has_cmake = has_command("cmake");

        if (tc.has_ninja) {
            tc.build_system = "ninja";
        } else if (tc.has_cmake) {
            tc.build_system = "cmake";
        } else if (has_command("make")) {
            tc.build_system = "make";
        }

        // Project markers
        if (std::filesystem::exists(std::filesystem::path(root) / "CMakePresets.json", ec)) {
            tc.has_cmake_presets = true;
        }
        if (std::filesystem::exists(std::filesystem::path(root) / "build" / "build.ninja", ec) ||
            std::filesystem::exists(std::filesystem::path(root) / "build.ninja", ec)) {
            tc.has_build_ninja = true;
        }
        if (std::filesystem::exists(std::filesystem::path(root) / "build", ec)) {
            tc.build_directory = "build";
        }

        // Look for compile_commands.json
        std::string cc_path = find_compile_commands(root);
        if (!cc_path.empty()) {
            tc.compile_commands_path = cc_path;
            load_compile_commands_locked(cc_path);
            tc.compile_commands_count = compile_commands_.size();
        }

        cached_toolchain_ = tc;
        return tc;
    }

    /**
     * Find compile_commands.json in standard locations.
     */
    static std::string find_compile_commands(const std::string& workspace_path) {
        std::filesystem::path ws = workspace_path.empty() ? std::filesystem::current_path() : std::filesystem::path(workspace_path);
        std::vector<std::filesystem::path> candidates = {
            ws / "build" / "compile_commands.json",
            ws / "compile_commands.json",
            ws / "out" / "compile_commands.json",
            std::filesystem::current_path() / "build" / "compile_commands.json",
            std::filesystem::current_path() / "compile_commands.json"
        };

        std::error_code ec;
        for (const auto& cand : candidates) {
            if (std::filesystem::exists(cand, ec) && std::filesystem::file_size(cand, ec) > 0) {
                return std::filesystem::canonical(cand, ec).string();
            }
        }
        return "";
    }

    /**
     * Load and index compile_commands.json.
     */
    bool load_compile_commands(const std::string& filepath) {
        std::lock_guard<std::mutex> lock(mutex_);
        return load_compile_commands_locked(filepath);
    }

    /**
     * Lookup compilation entry for a given file.
     */
    std::optional<CompileCommandEntry> get_compile_command(const std::string& file_path, const std::string& workspace_dir = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        if (compile_commands_.empty()) {
            std::string search_dir = !workspace_dir.empty() ? workspace_dir : std::filesystem::path(file_path).parent_path().string();
            std::string cc_path = find_compile_commands(search_dir);
            if (!cc_path.empty()) {
                load_compile_commands_locked(cc_path);
            }
        }

        std::error_code ec;
        std::string canonical_target;
        if (std::filesystem::exists(file_path, ec)) {
            canonical_target = std::filesystem::canonical(file_path, ec).string();
        } else {
            canonical_target = file_path;
        }

        auto it = compile_commands_.find(canonical_target);
        if (it != compile_commands_.end()) {
            return it->second;
        }

        // Fallback: match by filename if path differs
        std::string filename = std::filesystem::path(file_path).filename().string();
        for (const auto& [path, entry] : compile_commands_) {
            if (std::filesystem::path(path).filename().string() == filename) {
                return entry;
            }
        }

        return std::nullopt;
    }

    /**
     * Extract syntax check command flags for a file.
     * Supports Clang & GCC (-fsyntax-only) and MSVC (/Zs).
     */
    std::string extract_syntax_command(const std::string& file_path, const std::string& workspace_dir = "") {
        auto entry_opt = get_compile_command(file_path, workspace_dir);
        if (entry_opt.has_value()) {
            const auto& entry = *entry_opt;
            std::string cmd = entry.command;

            // If command is empty but arguments are present, join arguments
            if (cmd.empty() && !entry.arguments.empty()) {
                std::ostringstream ss;
                for (size_t i = 0; i < entry.arguments.size(); ++i) {
                    if (i > 0) ss << " ";
                    ss << "\"" << entry.arguments[i] << "\"";
                }
                cmd = ss.str();
            }

            if (!cmd.empty()) {
                if (cmd.find("cl.exe") != std::string::npos || cmd.find("cl ") != std::string::npos) {
                    // MSVC: strip /Fo and replace /c with /Zs
                    std::regex fo_regex(R"(/Fo\S+)");
                    cmd = std::regex_replace(cmd, fo_regex, "");
                    std::regex c_msvc_regex(R"(\s/c(?:\s|$))");
                    cmd = std::regex_replace(cmd, c_msvc_regex, " ");
                    if (cmd.find("/Zs") == std::string::npos) {
                        cmd += " /Zs";
                    }
                    return cmd;
                }

                // Clang / GCC: Strip `-o <output_file>`
                std::regex o_regex(R"(-o\s+["']?[^\s"']+["']?)");
                cmd = std::regex_replace(cmd, o_regex, "");

                // Strip `-c`
                std::regex c_regex(R"(\s-c(?:\s|$))");
                cmd = std::regex_replace(cmd, c_regex, " ");

                // Append -fsyntax-only if not present
                if (cmd.find("-fsyntax-only") == std::string::npos) {
                    cmd += " -fsyntax-only";
                }

                return cmd;
            }
        }

        // Fallback: construct standard default C++ command
        std::string compiler = cached_toolchain_.cpp_compiler.empty() ? "clang++" : cached_toolchain_.cpp_compiler;
        std::string root = workspace_dir.empty() ? std::filesystem::current_path().string() : workspace_dir;

        // Auto-detect C++ standard (check CMakeLists.txt or default to C++23 if supported)
        std::string cpp_std = "c++20";
        std::filesystem::path cmakelists = std::filesystem::path(root) / "CMakeLists.txt";
        if (!std::filesystem::exists(cmakelists)) {
            cmakelists = std::filesystem::path(file_path).parent_path() / "CMakeLists.txt";
        }
        if (std::filesystem::exists(cmakelists)) {
            std::ifstream cfile(cmakelists);
            std::string content((std::istreambuf_iterator<char>(cfile)), std::istreambuf_iterator<char>());
            if (content.find("23") != std::string::npos || content.find("c++23") != std::string::npos || content.find("CXX_STANDARD 23") != std::string::npos) {
                cpp_std = "c++23";
            }
        }

        if (compiler == "cl") {
            return std::format(
                "cl.exe /nologo /Zs /std:{} /EHsc /I\"{}\" /I\"{}/src\" /I\"{}/external\" \"{}\"",
                (cpp_std == "c++23" ? "c++latest" : "c++20"), root, root, root, file_path
            );
        }

        std::string fallback_cmd = std::format(
            "{} -fsyntax-only -std={} -I\"{}\" -I\"{}/src\" -I\"{}/external\" \"{}\"",
            compiler, cpp_std, root, root, root, file_path
        );

        return fallback_cmd;
    }

    /**
     * Wrap command with `nix develop --command` if workspace contains flake.nix.
     */
    std::string wrap_nix_if_needed(const std::string& command, const std::string& workspace_path = "") {
        std::string root = workspace_path.empty() ? std::filesystem::current_path().string() : workspace_path;
        root = ProcessHelper::expandTilde(root);

        std::error_code ec;
        if (has_command("nix") && std::filesystem::exists(std::filesystem::path(root) / "flake.nix", ec)) {
            return std::format("nix develop --command sh -c '{}'", command);
        }
        return command;
    }

private:
    ToolchainService() = default;

    bool load_compile_commands_locked(const std::string& filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) return false;

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (content.empty()) return false;

        std::vector<CompileCommandEntry> entries;
        auto err = glz::read_json(entries, content);
        if (err) {
            return false;
        }

        compile_commands_.clear();
        std::error_code ec;
        for (auto& e : entries) {
            std::string canonical_file;
            if (std::filesystem::path(e.file).is_absolute()) {
                canonical_file = std::filesystem::canonical(e.file, ec).string();
                if (canonical_file.empty()) canonical_file = e.file;
            } else {
                auto combined = std::filesystem::path(e.directory) / e.file;
                canonical_file = std::filesystem::canonical(combined, ec).string();
                if (canonical_file.empty()) canonical_file = combined.string();
            }
            compile_commands_[canonical_file] = std::move(e);
        }
        loaded_cc_path_ = filepath;
        return true;
    }

    std::mutex mutex_;
    DiscoveredToolchain cached_toolchain_;
    std::string loaded_cc_path_;
    std::unordered_map<std::string, CompileCommandEntry> compile_commands_;
};

} // namespace rouen::helpers
