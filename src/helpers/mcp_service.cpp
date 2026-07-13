// 1. Standard includes in alphabetic order
#include <algorithm>
#include <sstream>

// 2. Libraries used in the project, in alphabetic order
#include "glaze_include.hpp"

// 3. All other includes
#include "debug.hpp"
#include "mcp_service.hpp"
#include "process_helper.hpp"

namespace rouen::helpers {

struct local_command_request {
    std::string command{};
    std::string working_directory{};
};

mcp_service::mcp_service() {
    // Register default run_local_command function associated with terminal
    function_definition run_cmd_def(
        "run_local_command",
        "Execute a local shell command and return combined stdout/stderr output. Supports any local command, including curl.",
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
}

void mcp_service::register_function(const std::string& card_type, const function_definition& func) {
    // Create a copy of the function with the card_type set
    function_definition func_copy(func.name, func.description, func.schema, func.handler, card_type);
    
    // Register in main function map
    functions_.emplace(func.name, std::move(func_copy));
    
    // Add to card function registry
    card_functions_[card_type].push_back(func.name);
    
    DEBUG_TRACE("MCP: Registered function '" + func.name + "' from card '" + card_type + "'");
}

void mcp_service::unregister_card_functions(const std::string& card_type) {
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
    std::vector<function_definition> result;
    result.reserve(functions_.size());
    
    for (const auto& [name, func] : functions_) {
        result.push_back(func);
    }
    
    return result;
}

std::vector<mcp_service::function_definition> mcp_service::get_functions_for_card(const std::string& card_type) const {
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
    auto func_it = functions_.find(name);
    if (func_it == functions_.end()) {
        return execution_result(false, "", "Function '" + name + "' not found");
    }
    
    const auto& func = func_it->second;
    
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
    return functions_.find(name) != functions_.end();
}

std::string mcp_service::get_function_schema(const std::string& name) const {
    auto func_it = functions_.find(name);
    if (func_it != functions_.end()) {
        return func_it->second.schema;
    }
    return "";
}

std::string mcp_service::get_functions_description() const {
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

} // namespace rouen::helpers
