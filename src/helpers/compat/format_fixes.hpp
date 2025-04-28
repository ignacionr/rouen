// filepath: /Users/ignaciorodriguez/src/rouen/src/helpers/compat/format_fixes.hpp
#pragma once

// This fixes format string issues for long long values
// Replaces instances of %ld with %lld for long long values

#include <string>
#include <format>

namespace compat {
    // Helper functions to convert %ld to %lld when necessary
    inline std::string fix_format_string(const char* format) {
        std::string result = format;
        
        // Replace %ld with %lld (for long long)
        size_t pos = 0;
        while ((pos = result.find("%ld", pos)) != std::string::npos) {
            result.replace(pos, 3, "%lld");
            pos += 4; // Length of "%lld"
        }
        
        // Replace %02ld with %02lld (for long long)
        pos = 0;
        while ((pos = result.find("%02ld", pos)) != std::string::npos) {
            result.replace(pos, 5, "%02lld");
            pos += 6; // Length of "%02lld"
        }
        
        return result;
    }
}