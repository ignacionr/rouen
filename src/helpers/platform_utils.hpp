#pragma once

#include <string>
#include <format>
#include <cstdlib>
#include <filesystem>

#if defined(__APPLE__)
#include <mach-o/dyld.h> // For _NSGetExecutablePath
#include <limits.h>      // For PATH_MAX
#elif defined(__linux__)
#include <unistd.h>      // For readlink
#include <limits.h>      // For PATH_MAX
#endif

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
     * 2. Executable directory
     * 3. Current working directory
     * 4. Parent directory (for running from build directories)
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
        std::filesystem::path exec_path;
        std::filesystem::path current_path = std::filesystem::current_path();
        
        // Get the actual executable path (platform specific)
        #if defined(__APPLE__)
        {
            char path[PATH_MAX];
            uint32_t size = sizeof(path);
            if (_NSGetExecutablePath(path, &size) == 0) {
                exec_path = std::filesystem::path(path).parent_path();
            } else {
                // Fallback to current path if _NSGetExecutablePath fails
                exec_path = current_path;
            }
        }
        #elif defined(__linux__)
        {
            char result[PATH_MAX];
            ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
            if (count > 0) {
                exec_path = std::filesystem::path(std::string(result, count)).parent_path();
            } else {
                // Fallback to current path if readlink fails
                exec_path = current_path;
            }
        }
        #else
            // Fallback to current path for unsupported platforms
            exec_path = current_path;
        #endif
        
        #if defined(__APPLE__)
        // 1. macOS app bundle resource path
        {
            // Check if we're in an app bundle by looking at the path structure
            std::filesystem::path bundle_path = exec_path;
            std::filesystem::path previous_path;
            while (!bundle_path.empty() && !bundle_path.string().ends_with(".app")) {
                previous_path = bundle_path;
                bundle_path = bundle_path.parent_path();
                
                // Check if we've reached the root directory (parent_path equals path)
                if (bundle_path == previous_path) {
                    // We've reached the root without finding a .app
                    bundle_path = std::filesystem::path();  // Clear path to indicate not found
                    break;
                }
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
        
        // 2. Executable directory
        {
            search_paths.push_back(exec_path / filename);
        }
        
        // 3. Current working directory
        if (!resource_subdir.empty()) {
            search_paths.push_back(current_path / resource_subdir / filename);
        }
        search_paths.push_back(current_path / filename);
        
        // 4. parent directory (for running from build dir)
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

    /**
     * Get the path for user-specific data storage
     * 
     * This function returns a path suitable for storing user-specific application data:
     * - On macOS: ~/Library/Application Support/Rouen/
     * - On Linux: ~/.local/share/rouen/
     * - Fallback: ~/.rouen/
     *
     * @param filename Optional filename to append to the path
     * @param create_dirs Whether to create the directories if they don't exist
     * @return The path to the user data directory or file
     */
    inline std::filesystem::path get_user_data_path(const std::string& filename = "", bool create_dirs = true)
    {
        std::filesystem::path user_data_dir;
        
        // Get user's home directory
        std::string home_dir = get_env("HOME");
        if (home_dir.empty()) {
            // Fallback if HOME is not available
            #if defined(_WIN32)
                home_dir = get_env("USERPROFILE");
            #else
                // This is unlikely but provides a safeguard
                home_dir = ".";
            #endif
        }
        
        // Create platform-specific user data directory
        #if defined(__APPLE__)
            // macOS: ~/Library/Application Support/Rouen/
            user_data_dir = std::filesystem::path(home_dir) / "Library" / "Application Support" / "Rouen";
        #elif defined(__linux__)
            // Linux: ~/.local/share/rouen/
            user_data_dir = std::filesystem::path(home_dir) / ".local" / "share" / "rouen";
        #else
            // Fallback for other platforms: ~/.rouen/
            user_data_dir = std::filesystem::path(home_dir) / ".rouen";
        #endif
        
        // Create directories if requested and they don't exist
        if (create_dirs && !std::filesystem::exists(user_data_dir)) {
            std::filesystem::create_directories(user_data_dir);
        }
        
        // If filename provided, append it to the path
        if (!filename.empty()) {
            return user_data_dir / filename;
        }
        
        return user_data_dir;
    }

    // Add function to check if mpv is available in the system
    inline bool check_mpv_availability(std::string& mpv_path) {
        // Common installation paths
        std::vector<std::string> common_paths = {
            "/usr/local/bin/mpv",   // Homebrew on Intel Mac
            "/opt/homebrew/bin/mpv" // Homebrew on Apple Silicon
        };
        
        // Check common paths first
        for (const auto& path : common_paths) {
            if (std::filesystem::exists(path)) {
                mpv_path = path;
                return true;
            }
        }
        
        // Check if available in PATH by running 'which mpv'
        std::string command = "which mpv 2>/dev/null";
        std::string result;
        
        FILE* pipe = popen(command.c_str(), "r");
        if (pipe) {
            char buffer[256];
            while (!feof(pipe)) {
                if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                    result += buffer;
                }
            }
            pclose(pipe);
            
            // Clean up result
            if (!result.empty()) {
                // Trim newlines
                result.erase(result.find_last_not_of("\n\r") + 1);
                if (!result.empty() && std::filesystem::exists(result)) {
                    mpv_path = result;
                    return true;
                }
            }
        }
        
        // If we get here, mpv wasn't found
        mpv_path = "mpv"; // Fall back to just the command name
        return false;
    }
}