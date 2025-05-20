#pragma once

#include <string>
#include <unordered_map>
#include <chrono>
#include <filesystem>
#include <thread>
#include <mutex>
#include <atomic>
#include <fstream>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>  // For file control options (O_WRONLY, O_CREAT, O_TRUNC)

#include "./imgui_include.hpp"
#include "../registrar.hpp"
#include "mpv_socket.hpp"
#include "platform_utils.hpp"
#include "../../external/IconsMaterialDesign.h" // Add this line to include Material Design Icons

struct media_player {
    struct item {
        std::string url;
        int player_pid{0};
        bool is_playing{false};
        mpv_socket_helper mpv_socket;
        std::atomic<double> position{0.0};
        std::atomic<double> duration{0.0};
        std::thread position_thread;
        std::atomic<bool> thread_running{false};
        std::mutex data_mutex;  // Add mutex to protect data access

        item() = default;
        
        ~item() {
            stopMedia();
        }

        bool checkMediaStatus() {
            if (player_pid <= 0) return false;
            
            std::string command = "ps -p " + std::to_string(player_pid) + " > /dev/null 2>&1 && echo 1 || echo 0";
            FILE* pipe = popen(command.c_str(), "r");
            std::string result = "";
            
            if (pipe) {
                char buffer[128];
                while (!feof(pipe)) {
                    if (fgets(buffer, 128, pipe) != nullptr)
                        result += buffer;
                }
                pclose(pipe);
            }
            
            // Trim whitespace
            result.erase(0, result.find_first_not_of(" \n\r\t"));
            result.erase(result.find_last_not_of(" \n\r\t") + 1);
            
            is_playing = (result == "1");
            return is_playing;
        }
        
        void stopMedia() {
            // Stop the position tracking thread if it's running
            if (thread_running) {
                thread_running = false;
                if (position_thread.joinable()) {
                    position_thread.join();
                }
            }
            
            // Close socket via helper
            mpv_socket.close_socket();
            
            if (player_pid > 0) {
                // Kill the process using the stored PID
                if (kill(player_pid, SIGTERM) == -1) {
                    perror("Failed to terminate process");
                }
                player_pid = 0;
                is_playing = false;
            }
            
            // Reset playback info
            position = 0.0;
            duration = 0.0;
        }
        
