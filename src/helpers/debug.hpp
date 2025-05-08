#pragma once

// 1. Standard includes in alphabetic order
#include <format>
#include <iostream>
#include <string_view>
#include <sstream>
#include <iomanip>

// 2. Libraries used in the project, in alphabetic order
// None in this file

// 3. All other includes
// None in this file

// Define logging levels
#define LOG_LEVEL_NONE 0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN 2
#define LOG_LEVEL_INFO 3
#define LOG_LEVEL_DEBUG 4
#define LOG_LEVEL_TRACE 5

// Set default log level if not defined (can be overridden with -D compiler flag)
#ifndef ROUEN_LOG_LEVEL
#ifdef NDEBUG
    // In release builds, only show errors by default
    #define ROUEN_LOG_LEVEL LOG_LEVEL_ERROR
#else
    // In debug builds, show warnings by default for better RSS feed visibility
    #define ROUEN_LOG_LEVEL LOG_LEVEL_TRACE
#endif
#endif

// Convenience macro for component logging
#define LOG_COMPONENT(component, level, message) \
    if (level <= ROUEN_LOG_LEVEL) { \
        std::cerr << "[" << component << "][" << \
        (level == LOG_LEVEL_ERROR ? "ERROR" : \
         level == LOG_LEVEL_WARN ? "WARN" : \
         level == LOG_LEVEL_INFO ? "INFO" : \
         level == LOG_LEVEL_DEBUG ? "DEBUG" : "TRACE") \
        << "] " << message << std::endl; \
    }

// Component-specific logging macros
#define RSS_ERROR(message) LOG_COMPONENT("RSS", LOG_LEVEL_ERROR, message)
#define RSS_WARN(message) LOG_COMPONENT("RSS", LOG_LEVEL_WARN, message)
#define RSS_INFO(message) LOG_COMPONENT("RSS", LOG_LEVEL_INFO, message)
#define RSS_DEBUG(message) LOG_COMPONENT("RSS", LOG_LEVEL_DEBUG, message)
#define RSS_TRACE(message) LOG_COMPONENT("RSS", LOG_LEVEL_TRACE, message)

// Weather component logging macros
#define WEATHER_ERROR(message) LOG_COMPONENT("WEATHER", LOG_LEVEL_ERROR, message)
#define WEATHER_WARN(message) LOG_COMPONENT("WEATHER", LOG_LEVEL_WARN, message)
#define WEATHER_INFO(message) LOG_COMPONENT("WEATHER", LOG_LEVEL_INFO, message)
#define WEATHER_DEBUG(message) LOG_COMPONENT("WEATHER", LOG_LEVEL_DEBUG, message)
#define WEATHER_TRACE(message) LOG_COMPONENT("WEATHER", LOG_LEVEL_TRACE, message)

// SQLite component logging macros - renamed to avoid conflict with SQLite's own macros
#define DB_ERROR(message) LOG_COMPONENT("SQLITE", LOG_LEVEL_ERROR, message)
#define DB_WARN(message) LOG_COMPONENT("SQLITE", LOG_LEVEL_WARN, message)
#define DB_INFO(message) LOG_COMPONENT("SQLITE", LOG_LEVEL_INFO, message)
#define DB_DEBUG(message) LOG_COMPONENT("SQLITE", LOG_LEVEL_DEBUG, message)
#define DB_TRACE(message) LOG_COMPONENT("SQLITE", LOG_LEVEL_TRACE, message)

// Main window component logging macros
#define WND_ERROR(message) LOG_COMPONENT("WINDOW", LOG_LEVEL_ERROR, message)
#define WND_WARN(message) LOG_COMPONENT("WINDOW", LOG_LEVEL_WARN, message)
#define WND_INFO(message) LOG_COMPONENT("WINDOW", LOG_LEVEL_INFO, message)
#define WND_DEBUG(message) LOG_COMPONENT("WINDOW", LOG_LEVEL_DEBUG, message)
#define WND_TRACE(message) LOG_COMPONENT("WINDOW", LOG_LEVEL_TRACE, message)

