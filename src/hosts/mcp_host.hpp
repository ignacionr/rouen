#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>


namespace rouen::hosts {

/**
 * Model Context Protocol (MCP) Host Service
 * 
 * Manages tool registration, subprocess tool channels, system info context,
 * and JSON-RPC tool call dispatching for AI integration.
 */
class mcp_host {
public:
    mcp_host();
    
    struct function_definition {
        std::string name;
        std::string description;
        std::string schema;
        std::function<std::string(const std::string& params)> handler;
        std::string card_type;
        
        function_definition() = default;
        
        function_definition(
            std::string func_name,
            std::string func_description, 
            std::string func_schema,
            std::function<std::string(const std::string&)> func_handler,
            std::string func_card_type
        ) : name(std::move(func_name)), description(std::move(func_description)), 
            schema(std::move(func_schema)), handler(std::move(func_handler)), 
            card_type(std::move(func_card_type)) {}
    };

    struct execution_result {
        bool success;
        std::string result;
        std::string error_message;
        
        execution_result(bool exec_success, std::string exec_result, std::string exec_error = "")
            : success(exec_success), result(std::move(exec_result)), error_message(std::move(exec_error)) {}
    };

    void register_function(const std::string& card_type, const function_definition& func);
    void unregister_card_functions(const std::string& card_type);
    std::vector<function_definition> get_available_functions() const;
    std::vector<function_definition> get_functions_for_card(const std::string& card_type) const;
    std::vector<std::string> get_registered_categories() const;
    execution_result execute_function(const std::string& name, const std::string& params);
    bool has_function(const std::string& name) const;
    std::string get_function_schema(const std::string& name) const;
    std::string get_functions_description() const;

private:
    std::unordered_map<std::string, function_definition> functions_;
    std::unordered_map<std::string, std::vector<std::string>> card_functions_;
    
    static bool validate_parameters(const std::string& params, const std::string& schema);
    std::string cached_system_info_;
    void detect_system_info();
    
    mutable std::mutex mutex_;
};

} // namespace rouen::hosts

namespace rouen::helpers {
    using mcp_service = ::rouen::hosts::mcp_host;
}