        // Start a thread to periodically update position information
        void startPositionTracking() {
            // Stop any existing thread
            if (thread_running) {
                thread_running = false;
                if (position_thread.joinable()) {
                    position_thread.join();
                }
            }
            
            // Test the socket connection before starting thread
            if (mpv_socket.is_connected()) {
                if (!mpv_socket.test_connection()) {
                    MPV_WARN("Socket test failed before starting position tracking; attempting reconnection");
                    mpv_socket.close_socket();
                    
                    // Try to reinitialize the socket
                    std::string socket_path = mpv_socket.get_socket_path();
                    if (!socket_path.empty() && !mpv_socket.init_socket(socket_path)) {
                        MPV_ERROR("Failed to reconnect to socket; position tracking may not work");
                    }
                }
            } else {
                MPV_WARN("Socket not connected before starting position tracking");
            }
            
            thread_running = true;
            position_thread = std::thread([this]() {
                // Keep track of consecutive errors
                int consecutive_errors = 0;
                
                // Store persistent duration value - we only need to get it once reliably
                double persistent_duration = 0.0;
                bool duration_initialized = false;
                
                while (thread_running) {
                    bool is_running = false;
                    
                    // First check if the player is still running
                    if (player_pid > 0) {
                        is_running = checkMediaStatus();
                        if (!is_running) {
                            // Player has stopped
                            mpv_socket.close_socket();
                            is_playing = false;
                            std::this_thread::sleep_for(std::chrono::milliseconds(500));
                            continue;
                        }
                    } else {
                        // No valid PID, can't continue
                        is_playing = false;
                        mpv_socket.close_socket();
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                        continue;
                    }
                    
                    // Only proceed if socket is valid
                    if (mpv_socket.is_connected()) {
                        // First, if duration is not initialized yet, get it
                        if (!duration_initialized || persistent_duration <= 0) {
                            MPV_TRACE("Requesting duration from MPV socket");
                            // Request duration only
                            if (mpv_socket.send_command("{\"command\":[\"get_property\",\"duration\"],\"request_id\":2}\n")) {
                                // Wait for response - longer wait time for app bundle environment
                                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                                
                                // Get the duration response
                                char buffer[4096];
                                memset(buffer, 0, sizeof(buffer));
                                
                                if (mpv_socket.receive_response(buffer, sizeof(buffer), 500)) { // Longer timeout
                                    std::string response(buffer);
                                    MPV_TRACE_FMT("Received duration response: {}", response);
                                    
                                    // Only look for duration response
                                    if (response.find("\"request_id\":2") != std::string::npos) {
                                        size_t pos = response.find("\"data\":");
                                        if (pos != std::string::npos) {
                                            std::string value = response.substr(pos + 7);
                                            size_t end = value.find_first_of(",}");
                                            if (end != std::string::npos) {
                                                try {
                                                    double dur_value = std::stod(value.substr(0, end));
                                                    if (dur_value > 0) {
                                                        persistent_duration = dur_value;
                                                        duration_initialized = true;
                                                        
                                                        // Update shared duration once we have a good value
                                                        std::lock_guard<std::mutex> lock(data_mutex);
                                                        duration = persistent_duration;
                                                        MPV_INFO_FMT("Duration updated: {:.2f} seconds", persistent_duration);
                                                    }
                                                } catch (const std::exception& e) {
                                                    MPV_ERROR_FMT("Error parsing duration value: {}", e.what());
                                                }
                                            }
                                        }
                                    }
                                } else {
                                    MPV_WARN("Failed to receive duration response");
                                }
                            } else {
                                MPV_WARN("Failed to send duration request command");
                            }
                            
                            // If we couldn't get the duration, wait and try again
                            if (!duration_initialized) {
                                // Try to reconnect the socket if we're having issues
                                if (consecutive_errors > 3) {
                                    MPV_INFO("Trying to reconnect the socket due to duration retrieval issues");
                                    mpv_socket.close_socket();
                                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                                    mpv_socket.init_socket(mpv_socket.get_socket_path());
                                }
                                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                                continue;
                            }
                        }
                        
                        // Now separately request playback position
                        MPV_TRACE("Requesting playback position from MPV socket");
                        if (mpv_socket.send_command("{\"command\":[\"get_property\",\"playback-time\"],\"request_id\":1}\n")) {
                            // Wait for response - longer wait time for app bundle environment
                            std::this_thread::sleep_for(std::chrono::milliseconds(300));
                            
                            // Process position response
                            char buffer[4096];
                            memset(buffer, 0, sizeof(buffer));
                            
                            double current_position = 0.0;
                            bool position_updated = false;
                            
                            if (mpv_socket.receive_response(buffer, sizeof(buffer), 500)) { // Longer timeout
                                std::string response(buffer);
                                MPV_TRACE_FMT("Received position response: {}", response);
                                
                                // Check for playback end events
                                if (response.find("\"event\":\"end-file\"") != std::string::npos || 
                                    response.find("\"event\":\"idle\"") != std::string::npos) {
                                    // Playback has ended
                                    MPV_INFO("Detected playback end event");
                                    is_playing = false;
                                    mpv_socket.close_socket();
                                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                                    continue;
                                }
                                
                                // Look for position response only
                                if (response.find("\"request_id\":1") != std::string::npos) {
                                    size_t pos = response.find("\"data\":");
                                    if (pos != std::string::npos) {
                                        std::string value = response.substr(pos + 7);
                                        size_t end = value.find_first_of(",}");
                                        if (end != std::string::npos) {
                                            try {
                                                current_position = std::stod(value.substr(0, end));
                                                position_updated = true;
                                                MPV_TRACE_FMT("Current position: {:.2f}", current_position);
                                            } catch (const std::exception& e) {
                                                MPV_ERROR_FMT("Error parsing position value: {}", e.what());
                                            }
                                        }
                                    }
                                }
                            } else {
                                // Failed to receive response
                                consecutive_errors++;
                                MPV_WARN_FMT("Failed to receive position response (error count: {})", consecutive_errors);
                                
                                // More aggressive reconnection strategy
                                if (consecutive_errors > 3) {
                                    MPV_INFO("Attempting to reconnect socket due to position retrieval issues");
                                    mpv_socket.close_socket();
                                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                                    
                                    // Try to reconnect
                                    bool reconnected = false;
                                    for (size_t i = 0; i < 2; i++) {
                                        if (mpv_socket.init_socket(mpv_socket.get_socket_path())) {
                                            MPV_INFO("Successfully reconnected to socket");
                                            consecutive_errors = 0;
                                            reconnected = true;
                                            break;
                                        }
                                        std::this_thread::sleep_for(std::chrono::milliseconds(200));
                                    }
                                    
                                    if (!reconnected) {
                                        MPV_ERROR("Failed to reconnect to socket");
                                        consecutive_errors = 0; // Reset to prevent constant reconnection attempts
                                    }
                                    
                                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                                    continue;
                                }
                            }
                            
                            // Update position if we got a valid value
                            if (position_updated) {
                                std::lock_guard<std::mutex> lock(data_mutex);
                                position = current_position;
                                
                                // Reset error counter on successful update
                                consecutive_errors = 0;
                                
                                // Always reassert the persistent duration to make sure it doesn't get overwritten
                                if (duration_initialized && persistent_duration > 0) {
                                    duration = persistent_duration;
                                }
                                
                                // Check if we've reached the end of playback
                                if (current_position >= persistent_duration - 0.5 && persistent_duration > 0) {
                                    MPV_INFO("Reached end of playback based on position");
                                    is_playing = false;
                                    mpv_socket.close_socket();
                                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                                    continue;
                                }
                            } else {
                                consecutive_errors++;
                                MPV_WARN_FMT("Failed to update position (error count: {})", consecutive_errors);
                                
                                if (consecutive_errors > 3) {
                                    // Check if process is still running before giving up
                                    if (checkMediaStatus()) {
                                        MPV_INFO("Media is still playing, retrying socket connection");
                                        // Try to use existing position as a fallback
                                        {
                                            std::lock_guard<std::mutex> lock(data_mutex);
                                            // Increment position by 0.5 seconds as a guess
                                            position += 0.5;
                                        }
                                        
                                        // Try to reconnect socket
                                        mpv_socket.close_socket();
                                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                                        mpv_socket.init_socket(mpv_socket.get_socket_path());
                                    } else {
                                        MPV_INFO("Media is no longer playing, closing socket");
                                        is_playing = false;
                                        mpv_socket.close_socket();
                                    }
                                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                                    continue;
                                }
                            }
                        } else {
                            // Failed to send command
                            consecutive_errors++;
                            if (consecutive_errors > 5) {
                                mpv_socket.close_socket();
                            }
                        }
                    }
                    
                    // Reset error counter on success
                    if (mpv_socket.is_connected()) {
                        consecutive_errors = 0;
                    }
                    
                    // Sleep between updates
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
            });
        }
        
        // Decode a URL-encoded string
        std::string urlDecode(const std::string& encoded) {
            std::string result;
            for (size_t i = 0; i < encoded.length(); ++i) {
                if (encoded[i] == '%' && i + 2 < encoded.length()) {
                    int value;
                    std::istringstream hex_stream(encoded.substr(i + 1, 2));
                    if (hex_stream >> std::hex >> value) {
                        result += static_cast<char>(value);
                        i += 2;
                    } else {
                        result += encoded[i];
                    }
                } else if (encoded[i] == '+') {
                    result += ' ';
                } else {
                    result += encoded[i];
                }
            }
            return result;
        }
        
        // Check if a URL is already encoded
        bool isUrlEncoded(const std::string& input_str) {
            return input_str.find('%') != std::string::npos;
        }
        
        // Sanitize URLs to handle special characters better
        std::string sanitizeURL(const std::string& input_url) {
            std::string processed_str = input_url;
            
            // Skip if it looks like the URL is already properly encoded
            if (isUrlEncoded(processed_str) && processed_str.find(' ') == std::string::npos) {
                return processed_str;
            }
            
            // Identify protocol portion if present to preserve it
            size_t protocol_end = processed_str.find("://");
            std::string protocol_portion;
            std::string path_portion;
            
            if (protocol_end != std::string::npos) {
                protocol_portion = processed_str.substr(0, protocol_end + 3); // Include "://"
                path_portion = processed_str.substr(protocol_end + 3);
            } else {
                // No protocol, treat the entire string as the path portion
                path_portion = processed_str;
            }
            
            // Replace any spaces with %20 in path portion only
            size_t pos = 0;
            while ((pos = path_portion.find(" ", pos)) != std::string::npos) {
                path_portion.replace(pos, 1, "%20");
                pos += 3; // length of "%20"
            }
            
            // Handle other common special characters in path portion only
            static const std::vector<std::pair<std::string, std::string>> replacements = {
                {"\\", "%5C"},
                {"\"", "%22"},
                {"'", "%27"},
                {"`", "%60"},
                {"|", "%7C"},
                {"<", "%3C"},
                {">", "%3E"},
                {"#", "%23"},
                {"&", "%26"},
                {"?", "%3F"},
                {"+", "%2B"},
                {"=", "%3D"},
                {";", "%3B"},
                // Remove ":" from the list to preserve protocols
                {",", "%2C"},
                {"@", "%40"}
            };
            
            for (const auto& [from, to] : replacements) {
                pos = 0;
                while ((pos = path_portion.find(from, pos)) != std::string::npos) {
                    path_portion.replace(pos, from.length(), to);
                    pos += to.length();
                }
            }
            
            // Recombine the protocol and path
            std::string result = protocol_portion + path_portion;
            
            // Ensure no spaces in URL
            if (result != input_url) {
                MPV_INFO_FMT("Sanitized URL: {} -> {}", input_url, result);
            }
            
            return result;
        }
        
        // Validate URLs before attempting to play them
        bool validateURL(const std::string& url_to_check) {
            if (url_to_check.empty()) {
                MPV_ERROR("Cannot play empty URL");
                return false;
            }
            
            // Check for encoded URL protocol (resulting from incorrect sanitization)
            std::string check_url = url_to_check;
            if (check_url.find("%3A//") != std::string::npos || check_url.find("%3a//") != std::string::npos) {
                // Fix URL by replacing %3A// with ://
                MPV_INFO("Found encoded protocol in URL, fixing");
                size_t pos = 0;
                while ((pos = check_url.find("%3A//", pos)) != std::string::npos) {
                    check_url.replace(pos, 5, "://");
                    pos += 3;
                }
                pos = 0;
                while ((pos = check_url.find("%3a//", pos)) != std::string::npos) {
                    check_url.replace(pos, 5, "://");
                    pos += 3;
                }
                // Continue validation with the fixed URL
                MPV_INFO_FMT("Fixed URL: {} -> {}", url_to_check, check_url);
                // Update the class member - we can't modify the parameter since it's const
                const_cast<std::string&>(url) = check_url;
            }
            
            // Check if it's a local file
            if (check_url.find("://") == std::string::npos) {
                // Might be a local file
                if (std::filesystem::exists(check_url)) {
                    MPV_INFO_FMT("URL is a valid local file: {}", check_url);
                    return true;
                } else {
                    MPV_WARN_FMT("URL doesn't contain protocol and isn't a local file: {}", check_url);
                    // Continue anyway as it might be in a special format mpv understands
                }
            }
            
            // Basic protocol check
            bool is_remote_url = false;
            bool is_http = (url_to_check.length() >= 7) && (url_to_check.substr(0, 7) == "http://");
            bool is_https = (url_to_check.length() >= 8) && (url_to_check.substr(0, 8) == "https://");
            bool is_rtsp = (url_to_check.length() >= 6) && (url_to_check.substr(0, 6) == "rtsp://");
            bool is_rtmp = (url_to_check.length() >= 6) && (url_to_check.substr(0, 6) == "rtmp://");
            
            if (is_http || is_https || is_rtsp || is_rtmp) {
                
                MPV_INFO_FMT("URL has valid protocol: {}", url_to_check);
                is_remote_url = true;
                
                // For remote URLs, add a basic connection check to prevent hanging
                if (((url_to_check.length() >= 7) && (url_to_check.substr(0, 7) == "http://")) || 
                    ((url_to_check.length() >= 8) && (url_to_check.substr(0, 8) == "https://"))) {
                    MPV_INFO("Performing quick connection test for remote URL...");
                    
                    // Use curl to check if the URL is accessible with a short timeout
                    std::string check_cmd = "curl -s -I --connect-timeout 3 -m 6 \"" + url_to_check + "\" > /dev/null 2>&1";
                    int result = system(check_cmd.c_str());
                    
                    if (result != 0) {
                        MPV_ERROR_FMT("Connection test failed for URL: {}", url_to_check);
                        MPV_ERROR("URL may be unreachable or slow to respond");
                        
                        // Try with a HEAD request instead
                        MPV_INFO("Trying alternate connection test with GET request...");
                        check_cmd = "curl -s --connect-timeout 4 -m 8 -o /dev/null -L \"" + url_to_check + "\"";
                        result = system(check_cmd.c_str());
                        
                        if (result != 0) {
                            // We'll still try to play it, but we'll warn the user
                            try {
                                "notify"_sfn("Connection test failed for URL. Media may not play correctly.");
                            } catch (...) {
                                // Ignore notification errors
                            }
                        } else {
                            MPV_INFO("Alternate connection test successful");
                        }
                    } else {
                        MPV_INFO("Connection test successful");
                    }
                }
                return true;
            }
            
            if (!is_remote_url) {
                MPV_WARN_FMT("URL has unknown protocol: {}", url_to_check);
            }
            return true; // Let mpv try to handle it anyway
        }

        bool playMedia() {
            try {
                // First validate the URL before attempting to play
                if (!validateURL(url)) {
                    MPV_ERROR("URL validation failed, not attempting playback");
                    return false;
                }
                
                // Sanitize the URL to handle special characters better
                std::string sanitized_url = sanitizeURL(url);
                
                // Stop any current playback
                stopMedia();
                
                // Create a unique socket path using the helper
                std::string socket_path = mpv_socket.create_socket_path();
                
                // Log the socket path for debugging
                MPV_INFO_FMT("Created socket path: {}", socket_path);
                
                // Verify the directory exists and is accessible
                std::filesystem::path socket_dir = std::filesystem::path(socket_path).parent_path();
                if (!std::filesystem::exists(socket_dir)) {
                    MPV_ERROR_FMT("Socket directory does not exist: {}", socket_dir.string());
                    try {
                        std::filesystem::create_directories(socket_dir);
                        MPV_INFO_FMT("Created socket directory: {}", socket_dir.string());
                    } catch (const std::exception& e) {
                        MPV_ERROR_FMT("Failed to create socket directory: {} - {}", socket_dir.string(), e.what());
                        return false;
                    }
                }
                
                // Check if we can create a test file in that directory to verify write permissions
                std::string test_path = (socket_dir / "test_write").string();
                try {
                    std::ofstream test_file(test_path);
                    if (test_file) {
                        test_file << "test";
                        test_file.close();
                        std::filesystem::remove(test_path);
                        MPV_INFO("Successfully verified write permissions to socket directory");
                    } else {
                        MPV_ERROR_FMT("Cannot write to socket directory: {}", socket_dir.string());
                        // Try to use /tmp as fallback
                        socket_dir = "/tmp";
                        MPV_WARN_FMT("Attempting to use fallback socket directory: {}", socket_dir.string());
                        test_path = (socket_dir / "rouen_test_write").string();
                        std::ofstream fallback_test(test_path);
                        if (fallback_test) {
                            fallback_test << "test";
                            fallback_test.close();
                            std::filesystem::remove(test_path);
                            MPV_INFO("Successfully verified write permissions to fallback socket directory");
                        } else {
                            MPV_ERROR_FMT("Cannot write to fallback socket directory: {}", socket_dir.string());
                            return false;
                        }
                    }
                } catch (const std::exception& e) {
                    MPV_ERROR_FMT("Exception when testing write permissions: {}", e.what());
                    return false;
                }
                
                // Verify mpv executable exists and is accessible
                std::string mpv_path;
                bool mpv_found = rouen::platform::check_mpv_availability(mpv_path);
                if (!mpv_found) {
                    MPV_ERROR("MPV executable not found. Please install MPV using 'brew install mpv' or equivalent.");
                    // Create a notification
                    try {
                        "notify"_sfn("MPV not found. Please install MPV using 'brew install mpv'.");
                    } catch (...) {
                        // Ignore notification errors
                    }
                    return false;
                } else {
                    MPV_INFO_FMT("Using MPV executable: {}", mpv_path);
                    
                    // Test MPV capabilities by creating a test socket in /tmp (better permissions)
                    std::string test_socket_path = "/tmp/rouen_test_sock";
                    MPV_INFO_FMT("Testing MPV socket capabilities with: {}", test_socket_path);
                    
                    // Remove test socket if it exists
                    if (std::filesystem::exists(test_socket_path)) {
                        std::filesystem::remove(test_socket_path);
                    }
                    
                    // Run a simple MPV command that creates a socket with more options to help debug
                    // Don't use --verbose as it's not supported by this MPV version, use --msg-level instead
                    std::string test_cmd = mpv_path + " --input-ipc-server=" + test_socket_path + 
                                          " --idle=yes --pause --msg-level=all=v /dev/null";
                    MPV_DEBUG_FMT("Testing socket creation with command: {}", test_cmd);
                    
                    // Run MPV in the background with timeout to prevent hanging
                    std::string background_cmd = test_cmd + " > /tmp/mpv_test_output.log 2>&1 & echo $!";
                    MPV_DEBUG_FMT("Running test command in background: {}", background_cmd);
                    
                    FILE* test_pipe = popen(background_cmd.c_str(), "r");
                    pid_t test_pid = 0;
                    if (test_pipe == nullptr) {
                        MPV_ERROR_FMT("Could not execute test MPV command: {}", strerror(errno));
                    } else {
                        // Get the PID of the background process
                        char buffer[128];
                        if (fgets(buffer, sizeof(buffer), test_pipe) != nullptr) {
                            try {
                                test_pid = std::stoi(buffer);
                                MPV_DEBUG_FMT("Test MPV process PID: {}", test_pid);
                            } catch (...) {
                                MPV_WARN("Failed to parse test MPV process PID");
                            }
                        }
                        pclose(test_pipe);
                        
                        // Wait a brief moment for MPV to start and generate output
                        std::this_thread::sleep_for(std::chrono::milliseconds(300));
                        
                        // Read any output to help debug
                        std::ifstream log_file("/tmp/mpv_test_output.log");
                        if (log_file.is_open()) {
                            // Try to read in reasonable chunks to avoid huge logs causing issues
                            std::string output;
                            std::string line;
                            int max_lines = 50;  // Limit the number of lines to process
                            
                            while (std::getline(log_file, line) && max_lines-- > 0) {
                                output += line + "\n";
                                
                                // Look for any critical error indicators
                                if (line.find("ERROR") != std::string::npos || 
                                    line.find("Error") != std::string::npos ||
                                    line.find("error") != std::string::npos ||
                                    line.find("failed") != std::string::npos ||
                                    line.find("Failed") != std::string::npos) {
                                    MPV_WARN_FMT("Detected potential error in test output: {}", line);
                                }
                            }
                            log_file.close();
                            
                            if (!output.empty()) {
                                MPV_DEBUG_FMT("MPV test output: {}", output);
                            }
                        }
                        
                        // Check if socket was created, with retries
                        bool socket_created = false;
                        for (int i = 0; i < 5; i++) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(200));
                            if (std::filesystem::exists(test_socket_path)) {
                                socket_created = true;
                                break;
                            }
                        }
                        
                        // Always ensure the test process is terminated to prevent hanging
                        if (test_pid > 0) {
                            MPV_DEBUG_FMT("Terminating test MPV process (PID: {})", test_pid);
                            kill(test_pid, SIGTERM);
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            kill(test_pid, SIGKILL); // Make sure it's really gone
                        }
                        
                        // Also try pkill as a backup cleanup method
                        std::string kill_cmd = "pkill -f 'mpv --input-ipc-server=" + test_socket_path + "' 2>/dev/null || true";
                        system(kill_cmd.c_str());
                        
                        if (socket_created) {
                            MPV_INFO("MPV test socket creation successful");
                            
                            // Cleanup socket
                            std::filesystem::remove(test_socket_path);
                        } else {
                            MPV_WARN("MPV test socket creation failed - trying alternative method");
                            
                            // Try with a simpler command that might work better in some environments
                            test_socket_path = "/tmp/mpv_sock_test";
                            
                            // Make sure there's no existing socket with this name
                            if (std::filesystem::exists(test_socket_path)) {
                                std::filesystem::remove(test_socket_path);
                            }
                            
                            // Use a background command with timeout to prevent hanging
                            std::string alt_cmd = mpv_path + " --input-ipc-server=" + test_socket_path + 
                                                " --no-terminal --idle /dev/null > /tmp/mpv_alt_test.log 2>&1 &";
                            
                            // Start the alternative test process
                            MPV_DEBUG_FMT("Running alternative test command: {}", alt_cmd);
                            system(alt_cmd.c_str());
                            
                            // Set up a timeout for the alternative test
                            bool alt_test_timeout = false;
                            auto timeout_start = std::chrono::steady_clock::now();
                            
                            // Wait for socket to appear with timeout
                            while (!std::filesystem::exists(test_socket_path)) {
                                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                                auto elapsed = std::chrono::steady_clock::now() - timeout_start;
                                
                                if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > 2000) {
                                    MPV_WARN("Alternative socket test timed out");
                                    alt_test_timeout = true;
                                    break;
                                }
                            }
                            
                            // Always ensure we kill the test process
                            system(("pkill -f 'mpv --input-ipc-server=" + test_socket_path + "' 2>/dev/null || true").c_str());
                            
                            if (!alt_test_timeout && std::filesystem::exists(test_socket_path)) {
                                MPV_INFO("Alternative MPV test socket creation successful");
                                std::filesystem::remove(test_socket_path);
                            } else {
                                // Check the alternative test log
                                std::ifstream alt_log("/tmp/mpv_alt_test.log");
                                if (alt_log.is_open()) {
                                    std::string alt_output((std::istreambuf_iterator<char>(alt_log)),
                                                        std::istreambuf_iterator<char>());
                                    alt_log.close();
                                    
                                    if (!alt_output.empty()) {
                                        MPV_WARN_FMT("Alternative socket test failed with output: {}", alt_output);
                                    }
                                }
                                
                                MPV_WARN("All socket creation tests failed - socket permissions issue?");
                            }
                        }
                    }
                }
                