// Git component logging macros
#define GIT_ERROR(message) LOG_COMPONENT("GIT", LOG_LEVEL_ERROR, message)
#define GIT_WARN(message) LOG_COMPONENT("GIT", LOG_LEVEL_WARN, message)
#define GIT_INFO(message) LOG_COMPONENT("GIT", LOG_LEVEL_INFO, message)
#define GIT_DEBUG(message) LOG_COMPONENT("GIT", LOG_LEVEL_DEBUG, message)
#define GIT_TRACE(message) LOG_COMPONENT("GIT", LOG_LEVEL_TRACE, message)

// Animation component logging macros
#define ANIM_ERROR(message) LOG_COMPONENT("ANIMATION", LOG_LEVEL_ERROR, message)
#define ANIM_WARN(message) LOG_COMPONENT("ANIMATION", LOG_LEVEL_WARN, message)
#define ANIM_INFO(message) LOG_COMPONENT("ANIMATION", LOG_LEVEL_INFO, message)
#define ANIM_DEBUG(message) LOG_COMPONENT("ANIMATION", LOG_LEVEL_DEBUG, message)
#define ANIM_TRACE(message) LOG_COMPONENT("ANIMATION", LOG_LEVEL_TRACE, message)

// Radio component logging macros
#define RADIO_ERROR(message) LOG_COMPONENT("RADIO", LOG_LEVEL_ERROR, message)
#define RADIO_WARN(message) LOG_COMPONENT("RADIO", LOG_LEVEL_WARN, message)
#define RADIO_INFO(message) LOG_COMPONENT("RADIO", LOG_LEVEL_INFO, message)
#define RADIO_DEBUG(message) LOG_COMPONENT("RADIO", LOG_LEVEL_DEBUG, message)
#define RADIO_TRACE(message) LOG_COMPONENT("RADIO", LOG_LEVEL_TRACE, message)

// Notification service logging macros
#define NOTIFY_ERROR(message) LOG_COMPONENT("NOTIFY", LOG_LEVEL_ERROR, message)
#define NOTIFY_WARN(message) LOG_COMPONENT("NOTIFY", LOG_LEVEL_WARN, message)
#define NOTIFY_INFO(message) LOG_COMPONENT("NOTIFY", LOG_LEVEL_INFO, message)
#define NOTIFY_DEBUG(message) LOG_COMPONENT("NOTIFY", LOG_LEVEL_DEBUG, message)
#define NOTIFY_TRACE(message) LOG_COMPONENT("NOTIFY", LOG_LEVEL_TRACE, message)

// System/app logging macros
#define SYS_ERROR(message) LOG_COMPONENT("SYSTEM", LOG_LEVEL_ERROR, message)
#define SYS_WARN(message) LOG_COMPONENT("SYSTEM", LOG_LEVEL_WARN, message)
#define SYS_INFO(message) LOG_COMPONENT("SYSTEM", LOG_LEVEL_INFO, message)
#define SYS_DEBUG(message) LOG_COMPONENT("SYSTEM", LOG_LEVEL_DEBUG, message)
#define SYS_TRACE(message) LOG_COMPONENT("SYSTEM", LOG_LEVEL_TRACE, message)

// Chess-specific logging macros
#define CHESS_ERROR(message) LOG_COMPONENT("CHESS", LOG_LEVEL_ERROR, message)
#define CHESS_WARN(message) LOG_COMPONENT("CHESS", LOG_LEVEL_WARN, message)
#define CHESS_INFO(message) LOG_COMPONENT("CHESS", LOG_LEVEL_INFO, message)
#define CHESS_DEBUG(message) LOG_COMPONENT("CHESS", LOG_LEVEL_DEBUG, message)
#define CHESS_TRACE(message) LOG_COMPONENT("CHESS", LOG_LEVEL_TRACE, message)

namespace debug {
    // Convert char32_t to a string representation for logging
    inline std::string char32_to_string(char32_t c) {
        std::stringstream ss;
        ss << "U+" << std::hex << std::uppercase << std::setfill('0') << std::setw(4)
           << static_cast<uint32_t>(c);
        return ss.str();
    }
    
