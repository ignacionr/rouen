#pragma once

#include "debug.hpp"

import rouen.helpers.config_service;

// Convenience macros for configuration access
#define CONFIG_SERVICE() rouen::helpers::ConfigService::instance()
#define GET_CONFIG(name) CONFIG_SERVICE()->get_env(name)
#define GET_CONFIG_OPTIONAL(name) CONFIG_SERVICE()->get_env_optional(name)
#define GET_CONFIG_OPT(name) GET_CONFIG_OPTIONAL(name)
#define HAS_CONFIG(name) CONFIG_SERVICE()->has_env(name)

// Macro for registering configurations easily
#define REGISTER_CONFIG(name, category, required, sensitive, desc, default_val) \
    CONFIG_SERVICE()->register_config(name, category, required, sensitive, desc, default_val)

// Macros for specific configuration categories
#define GET_API_KEY(service) CONFIG_SERVICE()->get_api_key(service)
#define GET_JIRA_CONFIG(profile, key) CONFIG_SERVICE()->get_jira_config(profile, key)

// Macros for logging with configuration service
#define CONFIG_ERROR(message) LOG_COMPONENT("CONFIG", LOG_LEVEL_ERROR, message)
#define CONFIG_WARN(message) LOG_COMPONENT("CONFIG", LOG_LEVEL_WARN, message)
#define CONFIG_INFO(message) LOG_COMPONENT("CONFIG", LOG_LEVEL_INFO, message)
#define CONFIG_DEBUG(message) LOG_COMPONENT("CONFIG", LOG_LEVEL_DEBUG, message)

#define CONFIG_ERROR_FMT(fmt, ...) CONFIG_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define CONFIG_WARN_FMT(fmt, ...) CONFIG_WARN(debug::format_log(fmt, __VA_ARGS__))
#define CONFIG_INFO_FMT(fmt, ...) CONFIG_INFO(debug::format_log(fmt, __VA_ARGS__))
#define CONFIG_DEBUG_FMT(fmt, ...) CONFIG_DEBUG(debug::format_log(fmt, __VA_ARGS__))

