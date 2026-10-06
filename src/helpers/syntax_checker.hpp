#pragma once

#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <regex>
#include <sstream>
#include <optional>
#include <algorithm>
#include "glaze_include.hpp"
#include "process_helper.hpp"
#include "toolchain_service.hpp"
#include "../registrar.hpp"

namespace rouen::helpers {

struct Diagnostic {
    std::string file;
    int line{1};
    int column{1};
    std::string severity{"error"}; // "error", "warning", "note"
    std::string message;

    struct glaze {
        using T = Diagnostic;
        static constexpr auto value = glz::object(
            "file", &T::file,
            "line", &T::line,
            "column", &T::column,
            "severity", &T::severity,
            "message", &T::message
        );
    };
};

struct SyntaxCheckResult {
    std::string file_path;
    bool success{true};
    size_t error_count{0};
    size_t warning_count{0};
    std::vector<Diagnostic> diagnostics;
    std::string raw_output;

    struct glaze {
        using T = SyntaxCheckResult;
        static constexpr auto value = glz::object(
            "file_path", &T::file_path,
            "success", &T::success,
            "error_count", &T::error_count,
            "warning_count", &T::warning_count,
            "diagnostics", &T::diagnostics,
            "raw_output", &T::raw_output
        );
    };
};

class SyntaxChecker {
public:
    static SyntaxChecker& instance() {
        static SyntaxChecker s_instance;
        return s_instance;
    }

    /**
     * Parse raw compiler/linter output into a structured list of Diagnostics.
     */
    static std::vector<Diagnostic> parse_compiler_output(const std::string& output, const std::string& fallback_file = "") {
        std::vector<Diagnostic> diagnostics;
        if (output.empty()) return diagnostics;

        std::istringstream stream(output);
        std::string line;

        // Regex 1: GCC / Clang / AppleClang standard format
        // Example: /path/file.cpp:42:15: error: use of undeclared identifier 'x'
        // Example: file.cpp:42: error: message
        static const std::regex gcc_clang_regex(
            R"(^(.+?):(\d+):(?:(\d+):)?\s*(fatal error|error|warning|note):\s*(.+)$)",
            std::regex::optimize
        );

        // Regex 2: MSVC format
        // Example: C:\path\file.cpp(42,15): error C2065: 'x': undeclared identifier
        // Example: 1>C:\path\file.cpp(42,15): error C2065: ...
        static const std::regex msvc_regex(
            R"(^(?:\d+>\s*)?(.+?)\((\d+)(?:,(\d+))?\):\s*(fatal error|error|warning|note)\s+([A-Za-z0-9]+:\s*.+)$)",
            std::regex::optimize
        );

        // Regex 3: Python syntax error header
        // Example: File "script.py", line 42
        static const std::regex py_file_regex(
            R"(^\s*File\s+\"([^\"]+)\",\s+line\s+(\d+))",
            std::regex::optimize
        );

        std::string current_py_file;
        int current_py_line = -1;

        while (std::getline(stream, line)) {
            // Trim trailing \r if present
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) continue;

            std::smatch match;

            // Test GCC/Clang
            if (std::regex_search(line, match, gcc_clang_regex)) {
                Diagnostic diag;
                diag.file = match[1].str();
                diag.line = std::max(1, std::stoi(match[2].str()));
                diag.column = match[3].matched ? std::max(1, std::stoi(match[3].str())) : 1;
                
                std::string sev = match[4].str();
                if (sev == "fatal error") sev = "error";
                diag.severity = sev;
                diag.message = match[5].str();

                diagnostics.push_back(diag);
                continue;
            }

            // Test MSVC
            if (std::regex_search(line, match, msvc_regex)) {
                Diagnostic diag;
                diag.file = match[1].str();
                diag.line = std::max(1, std::stoi(match[2].str()));
                diag.column = match[3].matched ? std::max(1, std::stoi(match[3].str())) : 1;

                std::string sev = match[4].str();
                if (sev == "fatal error") sev = "error";
                diag.severity = sev;
                diag.message = match[5].str();

                diagnostics.push_back(diag);
                continue;
            }

            // Test Python
            if (std::regex_search(line, match, py_file_regex)) {
                current_py_file = match[1].str();
                current_py_line = std::max(1, std::stoi(match[2].str()));
                continue;
            }

            if (current_py_line > 0 && line.starts_with("SyntaxError:")) {
                Diagnostic diag;
                diag.file = current_py_file.empty() ? fallback_file : current_py_file;
                diag.line = current_py_line;
                diag.column = 1;
                diag.severity = "error";
                diag.message = line.substr(12); // strip "SyntaxError:"
                while (!diag.message.empty() && diag.message.front() == ' ') {
                    diag.message.erase(diag.message.begin());
                }
                diagnostics.push_back(diag);
                current_py_line = -1;
                current_py_file.clear();
                continue;
            }
        }

