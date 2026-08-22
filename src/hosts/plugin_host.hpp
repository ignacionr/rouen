#pragma once

// 1. Standard includes in alphabetic order
#include <filesystem>
#include <string>
#include <vector>

// 2. Libraries used in the project, in alphabetic order
// None in this file

// 3. All other includes
// None in this file

namespace rouen::hosts {

    // Discovers and loads dynamically-linked card/schema plugins at
    // startup. Stateful (tracks what was loaded), so it lives under
    // src/hosts/ rather than src/helpers/ - see docs/ARCHITECTURE.md.
    //
    // Plugins are loaded once, at startup, and are never unloaded for
    // the lifetime of the process (see src/helpers/dynamic_library.hpp
    // for why). There is deliberately no shutdown()/unload_all().
    class plugin_host {
    public:
        static plugin_host& instance();

        // Scans the bundled and user plugin directories and loads every
        // dynamic library found there, calling its rouen_plugin_init
        // entry point. Safe to call once, early in startup, before any
        // card is created from persisted layout or the command line.
        void load_all_plugins();

        [[nodiscard]] std::vector<std::string> const& loaded_plugins() const { return loaded_plugins_; }

    private:
        plugin_host() = default;

        void load_from_directory(std::filesystem::path const& dir);
        void load_plugin(std::filesystem::path const& library_path);

        std::vector<std::string> loaded_plugins_;
    };

} // namespace rouen::hosts
