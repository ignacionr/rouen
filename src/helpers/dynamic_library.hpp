#pragma once

// 1. Standard includes in alphabetic order
#include <filesystem>
#include <string>
#include <string_view>

// 2. Libraries used in the project, in alphabetic order
// None in this file

// 3. All other includes
// None in this file

namespace rouen::helpers::dynamic_library {

    // Loads a dynamic library (.dll/.dylib/.so). Returns an opaque
    // platform handle, or nullptr on failure (see last_error()).
    //
    // Loaded libraries are intentionally never unloaded. Rouen's card
    // factory keeps std::function closures whose code lives inside a
    // plugin library in a process-lifetime container, so freeing the
    // library before process exit would leave dangling code behind
    // those closures. There is deliberately no unload()/close() here -
    // see docs/PLUGINS.md for the full reasoning.
    void* load(std::filesystem::path const& path);

    // Resolves an exported symbol by name; returns nullptr if not found.
    void* get_symbol(void* handle, std::string_view name);

    // Human-readable description of the last load()/get_symbol() failure
    // on the calling thread.
    std::string last_error();

    // Platform's dynamic library file extension, including the leading
    // dot (".dll", ".dylib" or ".so").
    std::string_view platform_extension();

} // namespace rouen::helpers::dynamic_library
