#pragma once

#include <iostream>
#include <format>
#include <string_view>

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
    // In debug builds, show info logs by default
    #define ROUEN_LOG_LEVEL LOG_LEVEL_WARN
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

// SQLite component logging macros - renamed to avoid conflict with SQLite's own macros
#define DB_ERROR(message) LOG_COMPONENT("SQLITE", LOG_LEVEL_ERROR, message)
#define DB_WARN(message) LOG_COMPONENT("SQLITE", LOG_LEVEL_WARN, message)
#define DB_INFO(message) LOG_COMPONENT("SQLITE", LOG_LEVEL_INFO, message)
#define DB_DEBUG(message) LOG_COMPONENT("SQLITE", LOG_LEVEL_DEBUG, message)
#define DB_TRACE(message) LOG_COMPONENT("SQLITE", LOG_LEVEL_TRACE, message)

namespace debug {
    // Helper function for format-based logging
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

// SQLite format macros - renamed to avoid conflict
#define DB_ERROR_FMT(fmt, ...) DB_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define DB_WARN_FMT(fmt, ...) DB_WARN(debug::format_log(fmt, __VA_ARGS__))
#define DB_INFO_FMT(fmt, ...) DB_INFO(debug::format_log(fmt, __VA_ARGS__))
#define DB_DEBUG_FMT(fmt, ...) DB_DEBUG(debug::format_log(fmt, __VA_ARGS__))
#define DB_TRACE_FMT(fmt, ...) DB_TRACE(debug::format_log(fmt, __VA_ARGS__))