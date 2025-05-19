#pragma once

#include <string>
#include <format>
#include <cstdlib>
#include <filesystem>

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

    /**
     * Get the full path to a resource file
     * Handles both app bundle resources and development environment paths
     * 
     * This function provides a unified approach to resource management, ensuring that
     * files can be located both during development and in the deployed application bundle.
     * It searches multiple locations in priority order:
     * 1. macOS app bundle Contents/Resources directory
     * 2. Current working directory
     * 3. Parent directory (for running from build directories)
     *
     * @param filename The name of the resource file (e.g. "presets.txt")
     * @param resource_subdir Optional subdirectory within Resources (e.g. "img")
     * @return The full path to the resource file
     */
    inline std::filesystem::path get_resource_path(const std::string& filename, const std::string& resource_subdir = "")
    {
        // Check several possible locations for the resources, starting with most likely
        std::vector<std::filesystem::path> search_paths;
        
        // Get the current executable path
        auto exec_path = std::filesystem::current_path();
        
        #if defined(__APPLE__)
        // 1. macOS app bundle resource path
        {
            // Check if we're in an app bundle by looking at the path structure
            std::filesystem::path bundle_path = exec_path;
            while (!bundle_path.empty() && !bundle_path.string().ends_with(".app")) {
                bundle_path = bundle_path.parent_path();
            }
            
            if (!bundle_path.empty()) {
                // We're in an app bundle
                std::filesystem::path resource_path = bundle_path / "Contents" / "Resources";
                
                if (!resource_subdir.empty()) {
                    resource_path = resource_path / resource_subdir;
                }
                
                search_paths.push_back(resource_path / filename);
            }
        }
        #endif
        
        // 2. Current working directory
        if (!resource_subdir.empty()) {
            search_paths.push_back(exec_path / resource_subdir / filename);
        }
        search_paths.push_back(exec_path / filename);
        
        // 3. parent directory (for running from build dir)
        if (!resource_subdir.empty()) {
            search_paths.push_back(exec_path.parent_path() / resource_subdir / filename);
        }
        search_paths.push_back(exec_path.parent_path() / filename);
        
        // Check all paths and return the first one that exists
        for (const auto& path : search_paths) {
            if (std::filesystem::exists(path)) {
                return path;
            }
        }
        
        // If not found, return the first path (most likely location)
        // This will likely cause an error when used, but at least it's predictable
        return search_paths.front();
    }
}