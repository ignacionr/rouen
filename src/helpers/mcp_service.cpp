// 1. Standard includes in alphabetic order
#include <algorithm>
#include <fstream>
#include <sstream>

// 2. Libraries used in the project, in alphabetic order
#include "glaze_include.hpp"

// 3. All other includes
#include "debug.hpp"
#include "mcp_service.hpp"
#include "process_helper.hpp"
#include "../registrar.hpp"

namespace rouen::helpers {

struct local_command_request {
    std::string command{};
    std::string working_directory{};
};

struct mcp_create_card_params {
    std::string uri;
    struct glaze {
        using T = mcp_create_card_params;
        static constexpr auto value = glz::object(
            "uri", &T::uri
        );
    };
};

mcp_service::mcp_service() {
    detect_system_info();

    // Register default run_local_command function associated with terminal
    function_definition run_cmd_def(
        "run_local_command",
        "Execute a local shell command and return combined stdout/stderr output. Supports any local command, including curl. Host system info: " + cached_system_info_,
        R"mcp({
            "type": "object",
            "properties": {
                "command": {
                    "type": "string",
                    "description": "Shell command to execute locally (for example: curl -sS http://127.0.0.1:8099/health)"
                },
                "working_directory": {
                    "type": "string",
                    "description": "Optional directory where the command should run"
                }
            },
            "required": ["command"]
        })mcp",
        [](const std::string& params) -> std::string {
            if (params.empty()) {
                return "Error: Missing params. Expected JSON with a non-empty 'command' field.";
            }

            local_command_request request{};
            auto parse_result = glz::read_json(request, params);
            if (parse_result || request.command.empty()) {
                return "Error: Invalid params. Expected JSON: {\"command\":\"...\",\"working_directory\":\"optional\"}.";
            }

            const std::string command_with_stderr = request.command + " 2>&1";
            if (!request.working_directory.empty()) {
                return ProcessHelper::executeCommandInDirectory(request.working_directory, command_with_stderr);
            }

            return ProcessHelper::executeCommand(command_with_stderr);
        },
        "terminal"
    );
    
    register_function("terminal", run_cmd_def);

    // Register global create_card function
    function_definition create_card_def(
        "create_card",
        "Create and add a new card to Rouen by its URI (e.g. 'pomodoro', 'terminal', 'git', 'calendar'). Use 'pomodoro' to open/create a Pomodoro timer card.",
        R"mcp({
            "type": "object",
            "properties": {
                "uri": {
                    "type": "string",
                    "description": "The URI of the card to create (e.g. 'pomodoro')"
                }
            },
            "required": ["uri"]
        })mcp",
        [](const std::string& params) -> std::string {
            if (params.empty()) {
                return R"({"status":"error","message":"Missing params"})";
            }
            
            mcp_create_card_params request{};
            auto parse_result = glz::read_json(request, params);
            if (parse_result || request.uri.empty()) {
                return R"({"status":"error","message":"Invalid params"})";
            }
            
            auto create_card_fn = registrar::get<std::function<void(std::string const&)>>("create_card");
            if (!create_card_fn) {
                return R"({"status":"error","message":"create_card service is not currently available"})";
            }
            
            (*create_card_fn)(request.uri);
            return R"({"status":"success","message":"Card created successfully"})";
        },
        "deck"
    );
    
    register_function("deck", create_card_def);
}

void mcp_service::register_function(const std::string& card_type, const function_definition& func) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Create a copy of the function with the card_type set
    function_definition func_copy(func.name, func.description, func.schema, func.handler, card_type);
    
    // Register in main function map
    functions_.emplace(func.name, std::move(func_copy));
    
    // Add to card function registry
    card_functions_[card_type].push_back(func.name);
    
    DEBUG_TRACE("MCP: Registered function '" + func.name + "' from card '" + card_type + "'");
}

void mcp_service::unregister_card_functions(const std::string& card_type) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto card_it = card_functions_.find(card_type);
    if (card_it == card_functions_.end()) {
        return;
    }
    
    // Remove all functions for this card type
    for (const auto& func_name : card_it->second) {
        functions_.erase(func_name);
        DEBUG_TRACE("MCP: Unregistered function '" + func_name + "' from card '" + card_type + "'");
    }
    
    // Remove card from registry
    card_functions_.erase(card_it);
}

std::vector<mcp_service::function_definition> mcp_service::get_available_functions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<function_definition> result;
    result.reserve(functions_.size());
    
    for (const auto& [name, func] : functions_) {
        result.push_back(func);
    }
    
    return result;
}

std::vector<mcp_service::function_definition> mcp_service::get_functions_for_card(const std::string& card_type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<function_definition> result;
    
    auto card_it = card_functions_.find(card_type);
    if (card_it == card_functions_.end()) {
        return result;
    }
    
    result.reserve(card_it->second.size());
    for (const auto& func_name : card_it->second) {
        auto func_it = functions_.find(func_name);
        if (func_it != functions_.end()) {
            result.push_back(func_it->second);
        }
    }
    
    return result;
}

