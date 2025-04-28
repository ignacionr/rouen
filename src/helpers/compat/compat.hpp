// filepath: /Users/ignaciorodriguez/src/rouen/src/helpers/compat/compat.hpp
#pragma once

// Main compatibility header - include this to get all compatibility features

// C++20/23 features not fully supported on macOS
#include "jthread.hpp"
#include "format_fixes.hpp"

// Add a macro to make the specific format string fixes easier to apply
#define FIX_FORMAT(format_str, ...) \
    compat::fix_format_string(format_str).c_str(), __VA_ARGS__