                // Directly start mpv process and get its PID
                // We'll use fork and execv to avoid shell quoting issues
                pid_t pid = fork();
                
                // Setup file descriptor socket pair before fork
                int socket_fds[2] = {-1, -1};
                bool use_fd_socket = false;
                
                // We won't use file descriptor sockets since MPV might not support it
                // Instead, we'll use a filesystem-based socket which is more compatible
                use_fd_socket = false;
                
                // Always use filesystem socket path - more compatible across different MPV versions
                if (socket_path.empty()) {
                    socket_path = mpv_socket.create_socket_path();
                }
                MPV_INFO_FMT("Using filesystem socket path: {}", socket_path);
                
                // Clean up any existing socket at this path
                if (std::filesystem::exists(socket_path)) {
                    try {
                        std::filesystem::remove(socket_path);
                        MPV_INFO_FMT("Removed existing socket file: {}", socket_path);
                    } catch (const std::exception& e) {
                        MPV_WARN_FMT("Failed to remove existing socket file: {}", e.what());
                    }
                }
                
                // Though we're not using FD sockets, create an event pipe for parent-child signaling
                int event_pipe[2] = {-1, -1};
                if (pipe(event_pipe) == 0) {
                    MPV_INFO("Created event pipe for process synchronization");
                } else {
                    MPV_WARN_FMT("Failed to create event pipe: {}", strerror(errno));
                }
                
