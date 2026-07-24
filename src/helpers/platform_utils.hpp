#pragma once

#include <string>
#include <vector>
#include <format>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <thread>

#if defined(__APPLE__)
#include <mach-o/dyld.h> // For _NSGetExecutablePath
#include <limits.h>      // For PATH_MAX
#include <unistd.h>      // For access()
#elif defined(__linux__)
#include <unistd.h>      // For readlink
#include <limits.h>      // For PATH_MAX
#elif defined(_WIN32)
#include <windows.h>     // For GetModuleFileName
#include <shellapi.h>    // For ShellExecuteA
#include <io.h>          // For _popen/_pclose
#define popen _popen
#define pclose _pclose
#endif

namespace rouen::platform
{
#if defined(_WIN32)
    constexpr bool is_windows = true;
#else
    constexpr bool is_windows = false;
#endif

#if defined(__APPLE__)
    constexpr bool is_apple = true;
#else
    constexpr bool is_apple = false;
#endif

#if defined(__linux__)
    constexpr bool is_linux = true;
#else
    constexpr bool is_linux = false;
#endif

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
        #elif defined(_WIN32)
            // Windows uses the 'start' command
            (void)background; // Background is default behavior on Windows
            cmd = std::format("start \"\" \"{}\"", path);
        #else
            // Linux uses xdg-open
            cmd = std::format("xdg-open \"{}\"", path);
            if (background) {
                cmd += " &";
            }
        #endif
        