    // Convert char32_t to a hex representation without the U+ prefix
    inline std::string char32_to_hex(char32_t c) {
        std::stringstream ss;
        ss << std::hex << std::uppercase << std::setfill('0') << std::setw(4)
           << static_cast<uint32_t>(c);
        return ss.str();
    }
}

// C++23 formatter for char32_t with support for hex format specifier
template<>
struct std::formatter<char32_t> {
    // Store whether we should format as hex
    bool hex_format = false;
    
    // Parse the format specification to detect if we should use hex format
    constexpr auto parse(std::format_parse_context& ctx) {
        auto it = ctx.begin();
        auto end = ctx.end();
        
        // If we have any format specifier
        if (it != end && *it == ':') {
            // Move past the colon
            ++it;
            
            // Check if next character is 'X' for hex format
            if (it != end && (*it == 'X' || *it == 'x')) {
                hex_format = true;
                ++it;
            }
            
            // Skip any additional format parameters
            while (it != end && *it != '}') ++it;
        }
        
        return it;
    }
    
    // Format the char32_t based on the parsed format spec
    template<typename FormatContext>
    auto format(char32_t c, FormatContext& ctx) const {
        if (hex_format) {
            // Format as hex without U+ prefix
            return std::format_to(ctx.out(), "{}", debug::char32_to_hex(c));
        } else {
            // Default format with U+ prefix
            return std::format_to(ctx.out(), "{}", debug::char32_to_string(c));
        }
    }
};

namespace debug {
    // Helper function for format-based logging with proper C++23 support for all types
    template<typename... Args>
    inline std::string format_log(std::format_string<Args...> fmt, Args&&... args) {
        return std::format(fmt, std::forward<Args>(args)...);
    }
}

// Convenience macros with format support
#define RSS_ERROR_FMT(fmt, ...) RSS_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define RSS_WARN_FMT(fmt, ...) RSS_WARN(debug::format_log(fmt, __VA_ARGS__))
#define RSS_INFO_FMT(fmt, ...) RSS_INFO(debug::format_log(fmt, __VA_ARGS__))
#define RSS_DEBUG_FMT(fmt, ...) RSS_DEBUG(debug::format_log(fmt, __VA_ARGS__))
#define RSS_TRACE_FMT(fmt, ...) RSS_TRACE(debug::format_log(fmt, __VA_ARGS__))

// Weather component format macros
#define WEATHER_ERROR_FMT(fmt, ...) WEATHER_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define WEATHER_WARN_FMT(fmt, ...) WEATHER_WARN(debug::format_log(fmt, __VA_ARGS__))
#define WEATHER_INFO_FMT(fmt, ...) WEATHER_INFO(debug::format_log(fmt, __VA_ARGS__))
#define WEATHER_DEBUG_FMT(fmt, ...) WEATHER_DEBUG(debug::format_log(fmt, __VA_ARGS__))
#define WEATHER_TRACE_FMT(fmt, ...) WEATHER_TRACE(debug::format_log(fmt, __VA_ARGS__))

// SQLite format macros - renamed to avoid conflict
#define DB_ERROR_FMT(fmt, ...) DB_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define DB_WARN_FMT(fmt, ...) DB_WARN(debug::format_log(fmt, __VA_ARGS__))
#define DB_INFO_FMT(fmt, ...) DB_INFO(debug::format_log(fmt, __VA_ARGS__))
#define DB_DEBUG_FMT(fmt, ...) DB_DEBUG(debug::format_log(fmt, __VA_ARGS__))
#define DB_TRACE_FMT(fmt, ...) DB_TRACE(debug::format_log(fmt, __VA_ARGS__))

// Window component format macros
#define WND_ERROR_FMT(fmt, ...) WND_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define WND_WARN_FMT(fmt, ...) WND_WARN(debug::format_log(fmt, __VA_ARGS__))
#define WND_INFO_FMT(fmt, ...) WND_INFO(debug::format_log(fmt, __VA_ARGS__))
#define WND_DEBUG_FMT(fmt, ...) WND_DEBUG(debug::format_log(fmt, __VA_ARGS__))
#define WND_TRACE_FMT(fmt, ...) WND_TRACE(debug::format_log(fmt, __VA_ARGS__))

