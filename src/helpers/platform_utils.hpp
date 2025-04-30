#pragma once

#include <string>
#include <format>
#include <cstdlib>

namespace rouen::platform
{
    /**
     * Opens a file or URL with the default system application
     * Uses 'open' on macOS and 'xdg-open' on Linux
     *
     * @param path The file path or URL to open
     * @param background Run in background (appends & on Linux)
     * @return The command string that was executed
     */
    inline std::string open_file(const std::string& path, bool background = false)
    {
        std::string cmd;
        
        #if defined(__APPLE__)
            // macOS uses the 'open' command (ignore background parameter)
            (void)background; // Silence unused parameter warning
            cmd = std::format("open \"{}\"", path);
        #else
            // Linux and others use xdg-open
            cmd = std::format("xdg-open \"{}\"", path);
            // Add & for background operation if requested
            if (background) {
                cmd += " &";
            }
        #endif
        
        return cmd;
    }
    
    /**
     * Get the value of an environment variable
     *
     * @param name The name of the environment variable
     * @return The value of the environment variable or empty string if not set
     */
    inline std::string get_env(const std::string& name)
    {
        const char* value = std::getenv(name.c_str());
        return value ? std::string(value) : std::string();
    }
}