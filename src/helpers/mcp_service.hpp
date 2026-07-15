#pragma once

// 1. Standard includes in alphabetic order
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// 2. Libraries used in the project, in alphabetic order
#include "glaze_include.hpp"

// 3. All other includes
// None in this file

namespace rouen::helpers {

/**
 * Internal Model Context Protocol (MCP) Service
 * 
 * Provides function calling capabilities for AI chat integration
 * with other cards and services in the Rouen application.
 */
class mcp_service {
public:
    mcp_service();
    
    // Function definition structure
    struct function_definition {
        std::string name;
        std::string description;
        std::string schema; // JSON schema for parameters
        std::function<std::string(const std::string& params)> handler;
        std::string card_type; // Which card registered this function
        
        // Default constructor
        function_definition() = default;
        
        // Constructor
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

    // Function execution result
    struct execution_result {
        bool success;
        std::string result;
        std::string error_message;
        
        execution_result(bool exec_success, std::string exec_result, std::string exec_error = "")
            : success(exec_success), result(std::move(exec_result)), error_message(std::move(exec_error)) {}
    };

    /**
     * Register a function from a card
     */
    void register_function(const std::string& card_type, const function_definition& func);
    
    /**
     * Unregister all functions for a specific card type
     */
    void unregister_card_functions(const std::string& card_type);
    
    /**
     * Get all available functions
     */
    std::vector<function_definition> get_available_functions() const;
    
    /**
     * Get functions for a specific card type
     */
    std::vector<function_definition> get_functions_for_card(const std::string& card_type) const;
    
    /**
     * Execute a function call with error handling
     */
    execution_result execute_function(const std::string& name, const std::string& params);
    
    /**
     * Check if a function exists
     */
    bool has_function(const std::string& name) const;
    
    /**
     * Get function schema for AI integration
     */
    std::string get_function_schema(const std::string& name) const;
    
    /**
     * Get all functions formatted for AI prompt
     */
    std::string get_functions_description() const;

private:
    // Function registry - maps function name to definition
    std::unordered_map<std::string, function_definition> functions_;
    
    // Card type registry - maps card type to function names
    std::unordered_map<std::string, std::vector<std::string>> card_functions_;
    
    // Helper to validate JSON parameters against schema
    bool validate_parameters(const std::string& params, const std::string& schema) const;
    
    // System information cached at startup
    std::string cached_system_info_;
    
    // Detect OS type, version, and installed package managers
    void detect_system_info();
    
    // Mutex to protect internal registry maps
    mutable std::mutex mutex_;
};

} // namespace rouen::helpers