// Git component format macros
#define GIT_ERROR_FMT(fmt, ...) GIT_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define GIT_WARN_FMT(fmt, ...) GIT_WARN(debug::format_log(fmt, __VA_ARGS__))
#define GIT_INFO_FMT(fmt, ...) GIT_INFO(debug::format_log(fmt, __VA_ARGS__))
#define GIT_DEBUG_FMT(fmt, ...) GIT_DEBUG(debug::format_log(fmt, __VA_ARGS__))
#define GIT_TRACE_FMT(fmt, ...) GIT_TRACE(debug::format_log(fmt, __VA_ARGS__))

// Animation component format macros
#define ANIM_ERROR_FMT(fmt, ...) ANIM_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define ANIM_WARN_FMT(fmt, ...) ANIM_WARN(debug::format_log(fmt, __VA_ARGS__))
#define ANIM_INFO_FMT(fmt, ...) ANIM_INFO(debug::format_log(fmt, __VA_ARGS__))
#define ANIM_DEBUG_FMT(fmt, ...) ANIM_DEBUG(debug::format_log(fmt, __VA_ARGS__))
#define ANIM_TRACE_FMT(fmt, ...) ANIM_TRACE(debug::format_log(fmt, __VA_ARGS__))

// Radio component format macros
#define RADIO_ERROR_FMT(fmt, ...) RADIO_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define RADIO_WARN_FMT(fmt, ...) RADIO_WARN(debug::format_log(fmt, __VA_ARGS__))
#define RADIO_INFO_FMT(fmt, ...) RADIO_INFO(debug::format_log(fmt, __VA_ARGS__))
#define RADIO_DEBUG_FMT(fmt, ...) RADIO_DEBUG(debug::format_log(fmt, __VA_ARGS__))
#define RADIO_TRACE_FMT(fmt, ...) RADIO_TRACE(debug::format_log(fmt, __VA_ARGS__))

// Notification service format macros
#define NOTIFY_ERROR_FMT(fmt, ...) NOTIFY_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define NOTIFY_WARN_FMT(fmt, ...) NOTIFY_WARN(debug::format_log(fmt, __VA_ARGS__))
#define NOTIFY_INFO_FMT(fmt, ...) NOTIFY_INFO(debug::format_log(fmt, __VA_ARGS__))
#define NOTIFY_DEBUG_FMT(fmt, ...) NOTIFY_DEBUG(debug::format_log(fmt, __VA_ARGS__))
#define NOTIFY_TRACE_FMT(fmt, ...) NOTIFY_TRACE(debug::format_log(fmt, __VA_ARGS__))

// System/app format macros
#define SYS_ERROR_FMT(fmt, ...) SYS_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define SYS_WARN_FMT(fmt, ...) SYS_WARN(debug::format_log(fmt, __VA_ARGS__))
#define SYS_INFO_FMT(fmt, ...) SYS_INFO(debug::format_log(fmt, __VA_ARGS__))
#define SYS_DEBUG_FMT(fmt, ...) SYS_DEBUG(debug::format_log(fmt, __VA_ARGS__))
#define SYS_TRACE_FMT(fmt, ...) SYS_TRACE(debug::format_log(fmt, __VA_ARGS__))

// Chess component format macros
#define CHESS_ERROR_FMT(fmt, ...) CHESS_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define CHESS_WARN_FMT(fmt, ...) CHESS_WARN(debug::format_log(fmt, __VA_ARGS__))
#define CHESS_INFO_FMT(fmt, ...) CHESS_INFO(debug::format_log(fmt, __VA_ARGS__))
#define CHESS_DEBUG_FMT(fmt, ...) CHESS_DEBUG(debug::format_log(fmt, __VA_ARGS__))
#define CHESS_TRACE_FMT(fmt, ...) CHESS_TRACE(debug::format_log(fmt, __VA_ARGS__))