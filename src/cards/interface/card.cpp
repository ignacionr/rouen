// 1. Standard includes in alphabetic order
// None in this file's top section

// 2. Libraries used in the project, in alphabetic order
// None in this file's top section

// 3. All other includes
#include "card.hpp"
#include "../../registrar.hpp"
#include "../../helpers/debug.hpp"
#include "hosts/mcp_host.hpp"
#include <exception>
#include <string>

void card::register_mcp_functions() {
    try {
        if (mcp_functions_registered) {
            DEBUG_TRACE("MCP functions already registered for card: " + get_uri());
            return;
        }
        
        auto mcp = registrar::get<rouen::helpers::mcp_service>("mcp_service");
        if (!mcp) {
            DEBUG_WARN("MCP service not available for card: " + get_uri());
            return;
        }
        
        auto functions = get_mcp_functions();
        for (const auto& func : functions) {
            // Convert card::mcp_function to mcp_service::function_definition
            rouen::helpers::mcp_service::function_definition const def(
                func.name,
                func.description,
                func.schema,
                func.handler,
                get_uri()
            );
            
            mcp->register_function(get_uri(), def);
        }
        
        if (!functions.empty()) {
            mcp_functions_registered = true;
            DEBUG_INFO("Registered " + std::to_string(functions.size()) + " MCP functions for card: " + get_uri());
        }
    } catch (const std::exception& e) {
        DEBUG_ERROR("Exception during MCP function registration: " + std::string(e.what()));
    } catch (...) {
        DEBUG_ERROR("Unknown exception during MCP function registration");
    }
}

void card::unregister_mcp_functions() {
    try {
        if (!mcp_functions_registered) {
            DEBUG_TRACE("MCP functions not registered or already unregistered for card");
            return;
        }
        
        auto mcp = registrar::get<rouen::helpers::mcp_service>("mcp_service");
        if (!mcp) {
            DEBUG_TRACE("MCP service not available during card cleanup");
            mcp_functions_registered = false; // Mark as unregistered even if service unavailable
            return;
        }
        
        // Safely get the URI - guard against virtual method call during destruction
        std::string uri;
        try {
            uri = get_uri();
        } catch (...) {
            DEBUG_WARN("Failed to get URI during card destruction - skipping MCP cleanup");
            mcp_functions_registered = false; // Mark as unregistered
            return;
        }
        
        if (uri.empty()) {
            DEBUG_WARN("Empty URI during card destruction - skipping MCP cleanup");
            mcp_functions_registered = false; // Mark as unregistered
            return;
        }
        
        mcp->unregister_card_functions(uri);
        mcp_functions_registered = false; // Mark as unregistered
        DEBUG_TRACE("Unregistered MCP functions for card: " + uri);
    } catch (const std::exception& e) {
        mcp_functions_registered = false; // Mark as unregistered even on exception
        DEBUG_ERROR("Exception during MCP function unregistration: " + std::string(e.what()));
    } catch (...) {
        mcp_functions_registered = false; // Mark as unregistered even on exception
        DEBUG_ERROR("Unknown exception during MCP function unregistration");
    }
}