        return cmd;
    }
    
    /**
     * Opens a URL or file directly using platform-specific APIs
     * Uses ShellExecuteA on Windows, system() calls on other platforms
     *
     * @param url_or_path The URL or file path to open
     * @return true if the operation was initiated successfully, false otherwise
     */
    inline bool open_url(const std::string& url_or_path)
    {
        #ifdef _WIN32
            // Use Windows ShellExecuteA API for better integration
            HINSTANCE result = ShellExecuteA(NULL, "open", url_or_path.c_str(), NULL, NULL, SW_SHOWNORMAL);
            // ShellExecuteA returns a value greater than 32 if successful
            return reinterpret_cast<intptr_t>(result) > 32;
        #elif defined(__APPLE__)
            // macOS uses the 'open' command
            std::string cmd = std::format("open \"{}\"", url_or_path);
            return system(cmd.c_str()) == 0;
        #else
            // Linux and others use xdg-open
            std::string cmd = std::format("xdg-open \"{}\"", url_or_path);
            return system(cmd.c_str()) == 0;
        #endif
    }

    /**
     * Terminate any active speech synthesis process
     */
    inline void stop_speech()
    {
        #ifdef __APPLE__
            [[maybe_unused]] int result = std::system("killall say 2>/dev/null");
        #endif
    }

    /**
     * Synthesize speech from text asynchronously using platform capabilities
     *
     * @param text The text to speak
     * @param voice Optional voice name (platform-specific)
     * @param lang Optional language code ("es","en",...); when set to "es" prefer say-es
     * @param on_complete Optional callback when speech is complete or interrupted
     */
    template <typename Func>
    inline void speak_text_async(const std::string& text, const std::string& voice, const std::string& lang, Func&& on_complete)
    {
        #ifdef __APPLE__
            stop_speech();
            std::thread([text, voice, lang, cb = std::forward<Func>(on_complete)]() mutable {
                std::string clean_text = text;
                size_t pos = 0;
                while (true) {
                    pos = clean_text.find("http", pos);
                    if (pos == std::string::npos) {
                        break;
                    }
                    if (pos + 4 < clean_text.size() && (clean_text.substr(pos, 7) == "http://" || clean_text.substr(pos, 8) == "https://")) {
                        size_t end_pos = pos;
                        while (end_pos < clean_text.size() && !std::isspace(static_cast<unsigned char>(clean_text[end_pos]))) {
                            end_pos++;
                        }
                        clean_text.replace(pos, end_pos - pos, "link");
                        pos += 4;
                    } else {
                        pos += 4;
                    }
                }

                std::string safe_text;
                for (char c : clean_text) {
                    if (c == '"') {
                        safe_text += "\\\"";
                    } else if (c == '\\') {
                        safe_text += "\\\\";
                    } else if (c == '`' || c == '$' || c == '(' || c == ')' || c == ';' || c == '&' || c == '|' || c == '\n' || c == '\r') {
                        safe_text += ' ';
                    } else {
                        safe_text += c;
                    }
                }

                std::string command;
                if (lang == "es") {
                    // Prefer say-es for Spanish. Check common install locations (GUI apps may have limited PATH).
                    std::string sayes_path;
                    std::vector<std::string> candidates = {"/usr/bin/say-es", "/usr/local/bin/say-es", "/opt/homebrew/bin/say-es", "/usr/local/sbin/say-es"};
                    // Also check the user's ~/bin which GUI apps often use
                    if (const char* home = std::getenv("HOME"); home && home[0] != '\0') {
                        candidates.push_back(std::string(home) + "/bin/say-es");
                    }
                    for (const auto& p : candidates) {
                        if (std::filesystem::exists(p) && access(p.c_str(), X_OK) == 0) {
                            sayes_path = p;
                            break;
                        }
                    }
                    if (!sayes_path.empty()) {
                        command = std::format("\"{}\" \"{}\"", sayes_path, safe_text);
                    } else if (!voice.empty()) {
                        command = std::format("say -v \"{}\" \"{}\"", voice, safe_text);
                    } else {
                        command = std::format("say \"{}\"", safe_text);
                    }
                } else {
                    if (!voice.empty()) {
                        command = std::format("say -v \"{}\" \"{}\"", voice, safe_text);
                    } else {
                        command = std::format("say \"{}\"", safe_text);
                    }
                }

                [[maybe_unused]] int result = std::system(command.c_str());
                cb();
            }).detach();
        #else
            (void)text;
            (void)voice;
            (void)lang;
            on_complete();
        #endif
    }

    // Backwards-compatible overloads
    template <typename Func>
    inline void speak_text_async(const std::string& text, const std::string& voice, Func&& on_complete)
    {
        speak_text_async(text, voice, std::string(), std::forward<Func>(on_complete));
    }

    template <typename Func>
    inline void speak_text_async(const std::string& text, Func&& on_complete)
    {
        speak_text_async(text, std::string(), std::string(), std::forward<Func>(on_complete));
    }

    inline void speak_text_async(const std::string& text)
    {
        speak_text_async(text, std::string(), std::string(), [](){});
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
     * Get the directory containing the current executable
     * 
     * This function provides cross-platform access to the executable's directory.
     * It uses platform-specific APIs to get the actual executable path rather than
     * relying on working directory or argv[0] which can be unreliable.
     *
     * @return The directory path containing the current executable
     */
    inline std::filesystem::path get_executable_directory()
    {
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
                exec_path = std::filesystem::path(std::string(result, static_cast<std::string::size_type>(count))).parent_path();
            } else {
                // Fallback to current path if readlink fails
                exec_path = current_path;
            }
        }
        #elif defined(_WIN32)
        {
            char path[MAX_PATH];
            DWORD length = GetModuleFileNameA(NULL, path, MAX_PATH);
            if (length > 0 && length < MAX_PATH) {
                exec_path = std::filesystem::path(path).parent_path();
            } else {
                // Fallback to current path if GetModuleFileName fails
                exec_path = current_path;
            }
        }
        #else
            // Fallback to current path for unsupported platforms
            exec_path = current_path;
        #endif
        
        return exec_path;
    }

    /**
     * Get a writable directory for user-specific configuration files
     * Resolves to standard paths:
     * - Windows: %APPDATA%/Rouen
     * - macOS: ~/Library/Application Support/Rouen
     * - Linux/Unix: ~/.config/rouen (or XDG_CONFIG_HOME)
     */
    inline std::filesystem::path get_user_config_directory()
    {
        std::filesystem::path config_dir;
        #if defined(_WIN32)
            const char* appdata = std::getenv("APPDATA");
            if (appdata) {
                config_dir = std::filesystem::path(appdata) / "Rouen";
            } else {
                const char* userprofile = std::getenv("USERPROFILE");
                if (userprofile) {
                    config_dir = std::filesystem::path(userprofile) / ".config" / "rouen";
                } else {
                    config_dir = std::filesystem::current_path();
                }
            }
        #elif defined(__APPLE__)
            const char* home = std::getenv("HOME");
            if (home) {
                config_dir = std::filesystem::path(home) / "Library" / "Application Support" / "Rouen";
            } else {
                config_dir = std::filesystem::current_path();
            }
        #else
            const char* xdg = std::getenv("XDG_CONFIG_HOME");
            if (xdg && xdg[0] != '\0') {
                config_dir = std::filesystem::path(xdg) / "rouen";
            } else {
                const char* home = std::getenv("HOME");
                if (home) {
                    config_dir = std::filesystem::path(home) / ".config" / "rouen";
                } else {
                    config_dir = std::filesystem::current_path();
                }
            }
        #endif

        // Ensure the directory exists
        try {
            std::filesystem::create_directories(config_dir);
        } catch (...) {
            config_dir = std::filesystem::current_path();
        }

        return config_dir;
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
        
        // Use the centralized executable directory function to avoid code duplication
        std::filesystem::path exec_path = get_executable_directory();
        std::filesystem::path current_path = std::filesystem::current_path();
        
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
            if (!resource_subdir.empty()) {
                search_paths.push_back(exec_path / resource_subdir / filename);
            }
            search_paths.push_back(exec_path / "resources" / filename);
            if (!resource_subdir.empty()) {
                search_paths.push_back(exec_path / "resources" / resource_subdir / filename);
            }
        }
         
        // 3. Current working directory
        if (!resource_subdir.empty()) {
            search_paths.push_back(current_path / resource_subdir / filename);
            search_paths.push_back(current_path / "resources" / resource_subdir / filename);
        }
        search_paths.push_back(current_path / filename);
        search_paths.push_back(current_path / "resources" / filename);
         
        // 4. parent directory (for running from build dir)
        if (!resource_subdir.empty()) {
            search_paths.push_back(exec_path.parent_path() / resource_subdir / filename);
            search_paths.push_back(exec_path.parent_path() / "resources" / resource_subdir / filename);
        }
        search_paths.push_back(exec_path.parent_path() / filename);
        search_paths.push_back(exec_path.parent_path() / "resources" / filename);
        
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
        
        // Create platform-specific user data directory
        #if defined(__APPLE__)
            // macOS: ~/Library/Application Support/Rouen/
            std::string home_dir = get_env("HOME");
            if (!home_dir.empty()) {
                user_data_dir = std::filesystem::path(home_dir) / "Library" / "Application Support" / "Rouen";
            } else {
                user_data_dir = std::filesystem::path(".") / ".rouen";
            }
        #elif defined(__linux__)
            // Linux: ~/.local/share/rouen/
            std::string home_dir = get_env("HOME");
            if (!home_dir.empty()) {
                user_data_dir = std::filesystem::path(home_dir) / ".local" / "share" / "rouen";
            } else {
                user_data_dir = std::filesystem::path(".") / ".rouen";
            }
        #elif defined(_WIN32)
            // Windows: Use APPDATA environment variable for user-specific application data
            std::string appdata = get_env("APPDATA");
            if (!appdata.empty()) {
                user_data_dir = std::filesystem::path(appdata) / "Rouen";
            } else {
                // Fallback to USERPROFILE if APPDATA is not available
                std::string userprofile = get_env("USERPROFILE");
                if (!userprofile.empty()) {
                    user_data_dir = std::filesystem::path(userprofile) / "AppData" / "Roaming" / "Rouen";
                } else {
                    // Last resort fallback
                    user_data_dir = std::filesystem::path(".") / ".rouen";
                }
            }
        #else
            // Fallback for other platforms: ~/.rouen/
            std::string home_dir = get_env("HOME");
            if (!home_dir.empty()) {
                user_data_dir = std::filesystem::path(home_dir) / ".rouen";
            } else {
                user_data_dir = std::filesystem::path(".") / ".rouen";
            }
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

    /**
     * Finds an executable on the system by searching the PATH environment variable
     * and common installation directories for Homebrew and Nix profiles.
     */
    inline std::string find_executable(const std::string& name) {
        // If it's already an absolute or relative path, check if it exists
        if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
            if (std::filesystem::exists(name)) {
                return name;
            }
        }
        
        // Define directories to search
        std::vector<std::filesystem::path> search_dirs;
        
        // 1. Check directories in system PATH environment variable
        std::string path_env = get_env("PATH");
        if (!path_env.empty()) {
            std::string item;
            std::stringstream ss(path_env);
#ifdef _WIN32
            char delim = ';';
#else
            char delim = ':';
#endif
            while (std::getline(ss, item, delim)) {
                if (!item.empty()) {
                    search_dirs.push_back(item);
                }
            }
        }
        
        // 2. Add common search paths for Homebrew, Nix, and standard directories