        // If no structured matches were found but output indicates an error, create a fallback diagnostic
        if (diagnostics.empty() && !output.empty()) {
            std::string lower = output;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.find("error:") != std::string::npos || lower.find("syntaxerror") != std::string::npos) {
                Diagnostic diag;
                diag.file = fallback_file;
                diag.line = 1;
                diag.column = 1;
                diag.severity = "error";
                diag.message = output.substr(0, std::min<size_t>(output.size(), 200));
                diagnostics.push_back(diag);
            }
        }

        return diagnostics;
    }

    /**
     * Convert diagnostics into TextEditor ErrorMarkers map (1-based line number -> message).
     */
    static std::map<int, std::string> to_error_markers(
        const std::vector<Diagnostic>& diagnostics,
        const std::string& target_file = ""
    ) {
        std::map<int, std::string> markers;
        std::string target_name = target_file.empty() ? "" : std::filesystem::path(target_file).filename().string();

        for (const auto& diag : diagnostics) {
            if (!target_name.empty()) {
                std::string diag_name = std::filesystem::path(diag.file).filename().string();
                if (!diag_name.empty() && diag_name != target_name) {
                    continue;
                }
            }

            // Only mark errors and warnings
            if (diag.severity != "error" && diag.severity != "warning") {
                continue;
            }

            std::string prefix = (diag.severity == "warning") ? "[Warning] " : "[Error] ";
            std::string entry = prefix + diag.message;

            auto it = markers.find(diag.line);
            if (it == markers.end()) {
                markers[diag.line] = entry;
            } else {
                it->second += "\n" + entry;
            }
        }

        return markers;
    }

    /**
     * Execute fast syntax check on a single file.
     */
    SyntaxCheckResult check_file(const std::string& file_path, const std::string& workspace_dir = "") {
        SyntaxCheckResult result;
        result.file_path = file_path;

        std::string resolved_path = ProcessHelper::expandTilde(file_path);
        std::string ws = workspace_dir.empty() ? std::filesystem::current_path().string() : workspace_dir;
        ws = ProcessHelper::expandTilde(ws);

        std::filesystem::path p(resolved_path);
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        std::string cmd;
        if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c" || ext == ".h" || ext == ".hpp" || ext == ".cppm") {
            cmd = ToolchainService::instance().extract_syntax_command(resolved_path, ws);
            cmd = ToolchainService::instance().wrap_nix_if_needed(cmd, ws);
        } else if (ext == ".py") {
            cmd = std::format("python3 -m py_compile \"{}\"", resolved_path);
        } else if (ext == ".js" || ext == ".mjs" || ext == ".cjs") {
            cmd = std::format("node --check \"{}\"", resolved_path);
        } else if (ext == ".sh" || ext == ".bash" || ext == ".zsh") {
            cmd = std::format("bash -n \"{}\"", resolved_path);
        } else if (ext == ".json") {
            cmd = std::format("python3 -m json.tool \"{}\" > /dev/null", resolved_path);
        } else {
            // Unsupported file type for direct syntax-only check
            result.success = true;
            result.raw_output = "No syntax checker configured for extension: " + ext;
            return result;
        }

        // Execute command redirecting stderr to stdout
        std::string full_cmd = cmd + " 2>&1";
        std::string output = ProcessHelper::executeCommandInDirectory(ws, full_cmd);
        result.raw_output = output;

        // Parse diagnostics
        result.diagnostics = parse_compiler_output(output, resolved_path);
        for (const auto& d : result.diagnostics) {
            if (d.severity == "error") {
                result.error_count++;
            } else if (d.severity == "warning") {
                result.warning_count++;
            }
        }

        result.success = (result.error_count == 0);

        // Sync with active editor card if this file is currently open
        sync_to_active_editor(resolved_path, result.diagnostics);

        return result;
    }

    /**
     * Push error markers to active editor card if file matches.
     */
    static bool sync_to_active_editor(const std::string& file_path, const std::vector<Diagnostic>& diagnostics) {
        try {
            auto get_active = registrar::get<std::function<std::string()>>("editor_get_active_file");
            if (!get_active || !*get_active) return false;

            std::string active_file = (*get_active)();
            if (active_file.empty()) return false;

            std::string f1 = std::filesystem::path(active_file).filename().string();
            std::string f2 = std::filesystem::path(file_path).filename().string();
            if (f1 != f2) return false;

            auto markers = to_error_markers(diagnostics, active_file);
            auto set_markers = registrar::get<std::function<void(const std::map<int, std::string>&)>>("editor_set_error_markers");
            if (set_markers && *set_markers) {
                (*set_markers)(markers);
                return true;
            }
        } catch (...) {}
        return false;
    }

private:
    SyntaxChecker() = default;
};

} // namespace rouen::helpers