                if (pid == -1) {
                    // Fork failed
                    MPV_ERROR_FMT("Failed to fork process: {}", strerror(errno));
                    if (use_fd_socket) {
                        close(socket_fds[0]);
                        close(socket_fds[1]);
                    }
                    return false;
                } else if (pid == 0) {
                    // Child process
                    int fd = open("/tmp/mpv_output.log", O_WRONLY | O_CREAT | O_TRUNC, 0600);
                    if (fd == -1) {
                        perror("open log file failed");
                        _exit(1);
                    }
                    if (dup2(fd, STDOUT_FILENO) == -1) {
                        perror("dup2 stdout failed");
                        _exit(1);
                    }
                    if (dup2(fd, STDERR_FILENO) == -1) {
                        perror("dup2 stderr failed");
                        _exit(1);
                    }
                    close(fd);
                    
                    // Clean up event pipe if we created one
                    if (event_pipe[0] != -1) {
                        close(event_pipe[0]); // Close read end in child
                    }
                    
                    // No need to configure FD socket as we've switched to filesystem sockets
                    dprintf(STDERR_FILENO, "Child process using filesystem socket: %s\n", socket_path.c_str());
                    
                    // Signal parent that we're ready to continue
                    if (event_pipe[1] != -1) {
                        char ready = 1;
                        if (write(event_pipe[1], &ready, 1) != 1) {
                            dprintf(STDERR_FILENO, "Failed to signal parent process: %s\n", strerror(errno));
                        }
                        close(event_pipe[1]); // Close write end in child after signaling
                    }

                    // Always use filesystem socket method for maximum compatibility
                    std::string socket_arg = "--input-ipc-server=" + socket_path;
                    
                    // Log socket information for debugging
                    dprintf(STDERR_FILENO, "Using filesystem socket path: %s\n", socket_path.c_str());
                    
                    // Check if socket path exists already (it shouldn't) and log warning
                    if (std::filesystem::exists(socket_path)) {
                        dprintf(STDERR_FILENO, "WARNING: Socket path already exists, MPV may fail to create it\n");
                    }

                    // Log URL we're about to play with detailed information
                    dprintf(STDERR_FILENO, "MEDIA URL: %s\n", sanitized_url.c_str());
                    
                    // Check if URL is empty or malformed
                    if (sanitized_url.empty()) {
                        dprintf(STDERR_FILENO, "ERROR: URL is empty\n");
                        _exit(1);
                    }
                    
                    // Check if URL is a valid format (basic check)
                    if (sanitized_url.find("://") == std::string::npos && !std::filesystem::exists(sanitized_url)) {
                        // Check if the URL might have had the protocol encoded
                        if (sanitized_url.find("%3A//") != std::string::npos || 
                            sanitized_url.find("%3a//") != std::string::npos) {
                            // Fix encoding issue in protocol separator
                            std::string fixed_url = sanitized_url;
                            size_t pos = 0;
                            while ((pos = fixed_url.find("%3A//", pos)) != std::string::npos) {
                                fixed_url.replace(pos, 5, "://");
                                pos += 3;
                            }
                            pos = 0;
                            while ((pos = fixed_url.find("%3a//", pos)) != std::string::npos) {
                                fixed_url.replace(pos, 5, "://");
                                pos += 3;
                            }
                            sanitized_url = fixed_url;
                            dprintf(STDERR_FILENO, "Fixed URL protocol encoding: %s\n", sanitized_url.c_str());
                        } else {
                            dprintf(STDERR_FILENO, "WARNING: URL doesn't contain protocol '://' and isn't a local file: %s\n", url.c_str());
                            // Continue anyway as it might be a special format mpv understands
                        }
                    }

                    // Only use --idle=yes for socket test, not for real playback
                    std::vector<const char*> args;
                    // Adding better command line arguments to help with playback
                    args.push_back(mpv_path.c_str());
                    args.push_back("--no-video");
                    args.push_back("--no-terminal");
                    args.push_back("--log-file=/tmp/mpv_full.log"); // Log to a file to help with debugging
                    // args.push_back("--msg-level=all=v");  // Verbose logging to help with debugging
                    args.push_back("--keep-open=always");  // Keep mpv open even if playback fails
                    args.push_back("--idle=once");  // Stay open after file ends
                    
                    // Network related options to improve streaming reliability
                    args.push_back("--force-seekable=yes");  // Try to make stream seekable
                    
                    // MPV expects HTTP error codes without quotes but with properly escaped commas
                    // args.push_back("--stream-lavf-o=reconnect_on_http_error=4xx\\,5xx");  // Reconnect on HTTP errors

                    // Add user agent if URL is HTTP/HTTPS to avoid server rejections
                    if (url.substr(0, 7) == "http://" || url.substr(0, 8) == "https://") {
                        args.push_back("--user-agent=\"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36\"");
                    }
                    
                    args.push_back(socket_arg.c_str());
                    
                    // Final check for URL encoding issues
                    std::string final_url = sanitized_url;
                    if (final_url.find("%3A//") != std::string::npos || final_url.find("%3a//") != std::string::npos) {
                        dprintf(STDERR_FILENO, "Fixing encoded URL protocol before passing to MPV\n");
                        size_t pos = 0;
                        while ((pos = final_url.find("%3A//", pos)) != std::string::npos) {
                            final_url.replace(pos, 5, "://");
                            pos += 3;
                        }
                        pos = 0;
                        while ((pos = final_url.find("%3a//", pos)) != std::string::npos) {
                            final_url.replace(pos, 5, "://");
                            pos += 3;
                        }
                        dprintf(STDERR_FILENO, "Fixed URL: %s -> %s\n", sanitized_url.c_str(), final_url.c_str());
                    }
                    
                    args.push_back(final_url.c_str());
                    args.push_back(nullptr);

                    dprintf(STDERR_FILENO, "EXECUTING MPV WITH ARGS:\n");
                    for (size_t i = 0; args[i] != nullptr; i++) {
                        dprintf(STDERR_FILENO, "ARG[%zu]: %s\n", i, args[i]);
                    }
                    // Print a more complete command line that shows all arguments
                    std::string full_cmd = mpv_path + " ";
                    for (size_t i = 1; args[i] != nullptr; i++) {
                        if (i > 1) full_cmd += " ";
                        
                        // URL needs quotes in the debug output
                        if (args[i] == url.c_str()) {
                            full_cmd += "\"" + std::string(args[i]) + "\"";
                        } else {
                            full_cmd += args[i];
                        }
                    }
                    dprintf(STDERR_FILENO, "FULL COMMAND: %s\n", full_cmd.c_str());

                    // Flush before exec
                    fsync(STDERR_FILENO);
                    
                    // Create a properly null-terminated array of C strings for execv
                    char** exec_args = new char*[args.size()];
                    for (size_t i = 0; i < args.size(); i++) {
                        if (args[i] == nullptr) {
                            exec_args[i] = nullptr;
                        } else {
                            exec_args[i] = strdup(args[i]);
                        }
                    }

                    // Execute mpv with properly allocated C-style arguments
                    execv(mpv_path.c_str(), exec_args);

                    // If we get here, execv failed
                    dprintf(STDERR_FILENO, "EXECV FAILED: %s\n", strerror(errno));
                    perror("execv failed");
                    
                    // Clean up in case execv fails
                    for (size_t i = 0; exec_args[i] != nullptr; i++) {
                        free(exec_args[i]);
                    }
                    delete[] exec_args;
                    
                    _exit(1);
                }
                