#ifdef _WIN32
        // Windows common paths can be added here if needed
#else
        // macOS / Linux common paths
        search_dirs.push_back("/usr/bin");
        search_dirs.push_back("/bin");
        search_dirs.push_back("/usr/sbin");
        search_dirs.push_back("/sbin");
        search_dirs.push_back("/usr/local/bin");
        search_dirs.push_back("/opt/homebrew/bin");
        
        // Nix profile paths
        std::string home_dir = get_env("HOME");
        if (!home_dir.empty()) {
            search_dirs.push_back(std::filesystem::path(home_dir) / ".nix-profile" / "bin");
        }
        search_dirs.push_back("/nix/var/nix/profiles/default/bin");
        
        std::string user_env = get_env("USER");
        if (!user_env.empty()) {
            search_dirs.push_back(std::filesystem::path("/nix/var/nix/profiles/per-user") / user_env / "profile" / "bin");
        }
#endif
        
        // Search each directory for the executable
        for (const auto& dir : search_dirs) {
#ifdef _WIN32
            std::filesystem::path full_path = dir / (name.ends_with(".exe") ? name : name + ".exe");
#else
            std::filesystem::path full_path = dir / name;
#endif
            if (std::filesystem::exists(full_path)) {
                // Verify it's a regular file and has execute permissions
                try {
                    if (std::filesystem::is_regular_file(full_path)) {
#ifndef _WIN32
                        auto p = std::filesystem::status(full_path).permissions();
                        if ((p & std::filesystem::perms::owner_exec) != std::filesystem::perms::none ||
                            (p & std::filesystem::perms::group_exec) != std::filesystem::perms::none ||
                            (p & std::filesystem::perms::others_exec) != std::filesystem::perms::none) {
                            return full_path.string();
                        }
#else
                        return full_path.string();
#endif
                    }
                } catch (...) {}
            }
        }
        
        // Fall back to original name if not found
        return name;
    }

    // Add function to check if mpv is available in the system
    inline bool check_mpv_availability(std::string& mpv_path) {
        std::string found = find_executable("mpv");
        if (found != "mpv") {
            mpv_path = found;
            return true;
        }
        
        // Check if available in PATH via popen fallback
#ifdef _WIN32
        std::string command = "where mpv.exe 2>nul";
#else
        std::string command = "which mpv 2>/dev/null";
#endif
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