mcp_service::execution_result mcp_service::execute_function(const std::string& name, const std::string& params) {
    function_definition func;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto func_it = functions_.find(name);
        if (func_it == functions_.end()) {
            return execution_result(false, "", "Function '" + name + "' not found");
        }
        func = func_it->second; // Safe copy under lock
    }
    
    // Validate parameters if schema is provided
    if (!func.schema.empty() && !validate_parameters(params, func.schema)) {
        return execution_result(false, "", "Invalid parameters for function '" + name + "'");
    }
    
    try {
        DEBUG_TRACE("MCP: Executing function '" + name + "' with params: " + params);
        std::string result = func.handler(params);
        DEBUG_TRACE("MCP: Function '" + name + "' completed successfully");
        return execution_result(true, std::move(result));
    } catch (const std::exception& e) {
        std::string error = "Error executing function '" + name + "': " + e.what();
        DEBUG_ERROR(error);
        return execution_result(false, "", std::move(error));
    } catch (...) {
        std::string error = "Unknown error executing function '" + name + "'";
        DEBUG_ERROR(error);
        return execution_result(false, "", std::move(error));
    }
}

bool mcp_service::has_function(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return functions_.find(name) != functions_.end();
}

std::string mcp_service::get_function_schema(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto func_it = functions_.find(name);
    if (func_it != functions_.end()) {
        return func_it->second.schema;
    }
    return "";
}

std::string mcp_service::get_functions_description() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (functions_.empty()) {
        return "No functions available.";
    }
    
    std::ostringstream ss;
    ss << "Available functions:\n";
    
    for (const auto& [name, func] : functions_) {
        ss << "- " << func.name << " (" << func.card_type << "): " << func.description << "\n";
    }
    
    return ss.str();
}

bool mcp_service::validate_parameters(const std::string& params, const std::string& schema) const {
    // Basic validation - just check if params is valid JSON
    // In a full implementation, we'd validate against the JSON schema
    (void)schema; // Suppress unused parameter warning - schema validation not yet implemented
    
    try {
        if (params.empty()) {
            return true; // Empty params are valid for functions that don't require them
        }
        
        // Try to parse as JSON
        auto json_obj = glz::read_json<glz::json_t>(params);
        if (!json_obj) {
            DEBUG_WARN("MCP: Failed to parse parameters as JSON: " + params);
            return false;
        }
        
        return true;
    } catch (const std::exception& e) {
        DEBUG_WARN("MCP: Parameter validation error: " + std::string(e.what()));
        return false;
    }
}

void mcp_service::detect_system_info() {
    std::string os_type;
    std::string os_version;
    
#if defined(__APPLE__)
    os_type = "macOS";
    std::string sw_vers_out = ProcessHelper::executeCommand("sw_vers -productVersion");
    if (!sw_vers_out.empty()) {
        sw_vers_out.erase(sw_vers_out.find_last_not_of(" \t\r\n") + 1);
        os_version = sw_vers_out;
    } else {
        os_version = "unknown";
    }
#elif defined(__linux__)
    os_type = "Linux";
    std::ifstream release_file("/etc/os-release");
    if (release_file.is_open()) {
        std::string line;
        while (std::getline(release_file, line)) {
            if (line.starts_with("PRETTY_NAME=")) {
                std::string pretty = line.substr(12);
                if (pretty.size() >= 2 && pretty.front() == '"' && pretty.back() == '"') {
                    pretty = pretty.substr(1, pretty.size() - 2);
                }
                os_version = pretty;
                break;
            }
        }
    }
    if (os_version.empty()) {
        std::string uname_out = ProcessHelper::executeCommand("uname -r");
        if (!uname_out.empty()) {
            uname_out.erase(uname_out.find_last_not_of(" \t\r\n") + 1);
            os_version = uname_out;
        } else {
            os_version = "unknown";
        }
    }
#elif defined(_WIN32)
    os_type = "Windows";
    std::string ver_out = ProcessHelper::executeCommand("ver");
    if (!ver_out.empty()) {
        ver_out.erase(0, ver_out.find_first_not_of(" \t\r\n"));
        ver_out.erase(ver_out.find_last_not_of(" \t\r\n") + 1);
        os_version = ver_out;
    } else {
        os_version = "unknown";
    }
#else
    os_type = "Unknown OS";
    os_version = "unknown";
#endif

    std::vector<std::string> found;
#if defined(__APPLE__) || defined(__linux__)
    std::string cmd = "for cmd in brew nix apt dnf pacman yum zypper apk port; do command -v $cmd >/dev/null 2>&1 && echo $cmd; done";
    std::string output = ProcessHelper::executeCommand(cmd);
    std::stringstream ss(output);
    std::string line;
    while (std::getline(ss, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (!line.empty()) {
            found.push_back(line);
        }
    }
#elif defined(_WIN32)
    std::string cmd = "for %i in (winget choco scoop) do @where %i >nul 2>&1 && echo %i";
    std::string output = ProcessHelper::executeCommand(cmd);
    std::stringstream ss(output);
    std::string line;
    while (std::getline(ss, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (!line.empty()) {
            found.push_back(line);
        }
    }
#endif

    std::string pkgs;
    if (found.empty()) {
        pkgs = "none detected";
    } else {
        for (size_t i = 0; i < found.size(); ++i) {
            if (i > 0) pkgs += ", ";
            pkgs += found[i];
        }
    }

    cached_system_info_ = "OS: " + os_type + " (" + os_version + "), Installed Package Managers: " + pkgs;
    DEBUG_TRACE("MCP: Cached system info: " + cached_system_info_);
}

} // namespace rouen::helpers