                // Parent process
                if (pid > 0) {
                    MPV_INFO_FMT("Started MPV with PID: {}", pid);
                
                    // Flag to track if socket connection has been initialized
                    bool socket_initialized = false;
                
                    // Close event pipe write end in parent if we created one
                    if (event_pipe[1] != -1) {
                        close(event_pipe[1]);
                        // We can optionally wait for child to signal readiness through event_pipe[0]
                    }
                    
                    // For filesystem socket method, wait for socket file to appear and connect to it
                    MPV_INFO_FMT("Waiting for MPV to create socket at: {}", socket_path);
                    
                    // Set up a watchdog in a separate thread to prevent hanging indefinitely
                    std::atomic<bool> watchdog_triggered{false};
                    
                    // Use shared_ptr for safer thread management
                    auto watchdog_thread = std::make_shared<std::thread>([&watchdog_triggered, pid]() {
                        // Progressive watchdog approach - check at regular intervals instead of one long wait
                        for (int check = 0; check < 10; ++check) {
                            // Wait in small increments to be more responsive
                            std::this_thread::sleep_for(std::chrono::milliseconds(500));
                            
                            if (watchdog_triggered) {
                                MPV_DEBUG("Watchdog: Socket connection succeeded or properly handled");
                                return;  // Exit early if connection was successful
                            }
                            
                            // On later checks (after 2.5s), check process status
                            if (check >= 5) {
                                // Check if process is still running
                                if (kill(pid, 0) != 0) {
                                    // Process already exited
                                    MPV_ERROR("Watchdog: MPV process has terminated early");
                                    watchdog_triggered = true;  // Mark as handled
                                    return;
                                }
                            }
                        }
                        
                        // If we reach here, we've waited 5 seconds with no success
                        if (!watchdog_triggered) {
                            MPV_ERROR("Watchdog timeout (5s) while waiting for socket creation");
                            
                            // Check process one more time before terminating
                            if (kill(pid, 0) == 0) {
                                MPV_ERROR("MPV process is running but socket creation timed out - terminating");
                                
                                // Force terminate the process to prevent hanging
                                kill(pid, SIGTERM);
                                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                                kill(pid, SIGKILL);
                            }
                            watchdog_triggered = true;  // Mark as handled
                        }
                    });
                    
                    // Create a cleanup helper for the watchdog thread
                    struct WatchdogGuard {
                        std::shared_ptr<std::thread> thread;
                        std::atomic<bool>& trigger;
                        
                        WatchdogGuard(std::shared_ptr<std::thread> t, std::atomic<bool>& trig) 
                            : thread(t), trigger(trig) {}
                            
                        ~WatchdogGuard() {
                            // Signal thread to exit and wait for it
                            trigger = true;
                            if (thread && thread->joinable()) {
                                thread->join();
                            }
                        }
                    } watchdog_guard(watchdog_thread, watchdog_triggered);
                    
                    // Progressive waiting approach to ensure MPV has time to create the socket
                    bool socket_connected = false;
                    constexpr int max_attempts = 10;
                    
                    for (int wait_attempt = 1; wait_attempt <= max_attempts && !watchdog_triggered; wait_attempt++) {
                        // Wait with increasing times (start with shorter waits)
                        std::this_thread::sleep_for(std::chrono::milliseconds(200 * wait_attempt));
                        
                        // Check if socket file exists
                        if (std::filesystem::exists(socket_path)) {
                            MPV_INFO("Socket file created by MPV, attempting to connect");
                            
                            // Give a tiny bit more time for the socket to be ready for connections
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            
                            // Try to initialize the socket connection
                            if (mpv_socket.init_socket(socket_path)) {
                                MPV_INFO("Successfully connected to MPV socket");
                                socket_initialized = true;
                                
                                // Test that we can actually communicate
                                if (mpv_socket.test_connection()) {
                                    MPV_INFO("Socket communication test successful");
                                    socket_connected = true;
                                    break;
                                } else {
                                    MPV_WARN("Socket exists but communication test failed");
                                    mpv_socket.close_socket(); // Reset for retry
                                }
                            } else {
                                MPV_WARN_FMT("Socket file exists but connection failed (attempt {}/{})", 
                                           wait_attempt, max_attempts);
                            }
                        } else {
                            MPV_DEBUG_FMT("Waiting for socket file (attempt {}/{})", wait_attempt, max_attempts);
                            
                            // Check if the process is still running
                            if (wait_attempt % 3 == 0) { // Check process every 3rd attempt
                                std::string check_cmd = "ps -p " + std::to_string(pid) + " > /dev/null 2>&1 && echo 1 || echo 0";
                                FILE* check_pipe = popen(check_cmd.c_str(), "r");
                                char buffer[10];
                                std::string result;
                                
                                if (check_pipe) {
                                    if (fgets(buffer, sizeof(buffer), check_pipe) != nullptr) {
                                        result = buffer;
                                    }
                                    pclose(check_pipe);
                                    
                                    // Trim whitespace
                                    result.erase(0, result.find_first_not_of(" \n\r\t"));
                                    result.erase(result.find_last_not_of(" \n\r\t") + 1);
                                    
                                    if (result != "1") {
                                        MPV_ERROR("MPV process terminated early - playback likely failed to start");
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    
                    // Signal the watchdog that we're done waiting
                    watchdog_triggered = true;
                    
                    if (!socket_connected) {
                        MPV_ERROR("Failed to establish socket communication with MPV after multiple attempts");
                        
                        // Check if the MPV process is still alive
                        if (checkMediaStatus()) {
                            MPV_INFO("MPV process is still running despite socket connection failure");
                            
                            // Check MPV logs for any error messages
                            try {
                                std::ifstream log_file("/tmp/mpv_full.log");
                                if (log_file.is_open()) {
                                    std::string line;
                                    int line_count = 0;
                                    MPV_ERROR("Checking MPV logs for errors:");
                                    
                                    while (std::getline(log_file, line) && line_count < 20) {
                                        if (line.find("error") != std::string::npos || 
                                            line.find("ERROR") != std::string::npos || 
                                            line.find("failed") != std::string::npos || 
                                            line.find("fail") != std::string::npos ||
                                            line.find("cannot") != std::string::npos) {
                                            MPV_ERROR_FMT("MPV log error: {}", line);
                                            line_count++;
                                        }
                                    }
                                    log_file.close();
                                }
                            } catch (const std::exception& e) {
                                MPV_ERROR_FMT("Failed to read MPV log: {}", e.what());
                            }
                            
                            // Try to proceed anyway - the socket might be created later
                            socket_initialized = true;
                        } else {
                            MPV_ERROR("MPV process is not running - playback failed to start");
                        }
                    }
                
                    // Store the process ID for later termination
                    player_pid = pid;
                    is_playing = true;
                    
                    // Start a more advanced watchdog timer to prevent hanging
                    std::thread([this_ptr=this, pid, sanitized_url]() {
                        int check_count = 0;
                        bool notified_user = false;
                        
                        // Check periodically rather than just once
                        for (int i = 0; i < 4; i++) {  // Check 4 times over 8 seconds - reduced time to be more responsive
                            // Wait between checks
                            std::this_thread::sleep_for(std::chrono::seconds(2)); // Reduced sleep time
                            
                            // Check if process is still active
                            if (this_ptr->player_pid != pid || !this_ptr->is_playing) {
                                MPV_INFO("Watchdog: Process has already finished or changed");
                                return;  // Exit the watchdog since the process is already done
                            }
                            
                            // Check for playback progress
                            check_count++;
                            
                            // On the first check, just ensure the process is still running
                            if (check_count == 1) {
                                if (!this_ptr->checkMediaStatus()) {
                                    MPV_ERROR("Watchdog: Process terminated prematurely");
                                    return; // Process already terminated
                                }
                            } 
                            // On later checks, verify the socket is connected too
                            else if (!this_ptr->mpv_socket.is_connected()) {
                                MPV_ERROR("Watchdog detected potential hanging playback");
                                
                                // Try to notify user once
                                if (!notified_user) {
                                    try {
                                        "notify"_sfn("Media playback issues detected. Attempting recovery...");
                                        notified_user = true;
                                    } catch (...) {
                                        // Ignore notification errors
                                    }
                                }
                                
                                // On the third check, terminate the process if still not working
                                if (i >= 2) {
                                    MPV_WARN("Terminating potentially hung process");
                                    kill(pid, SIGTERM);
                                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                                    kill(pid, SIGKILL); // Make sure it's really gone
                                    
                                    try {
                                        "notify"_sfn("Media playback timed out and was terminated.");
                                    } catch (...) {
                                        // Ignore notification errors
                                    }
                                    return;
                                }
                            } else {
                                MPV_INFO("Watchdog: Playback socket connection successful");
                                // Socket is connected, everything is fine, but keep monitoring 
                                // for a short while longer to ensure stability
                                if (i >= 2) {
                                    return; // Exit watchdog if things are stable after a couple of checks
                                }
                            }
                        }
                    }).detach();
                
                    // Check if process is actually running
                    if (!checkMediaStatus()) {
                        MPV_ERROR("MPV process started but immediately terminated");
                        
                        // Check the output log if it exists - use a more robust method
                        MPV_ERROR("Attempting to read MPV output log");
                        std::string error_message;
                        bool found_error = false;
                        
                        try {
                            std::ifstream log_file("/tmp/mpv_output.log");
                            if (log_file.is_open()) {
                                std::string line;
                                MPV_ERROR("MPV output log contents:");
                                int log_line_count = 0;
                                
                                while (std::getline(log_file, line) && log_line_count < 50) {
                                    if (!line.empty()) {
                                        MPV_ERROR_FMT("MPV log: {}", line);
                                        log_line_count++;
                                        
                                        // Look for common error patterns
                                        if (line.find("error") != std::string::npos || 
                                            line.find("ERROR") != std::string::npos || 
                                            line.find("failed") != std::string::npos || 
                                            line.find("Failed") != std::string::npos || 
                                            line.find("Could not") != std::string::npos) {
                                            
                                            error_message = line;
                                            found_error = true;
                                        }
                                        
                                        // Look specifically for network-related issues
                                        if (line.find("Network error") != std::string::npos || 
                                            line.find("Name resolution") != std::string::npos ||
                                            line.find("Failed to resolve") != std::string::npos ||
                                            line.find("Connection refused") != std::string::npos ||
                                            line.find("SSL") != std::string::npos ||
                                            line.find("timeout") != std::string::npos) {
                                            
                                            MPV_ERROR("Detected network connectivity issue in log");
                                            try {
                                                "notify"_sfn("Network connectivity issue: " + line);
                                            } catch (...) {
                                                // Ignore notification errors
                                            }
                                        }
                                    }
                                }
                                log_file.close();
                            } else {
                                MPV_ERROR("Could not open MPV output log");
                            }
                        } catch (const std::exception& e) {
                            MPV_ERROR_FMT("Exception when reading MPV log: {}", e.what());
                        }
                        
                        // If we found a specific error, notify the user
                        if (found_error) {
                            try {
                                // Truncate long error messages
                                if (error_message.length() > 100) {
                                    error_message = error_message.substr(0, 97) + "...";
                                }
                                "notify"_sfn("Media playback failed: " + error_message);
                            } catch (...) {
                                // Ignore notification errors
                            }
                        } else {
                            // Generic error notification
                            try {
                                "notify"_sfn("Media playback failed. The media file may be unreachable or invalid.");
                            } catch (...) {
                                // Ignore notification errors
                            }
                        }
                        
                        // Try an alternative approach: attempt to use mpv with a timeout option
                        MPV_INFO("Attempting to play URL with alternative settings...");
                        std::string alt_opts = " --no-video --cache=yes --demuxer-max-back-bytes=10000000 --demuxer-readahead-secs=30 --stream-lavf-o=reconnect=1 --network-timeout=15";
                        std::string test_cmd = mpv_path + alt_opts + " \"" + sanitized_url + "\" > /tmp/mpv_alt_test.log 2>&1 &";
                        system(test_cmd.c_str());  // system() requires a C-string
                        
                        player_pid = 0;
                        is_playing = false;
                        return false;
                    }
                
                    MPV_INFO_FMT("MPV process is running with PID: {}", pid);
                
                    // Initialize the socket connection and start position tracking
                    MPV_INFO("Attempting to initialize socket connection for progress tracking");
                
                    // Give MPV a moment to initialize before connecting to socket
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                
                    // First check if the socket file was created
                    size_t max_socket_wait = 15; // Wait up to 7.5 seconds for socket creation
                    bool socket_exists = false;
                
                    for (size_t i = 0; i < max_socket_wait; i++) {
                        // Check if MPV process is still running (check child status)
                        int status = 0;
                        pid_t result = waitpid(pid, &status, WNOHANG);
                        if (result == pid) {
                            MPV_ERROR("MPV child process exited while waiting for socket");
                            if (WIFEXITED(status)) {
                                MPV_ERROR_FMT("MPV exited with status: %d", WEXITSTATUS(status));
                            } else if (WIFSIGNALED(status)) {
                                MPV_ERROR_FMT("MPV killed by signal: %d", WTERMSIG(status));
                            }
                            break;
                        }
                        if (!checkMediaStatus()) {
                            MPV_ERROR("MPV process terminated while waiting for socket");
                            break;
                        }
                        if (mpv_socket.socket_exists()) {
                            socket_exists = true;
                            MPV_INFO_FMT("Socket file was created successfully: {}", socket_path);
                            // Check socket file permissions
                            try {
                                auto perms = std::filesystem::status(socket_path).permissions();
                                MPV_DEBUG("Socket permissions:");
                                MPV_DEBUG_FMT("  Owner read:  {}", 
                                           (perms & std::filesystem::perms::owner_read) != std::filesystem::perms::none);
                                MPV_DEBUG_FMT("  Owner write: {}", 
                                           (perms & std::filesystem::perms::owner_write) != std::filesystem::perms::none);
                                MPV_DEBUG_FMT("  Group read:  {}", 
                                           (perms & std::filesystem::perms::group_read) != std::filesystem::perms::none);
                                MPV_DEBUG_FMT("  Group write: {}", 
                                           (perms & std::filesystem::perms::group_write) != std::filesystem::perms::none);
                            } catch (const std::exception& e) {
                                MPV_WARN_FMT("Could not check socket permissions: {}", e.what());
                            }
                            break;
                        }
                        MPV_DEBUG_FMT("Waiting for socket file to be created (attempt {}/{})", i+1, max_socket_wait);
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    }
                    
                    if (!socket_exists) {
                        MPV_ERROR("Socket file was not created after waiting; MPV process might have failed");
                        // Check MPV output log
                        std::ifstream log_file("/tmp/mpv_output.log");
                        if (log_file.is_open()) {
                            std::string line;
                            MPV_ERROR("MPV output log contents:");
                            while (std::getline(log_file, line)) {
                                MPV_ERROR_FMT("MPV log: {}", line);
                            }
                            log_file.close();
                        } else {
                            MPV_ERROR("Could not open MPV output log");
                        }
                        
                        // Search for socket files that might have been created elsewhere
                        std::string search_cmd = "find /tmp -name 'mpv-socket*' -o -name 'm*' -type s 2>/dev/null";
                        MPV_DEBUG_FMT("Searching for socket files in /tmp: {}", search_cmd);
                        FILE* search_pipe = popen(search_cmd.c_str(), "r");
                        if (search_pipe) {
                            char buffer[1024];
                            MPV_DEBUG("Socket files found in /tmp:");
                            bool found = false;
                            while (fgets(buffer, sizeof(buffer), search_pipe) != nullptr) {
                                // Remove trailing newline
                                size_t len = strlen(buffer);
                                if (len > 0 && buffer[len-1] == '\n') {
                                    buffer[len-1] = '\0';
                                }
                                MPV_DEBUG_FMT("  Found socket: {}", buffer);
                                found = true;
                            }
                            if (!found) {
                                MPV_DEBUG("  No socket files found in /tmp");
                            }
                            pclose(search_pipe);
                        }
                        
                        // Also check home directory
                        std::string home_dir = rouen::platform::get_env("HOME");
                        if (!home_dir.empty()) {
                            std::string home_search_cmd = "find " + home_dir + "/Library -name 'm*' -type s 2>/dev/null | head -10";
                            MPV_DEBUG_FMT("Searching for socket files in home directory: {}", home_search_cmd);
                            FILE* home_search_pipe = popen(home_search_cmd.c_str(), "r");
                            if (home_search_pipe) {
                                char buffer[1024];
                                MPV_DEBUG("Socket files found in home directory:");
                                bool found = false;
                                while (fgets(buffer, sizeof(buffer), home_search_pipe) != nullptr) {
                                    // Remove trailing newline
                                    size_t len = strlen(buffer);
                                    if (len > 0 && buffer[len-1] == '\n') {
                                        buffer[len-1] = '\0';
                                    }
                                    MPV_DEBUG_FMT("  Found socket: {}", buffer);
                                    found = true;
                                }
                                if (!found) {
                                    MPV_DEBUG("  No socket files found in home directory");
                                }
                                pclose(home_search_pipe);
                            }
                        }
                    }
                    
                    // Try multiple times to initialize the socket connection with timeout
                    // Use the previously declared socket_initialized variable
                    socket_initialized = socket_initialized || use_fd_socket; // Already initialized if using file descriptor socket
                    bool socket_timeout = false;
                    std::atomic<bool> timeout_occurred{false};
                    
                    // Create a timeout thread to prevent socket initialization from hanging indefinitely
                    // Use a shared_ptr to manage the thread so it's properly cleaned up
                    auto timeout_thread_ptr = std::make_shared<std::thread>([&timeout_occurred, &socket_timeout, pid=player_pid]() {
                        std::this_thread::sleep_for(std::chrono::seconds(5)); // Reduce timeout to 5 seconds for better responsiveness
                        if (!socket_timeout) {
                            timeout_occurred = true;
                            MPV_ERROR("Socket connection timeout occurred");
                            
                            // Check if process is still running
                            bool process_running = false;
                            if (pid > 0) {
                                std::string check_cmd = "ps -p " + std::to_string(pid) + " > /dev/null 2>&1 && echo 1 || echo 0";
                                FILE* check_pipe = popen(check_cmd.c_str(), "r");
                                if (check_pipe) {
                                    char buffer[128];
                                    if (fgets(buffer, sizeof(buffer), check_pipe) != nullptr) {
                                        process_running = (std::string(buffer).find("1") != std::string::npos);
                                    }
                                    pclose(check_pipe);
                                }
                                
                                // If process is running but socket connection timed out,
                                // try to kill and restart it
                                if (process_running) {
                                    MPV_WARN("Terminating potentially hung MPV process");
                                    kill(pid, SIGTERM);
                                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                                    // Make sure it's really gone with SIGKILL
                                    kill(pid, SIGKILL);
                                }
                            }
                            
                            // Try to notify the user about the timeout
                            try {
                                "notify"_sfn("Media playback connection timed out. Please try again or check if MPV is installed correctly.");
                            } catch (...) {
                                // Ignore notification errors
                            }
                        }
                    });
                    
                    // Make sure we join the timeout thread before function returns
                    struct ThreadGuard {
                        std::shared_ptr<std::thread> t;
                        bool& timeout;
                        ThreadGuard(std::shared_ptr<std::thread> thread, bool& to) : t(thread), timeout(to) {}
                        ~ThreadGuard() {
                            timeout = true;
                            if (t && t->joinable()) {
                                t->join();
                            }
                        }
                    } thread_guard(timeout_thread_ptr, socket_timeout);
                    
                    // Limit the number of retries and ensure we respect the timeout
                    for (int attempt = 1; attempt <= max_attempts && !timeout_occurred; attempt++) {
                        MPV_INFO_FMT("Socket connection attempt {} of {}", attempt, max_attempts);
                        
                        // Check if the socket has already been initialized during our earlier connection attempt
                        if (socket_initialized) {
                            MPV_INFO("Socket already initialized, starting position tracking");
                            startPositionTracking();
                            break;
                        }
                    
                        // Add a non-blocking check if the process is still running
                    if (!checkMediaStatus()) {
                        MPV_ERROR("MPV process terminated during socket connection attempts");
                        break;
                    }
                    
                    if (mpv_socket.init_socket(socket_path)) {
                        socket_initialized = true;
                        MPV_INFO("Socket connection successful, starting position tracking");
                        startPositionTracking();
                        break;
                    }
                    MPV_WARN_FMT("Socket connection attempt {} failed, retrying...", attempt);
                    
                    // Shorter waits between attempts
                    std::this_thread::sleep_for(std::chrono::milliseconds(300));
                }
                
                if (!socket_initialized) {
                    MPV_ERROR("Failed to initialize socket after multiple attempts");
                        
                        // Check if the MPV process is still running
                        if (!checkMediaStatus()) {
                            MPV_ERROR("MPV process has terminated");
                            player_pid = 0;
                            is_playing = false;
                            
                            // Add user notification
                            try {
                                "notify"_sfn("Media playback failed. The media file may be unreachable or invalid.");
                            } catch (...) {
                                // Ignore notification errors
                            }
                            return false;
                        }
                        
                        // Check the mpv log for any helpful error messages
                        try {
                            std::ifstream log_file("/tmp/mpv_output.log");
                            if (log_file.is_open()) {
                                std::string line;
                                bool found_error = false;
                                while (std::getline(log_file, line)) {
                                    if (line.find("error") != std::string::npos || 
                                        line.find("ERROR") != std::string::npos || 
                                        line.find("failed") != std::string::npos ||
                                        line.find("Failed") != std::string::npos) {
                                        MPV_ERROR_FMT("MPV error detected: {}", line);
                                        found_error = true;
                                        
                                        // Notify the user about the specific error
                                        try {
                                            std::string error_msg = "Playback issue: " + line;
                                            if (error_msg.length() > 100) {
                                                error_msg = error_msg.substr(0, 97) + "...";
                                            }
                                            "notify"_sfn(error_msg);
                                        } catch (...) {
                                            // Ignore notification errors
                                        }
                                    }
                                }
                                if (!found_error) {
                                    MPV_WARN("No specific errors found in MPV log");
                                }
                                log_file.close();
                            }
                        } catch (const std::exception& e) {
                            MPV_ERROR_FMT("Failed to check MPV log: {}", e.what());
                        }
                        
                        // If the process is still running but we couldn't connect to the socket,
                        // we'll still return true but log a warning
                        MPV_WARN("MPV process is running but socket connection failed; playback status tracking may not work");
                        
                        // Let the user know that position tracking may not work
                        try {
                            "notify"_sfn("Media may be playing but position tracking is unavailable.");
                        } catch (...) {
                            // Ignore notification errors
                        }
                    }
                    
                    // Start a watchdog timer to prevent hanging
                    std::thread([pid=player_pid]() {
                        // Check at regular intervals instead of one long wait
                        for (int i = 0; i < 3; i++) {
                            // Wait in shorter segments
                            std::this_thread::sleep_for(std::chrono::seconds(5));
                            
                            // Check if the process is responding (check twice to confirm hanging)
                            std::string check_cmd = "ps -p " + std::to_string(pid) + " > /dev/null 2>&1 && echo 1 || echo 0";
                            FILE* check_pipe = popen(check_cmd.c_str(), "r");
                            std::string result = "";
                            
                            if (check_pipe) {
                                char buffer[128];
                                if (fgets(buffer, 128, check_pipe) != nullptr) {
                                    result += buffer;
                                }
                                pclose(check_pipe);
                                
                                result.erase(0, result.find_first_not_of(" \n\r\t"));
                                result.erase(result.find_last_not_of(" \n\r\t") + 1);
                                
                                bool is_running = (result == "1");
                                if (!is_running) {
                                    // Process already terminated, no need to continue watchdog
                                    return;
                                }
                                
                                // On the final check, test if the process is hanging
                                if (i == 2) {
                                    // Check if it's consuming CPU - if not, it might be hung
                                    std::string cpu_cmd = "ps -o %cpu -p " + std::to_string(pid) + " | tail -n1";
                                    FILE* cpu_pipe = popen(cpu_cmd.c_str(), "r");
                                    std::string cpu_usage = "";
                                    
                                    if (cpu_pipe) {
                                        char cpu_buffer[32];
                                        if (fgets(cpu_buffer, sizeof(cpu_buffer), cpu_pipe) != nullptr) {
                                            cpu_usage = cpu_buffer;
                                        }
                                        pclose(cpu_pipe);
                                        
                                        // Clean up result
                                        cpu_usage.erase(0, cpu_usage.find_first_not_of(" \n\r\t"));
                                        cpu_usage.erase(cpu_usage.find_last_not_of(" \n\r\t") + 1);
                                        
                                        try {
                                            double cpu = std::stod(cpu_usage);
                                            
                                            // If CPU usage is low, check socket file status
                                            if (cpu < 1.0) {
                                                // Kill the process - it's likely hung
                                                kill(pid, SIGKILL);
                                                
                                                // Try to notify the user
                                                try {
                                                    "notify"_sfn("Media playback appears to be hung and was terminated.");
                                                } catch (...) {
                                                    // Ignore notification errors
                                                }
                                            }
                                        } catch (...) {
                                            // Failed to parse CPU usage, terminate to be safe
                                            kill(pid, SIGKILL);
                                        }
                                    }
                                }
                            }
                        }
                    }).detach();
                }
            } catch (const std::exception& e) {
                MPV_ERROR_FMT("Exception in playMedia: {}", e.what());
                player_pid = 0;
                is_playing = false;
                return false;
            }
            return true;
        }

        // Format time in MM:SS format
        std::string formatTime(double seconds) const {
            int mins = static_cast<int>(seconds) / 60;
            int secs = static_cast<int>(seconds) % 60;
            return std::format("{:02d}:{:02d}", mins, secs);
        }
        
        // Seek to a specific position in the media
        bool seekTo(auto position_seconds) {
            if (!is_playing || !mpv_socket.is_connected()) {
                return false;
            }
            
            // Format the seek command with the target position
            std::string seek_cmd = std::format("{{\"command\":[\"set_property\",\"playback-time\",{:.2f}],\"request_id\":3}}\n", 
                                              position_seconds);
            
            // Send the seek command to mpv
            return mpv_socket.send_command(seek_cmd);
        }
    };

    using item_map = std::unordered_map<ImGuiID, item>;

    static item_map & items() {
        static item_map items_;
        return items_;
    }

    static void stopAll() {
        for (auto &[k,v]: items()) {
            v.stopMedia();
        }
    }

    static void player(std::string_view url, auto info_color, std::string_view title = "Media") noexcept {
        (void)info_color; // Suppress unused parameter warning if not used
        ImGui::PushID(url.data());
        try {
            auto &item {items()[ImGui::GetID("MediaPlayer")]};
            item.url = url;
            // Check if media is currently playing
            if (item.player_pid > 0) {
                item.checkMediaStatus(); // Update playback status
            }
            
            if (item.is_playing) {
                ImGui::TextUnformatted(title.data());
                // Get safe copies of position and duration values with mutex protection
                double current_pos, current_dur;
                {
                    std::lock_guard<std::mutex> lock(item.data_mutex);
                    current_pos = item.position;
                    current_dur = item.duration;
                }
                
                if (current_pos > 0 && current_dur > 0) {
                    // Format and display playback time
                    ImGui::TextColored(info_color, "Playing: %s / %s", 
                        item.formatTime(current_pos).c_str(),
                        item.formatTime(current_dur).c_str());
                }
                // Stop button with Material Design icon instead of Unicode square
                if (ImGui::Button(std::format(" {} ", ICON_MD_STOP).c_str())) {
                    item.stopMedia();
                }
                // Show playback position if available
                ImGui::SameLine();
                if (current_dur > 0) {
                    // Draw a progress bar with safe calculation
                    float progress = current_pos > 0 && current_dur > 0 ? 
                        static_cast<float>(current_pos / current_dur) : 0.0f;
                    
                    // Clamp progress to 0.0-1.0 range
                    progress = std::max(0.0f, std::min(1.0f, progress));
                    
                    // Store the cursor position before the progress bar
                    ImVec2 progress_bar_pos = ImGui::GetCursorScreenPos();
                    ImVec2 progress_bar_size = ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
                    
                    // Draw the progress bar
                    ImGui::ProgressBar(progress, ImVec2(-1, 0), "");
                    
                    // Check if the user clicked on the progress bar
                    if (ImGui::IsItemClicked()) {
                        // Calculate the normalized position (0.0 to 1.0) based on mouse X position
                        auto mouse_x = ImGui::GetIO().MousePos.x;
                        auto rel_x = (mouse_x - progress_bar_pos.x) / progress_bar_size.x;
                        rel_x = std::max(0.0f, std::min(1.0f, rel_x)); // Clamp to valid range
                        
                        // Convert to seconds and seek to that position
                        auto target_pos = static_cast<double>(rel_x) * current_dur;
                        item.seekTo(target_pos);
                    }
                } else {
                    ImGui::ProgressBar(0.0f, ImVec2(-1, 0), "Loading...");
                }
            } else {
                // Set text alignment to left-aligned before creating the button
                ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
                // Play button with Material Design icon instead of Unicode triangle
                if (ImGui::Button(std::format(" {} {}", ICON_MD_PLAY_ARROW, title).c_str(), ImVec2(-1, 0))) {
                    stopAll();
                    item.playMedia();
                }
                // Restore default style
                ImGui::PopStyleVar();
            }
        }
        catch (const std::exception& e) {
            "notify"_sfn(e.what());
        }
        ImGui::PopID();
    }
    // Closing media_player struct properly
}; // end of struct media_player

// --- Alarm sound helpers (single static alarm_item for all alarm sound control) ---
namespace media_player_alarm_helper {
    // Shared alarm item for all alarm sound helpers
    static media_player::item& alarm_item_instance() {
        static media_player::item alarm_item;
        return alarm_item;
    }
    // Play a local sound file in a loop (for alarm repeat)
    static void play_sound_loop(std::string_view file_path) {
        auto& alarm_item = alarm_item_instance();
        // Use resource path to find the file in both development and app bundle
        auto resource_path = rouen::platform::get_resource_path(std::string(file_path), "");
        alarm_item.url = resource_path.string();
        alarm_item.stopMedia(); // Stop any previous sound
        
        // Create a unique socket path in user-accessible directory
        std::string socket_path = alarm_item.mpv_socket.create_socket_path();
        MPV_INFO_FMT("Alarm: Created socket path: {}", socket_path);
        
        // Determine best path to mpv executable
        std::string mpv_path;
        bool mpv_found = rouen::platform::check_mpv_availability(mpv_path);
        if (!mpv_found) {
            MPV_ERROR("Alarm: MPV executable not found. Please install MPV using 'brew install mpv' or equivalent.");
            try {
                "notify"_sfn("Cannot play alarm: MPV not found. Please install MPV using 'brew install mpv'.");
            } catch (...) {
                // Ignore notification errors
            }
            return;
        } else {
            MPV_INFO_FMT("Alarm: Using MPV executable: {}", mpv_path);
        }
        
        // We'll use fork/exec like in playMedia for more reliable process creation
        pid_t pid = fork();
        
        if (pid == -1) {
            MPV_ERROR_FMT("Alarm: Failed to fork process: {}", strerror(errno));
            return;
        } else if (pid == 0) {
            // Child process
            // Redirect output to log file
            int fd = open("/tmp/mpv_alarm_output.log", O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (fd != -1) {
                dup2(fd, STDOUT_FILENO);
                dup2(fd, STDERR_FILENO);
                close(fd);
            }
            
            // Detach from parent process
            setsid();
            
            // Prepare arguments for MPV
            // We need these to be in static storage so they don't get destroyed
            static std::string socket_arg;
            socket_arg = "--input-ipc-server=" + socket_path;
            
            // Execute mpv with loop option
            std::vector<const char*> args;
            args.push_back(mpv_path.c_str());
            args.push_back("--no-video");
            args.push_back("--loop=inf");
            args.push_back("--really-quiet");
            args.push_back("--keep-open=always");
            args.push_back("--idle=yes");
            args.push_back("--input-ipc-timeout=1000");
            args.push_back(socket_arg.c_str());
            args.push_back(alarm_item.url.c_str());
            args.push_back(nullptr);
            
            MPV_DEBUG_FMT("Alarm: Executing MPV with socket: {}", socket_arg);
            
            // Execute mpv
            execv(mpv_path.c_str(), const_cast<char* const*>(args.data()));
            
            // If we get here, execv failed
            perror("Alarm: execv failed");
            exit(1);
        }
        
        // Parent process
        if (pid > 0) {
            MPV_INFO_FMT("Alarm: Started MPV with PID: {}", pid);
            alarm_item.player_pid = pid;
            alarm_item.is_playing = true;
            
            // Give MPV a moment to initialize
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            
            // Try to initialize socket connection
            if (alarm_item.mpv_socket.init_socket(socket_path)) {
                alarm_item.startPositionTracking();
            } else {
                MPV_WARN("Alarm: Failed to initialize socket connection");
            }
        } else {
            alarm_item.player_pid = 0;
            alarm_item.is_playing = false;
        }
    }
    static void stop_sound_loop() {
        auto& alarm_item = alarm_item_instance();
        alarm_item.stopMedia();
    }
};