#pragma once

#include <string>
#include <chrono>
#include <filesystem>
#include <thread>
#include <cstring>

// Platform-specific socket headers
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    // Windows doesn't have Unix domain sockets in the same way
    // We'll need to use named pipes or TCP sockets as alternatives
#else
    #include <sys/socket.h>
    #include <sys/un.h>
    #include <unistd.h>
#endif

#include "debug.hpp"

// MPV component logging macros
#define MPV_ERROR(message) LOG_COMPONENT("MPV", LOG_LEVEL_ERROR, message)
#define MPV_WARN(message) LOG_COMPONENT("MPV", LOG_LEVEL_WARN, message)
#define MPV_INFO(message) LOG_COMPONENT("MPV", LOG_LEVEL_INFO, message)
#define MPV_DEBUG(message) LOG_COMPONENT("MPV", LOG_LEVEL_DEBUG, message)
#define MPV_TRACE(message) LOG_COMPONENT("MPV", LOG_LEVEL_TRACE, message)

// MPV component format macros
#define MPV_ERROR_FMT(fmt, ...) MPV_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define MPV_WARN_FMT(fmt, ...) MPV_WARN(debug::format_log(fmt, __VA_ARGS__))
#define MPV_INFO_FMT(fmt, ...) MPV_INFO(debug::format_log(fmt, __VA_ARGS__))
#define MPV_DEBUG_FMT(fmt, ...) MPV_DEBUG(debug::format_log(fmt, __VA_ARGS__))
#define MPV_TRACE_FMT(fmt, ...) MPV_TRACE(debug::format_log(fmt, __VA_ARGS__))

// Helper class to handle MPV player interaction via socket
class mpv_socket_helper {
private:
#ifdef _WIN32
    SOCKET socket_fd{INVALID_SOCKET};
#else
    int socket_fd{-1};
#endif
    std::string socket_path;
    bool using_fd_socket{false}; // Flag to indicate if we're using direct FD socket

public:
    mpv_socket_helper() = default;
    ~mpv_socket_helper() {
        close_socket();
    }

    void close_socket() {
#ifdef _WIN32
        if (socket_fd != INVALID_SOCKET) {
            closesocket(socket_fd);
            socket_fd = INVALID_SOCKET;
        }
#else
        if (socket_fd >= 0) {
            close(socket_fd);
            socket_fd = -1;
        }
#endif
        
        // Remove socket file if it exists and we're using path-based socket
        if (!using_fd_socket && !socket_path.empty() && std::filesystem::exists(socket_path)) {
            std::filesystem::remove(socket_path);
        }
    }
    
    // For direct file descriptor socket approach
    void set_socket_fd(int fd) {
        // Close any existing socket first
#ifdef _WIN32
        if (socket_fd != INVALID_SOCKET) {
            closesocket(socket_fd);
        }
        socket_fd = static_cast<SOCKET>(fd);
#else
        if (socket_fd >= 0) {
            close(socket_fd);
        }
        socket_fd = fd;
#endif
        
        using_fd_socket = true;
        socket_path.clear(); // Not using path-based socket
        
        MPV_INFO("Using direct file descriptor for MPV socket communication");
    }
    
    std::string create_socket_path() {
        // Create a unique socket path, preferring /tmp which has wider permissions
        // Unix domain socket paths are limited to 104-108 bytes depending on the system
        auto now = std::chrono::system_clock::now().time_since_epoch();
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        
        // First try with /tmp which typically has better permissions
        std::filesystem::path socket_dir = "/tmp";
        
        // Test if we can write to /tmp
        bool use_tmp = false;
        try {
            std::string test_path = (socket_dir / "rouen_socket_test").string();
            std::ofstream test_file(test_path);
            if (test_file) {
                test_file << "test";
                test_file.close();
                std::filesystem::remove(test_path);
                use_tmp = true;
                MPV_INFO("Using /tmp for socket (better compatibility)");
            }
        } catch (const std::exception& e) {
            MPV_WARN_FMT("Could not use /tmp for socket: {}", e.what());
        }
        
        // Fall back to user data directory if /tmp is not accessible
        if (!use_tmp) {
            socket_dir = rouen::platform::get_user_data_path("s", true);
            
            // Ensure the socket directory exists
            if (!std::filesystem::exists(socket_dir)) {
                std::filesystem::create_directories(socket_dir);
            }
            
            MPV_INFO_FMT("Using user data path for socket: {}", socket_dir.string());
        }
        
        // Generate a short name for the socket to minimize path length issues
        // Format: /tmp/r<timestamp> or user_data_path/s/r<timestamp>
        std::string name = "r" + std::to_string(millis);
        socket_path = (socket_dir / name).string();
        
        // Make sure the socket path is short enough
        if (socket_path.length() >= 100) { 
            // 100 is a safer limit - Unix socket paths are typically limited to 104-108 bytes
            MPV_WARN("Socket path too long, using shortened form");
            // Use a sequential number instead to make it shorter
            static std::atomic<int> counter{0};
            name = "s" + std::to_string(++counter);
            socket_path = (socket_dir / name).string();
        }
        
        // Log the length of the socket path for debugging
        MPV_INFO_FMT("Socket path: {} (length: {})", socket_path, socket_path.length());
        
        return socket_path;
    }
    
    bool init_socket(const std::string& path) {
        socket_path = path;
        
        // Check directory permissions before attempting to connect
        std::filesystem::path socket_dir = std::filesystem::path(socket_path).parent_path();
        try {
            // Check if directory exists
            if (!std::filesystem::exists(socket_dir)) {
                MPV_ERROR_FMT("Socket directory does not exist: {}", socket_dir.string());
                try {
                    // Try to create it
                    std::filesystem::create_directories(socket_dir);
                    MPV_INFO_FMT("Created socket directory: {}", socket_dir.string());
                } catch (const std::exception& e) {
                    MPV_ERROR_FMT("Failed to create socket directory: {} - {}", socket_dir.string(), e.what());
                    return false;
                }
            }
            
            // Test directory permissions with a temporary file
            std::string test_path = (socket_dir / "socket_test_perm").string();
            try {
                std::ofstream test_file(test_path);
                if (test_file) {
                    test_file << "test";
                    test_file.close();
                    std::filesystem::remove(test_path);
                    MPV_INFO("Socket directory permissions verified");
                } else {
                    MPV_ERROR_FMT("Cannot write to socket directory: {}", socket_dir.string());
                    return false;
                }
            } catch (const std::exception& e) {
                MPV_ERROR_FMT("Exception when testing socket directory permissions: {}", e.what());
                return false;
            }
        } catch (const std::exception& e) {
            MPV_ERROR_FMT("Exception checking socket directory: {}", e.what());
            return false;
        }
        
#ifdef _WIN32
        // Windows doesn't support Unix domain sockets natively
        // We'll use TCP sockets as an alternative for Windows
        MPV_WARN("Windows doesn't support Unix domain sockets - MPV socket communication disabled");
        return false;
#else
        // Create socket
        socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (socket_fd == -1) {
            MPV_ERROR_FMT("Socket creation failed: {}", strerror(errno));
            return false;
        }
        
        // Connect to the socket
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        
        // Check if the socket path is too long for sockaddr_un.sun_path
        if (socket_path.length() >= sizeof(addr.sun_path)) {
            MPV_ERROR_FMT("Socket path too long: {} (length: {}, max: {})", 
                         socket_path, socket_path.length(), sizeof(addr.sun_path) - 1);
            close(socket_fd);
            socket_fd = -1;
            return false;
        }
        
        strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
        
        // Wait for the socket to become available (mpv needs a moment to create it)
        MPV_INFO_FMT("Waiting for socket to become available at: {}", socket_path);
        // Use a slightly longer wait time to handle slower systems
        for (int i = 0; i < 70; i++) {  // Wait up to 7 seconds max
            if (std::filesystem::exists(socket_path)) {
                MPV_INFO("Socket file exists, proceeding to connect");
                
                // Add a small delay before attempting connection to avoid race conditions
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                break;
            }
            MPV_DEBUG_FMT("Socket file not ready yet, waiting... (attempt {})", i+1);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Check if socket exists
        if (!std::filesystem::exists(socket_path)) {
            MPV_ERROR("Socket file does not exist");
            close(socket_fd);
            socket_fd = -1;
            
            // Check if we can list the directory contents for troubleshooting
            try {
                std::filesystem::path parent_dir = std::filesystem::path(socket_path).parent_path();
                if (std::filesystem::exists(parent_dir)) {
                    MPV_DEBUG_FMT("Files in parent directory {}:", parent_dir.string());
                    for (const auto& entry : std::filesystem::directory_iterator(parent_dir)) {
                        MPV_DEBUG_FMT("  {}", entry.path().string());
                    }
                }
            } catch (const std::exception& e) {
                MPV_DEBUG_FMT("Error listing directory: {}", e.what());
            }
            
            return false;
        }
        
        // Check socket permissions before attempting to connect
        try {
            auto perms = std::filesystem::status(socket_path).permissions();
            MPV_DEBUG_FMT("Socket permissions: read={}, write={}", 
                        (perms & std::filesystem::perms::owner_read) != std::filesystem::perms::none,
                        (perms & std::filesystem::perms::owner_write) != std::filesystem::perms::none);
        } catch (const std::exception& e) {
            MPV_DEBUG_FMT("Could not check socket permissions: {}", e.what());
        }
        
        // Connect to socket
        if (connect(socket_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == -1) {
            MPV_ERROR_FMT("Connection failed: {}", strerror(errno));
            close(socket_fd);
            socket_fd = -1;
            return false;
        }
        
        MPV_INFO_FMT("Connected to MPV socket at {}", socket_path);
        
        // Test the connection to make sure it's actually working
        if (!test_connection()) {
            MPV_ERROR("Connection test failed - socket may not be properly initialized");
            close(socket_fd);
            socket_fd = -1;
            return false;
        }
        
        MPV_INFO("Connection test successful - socket is properly initialized");
        return true;
#endif // !_WIN32
    }
    
    bool send_command(const std::string& command) {
#ifdef _WIN32
        // Windows socket communication not supported yet
        (void)command; // Suppress unreferenced parameter warning
        MPV_WARN("MPV socket communication not available on Windows");
        return false;
#else
        // First check if the socket is valid
        if (socket_fd < 0) {
            MPV_WARN("Cannot send command - socket not connected");
            return false;
        }
        
        // Verify socket is still valid
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
            MPV_ERROR_FMT("Socket error check failed before send: {}", strerror(errno));
            close(socket_fd);
            socket_fd = -1;
            return false;
        }
        
        if (error != 0) {
            MPV_ERROR_FMT("Socket has pending error before send: {}", strerror(error));
            close(socket_fd);
            socket_fd = -1;
            return false;
        }
        
        // For retrying partial sends
        size_t total_sent = 0;
        size_t command_len = command.length();
        int retry_count = 0;
        const int max_retries = 3;
        
        while (total_sent < command_len && retry_count < max_retries) {
            // Send the command with MSG_NOSIGNAL to prevent SIGPIPE signals
            ssize_t bytes_sent = send(socket_fd, 
                                    command.c_str() + total_sent, 
                                    command_len - total_sent, 
                                    MSG_NOSIGNAL);
            
            if (bytes_sent > 0) {
                // Safely convert bytes_sent from ssize_t to size_t before adding
                total_sent += static_cast<size_t>(bytes_sent);
                if (total_sent == command_len) {
                    MPV_DEBUG_FMT("Command sent successfully: {}", command);
                    return true;
                }
                
                // Partial send - we'll continue in the next iteration
                MPV_DEBUG_FMT("Partial send: {} of {} bytes", total_sent, command_len);
            } else if (bytes_sent == 0) {
                // Socket was closed
                MPV_ERROR("Socket connection closed during send");
                close(socket_fd);
                socket_fd = -1;
                return false;
            } else {
                // Error occurred
                if (errno == EPIPE) {
                    // MPV process has likely terminated - this is normal behavior
                    MPV_DEBUG("Socket connection closed: MPV process terminated");
                    close(socket_fd);
                    socket_fd = -1;
                    return false;
                } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Non-blocking socket would block - not necessarily an error
                    MPV_DEBUG_FMT("Socket would block (retry {}), waiting briefly", retry_count + 1);
                    std::this_thread::sleep_for(std::chrono::milliseconds(50 * (retry_count + 1)));
                    retry_count++;
                    continue;
                } else {
                    // Other socket error
                    MPV_WARN_FMT("Socket send error: {}", strerror(errno));
                    close(socket_fd);
                    socket_fd = -1;
                    return false;
                }
            }
        }
        
        if (total_sent < command_len) {
            MPV_ERROR_FMT("Failed to send complete command after {} retries: {}/{} bytes", 
                         max_retries, total_sent, command_len);
            return false;
        }
        
        return true;
#endif // !_WIN32
    }
    
    bool receive_response(char* buffer, size_t buffer_size, int timeout_ms = 200) {
#ifdef _WIN32
        // Windows socket communication not supported yet
        (void)buffer; (void)buffer_size; (void)timeout_ms; // Suppress unreferenced parameter warnings
        MPV_WARN("MPV socket communication not available on Windows");
        return false;
#else
        if (socket_fd < 0) {
            MPV_DEBUG("Attempted to receive from closed socket");
            return false;
        }
        
        fd_set readfds;
        struct timeval tv;
        
        FD_ZERO(&readfds);
        FD_SET(socket_fd, &readfds);
        
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = static_cast<decltype(tv.tv_usec)>((timeout_ms % 1000) * 1000); // Convert remaining ms to microseconds
        
        // Clear the buffer first
        memset(buffer, 0, buffer_size);
        size_t total_bytes_read = 0;
        
        // Try multiple reads until we get a complete response or timeout
        int max_attempts = 3;
        for (int attempt = 0; attempt < max_attempts; attempt++) {
            int select_result = select(socket_fd + 1, &readfds, nullptr, nullptr, &tv);
            
            if (select_result > 0) {
                // Data is available to read
                ssize_t bytes_read = recv(socket_fd, buffer + total_bytes_read, buffer_size - total_bytes_read - 1, 0);
                
                if (bytes_read > 0) {
                    // Safely convert bytes_read from ssize_t to size_t
                    total_bytes_read += static_cast<size_t>(bytes_read);
                    buffer[total_bytes_read] = '\0'; // Ensure null termination
                    
                    MPV_TRACE_FMT("Received {} bytes from socket (attempt {}/{})", 
                                 bytes_read, attempt + 1, max_attempts);
                    
                    // Check if we have a complete JSON response
                    if (strstr(buffer, "}") != nullptr) {
                        MPV_TRACE("Found complete JSON response");
                        return true;
                    }
                    
                    // If buffer is almost full, break to avoid overflow
                    if (total_bytes_read >= buffer_size - 100) {
                        MPV_WARN("Buffer getting full, stopping read");
                        break;
                    }
                    
                    // Reset select timeout for next attempt but make it shorter
                    tv.tv_sec = 0;
                    tv.tv_usec = 100000; // 100ms for subsequent reads
                } else if (bytes_read == 0) {
                    // Socket closed
                    MPV_WARN("Socket connection closed by MPV");
                    close(socket_fd);
                    socket_fd = -1;
                    return false;
                } else if (bytes_read < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        // No more data available right now
                        MPV_TRACE("No more data available to read right now");
                        break;
                    } else {
                        // Socket error
                        MPV_ERROR_FMT("Socket receive error: {}", strerror(errno));
                        close(socket_fd);
                        socket_fd = -1;
                        return false;
                    }
                }
            } else if (select_result < 0) {
                MPV_ERROR_FMT("Socket select error: {}", strerror(errno));
                return false;
            } else {
                // Timeout occurred
                MPV_TRACE("Select timeout occurred");
                break;
            }
        }
        
        // Return true if we received any data at all
        return total_bytes_read > 0;
#endif // !_WIN32
    }
    
    bool is_connected() const {
#ifdef _WIN32
        return socket_fd != INVALID_SOCKET;
#else
        return socket_fd >= 0;
#endif
    }
    
    bool test_connection() {
#ifdef _WIN32
        // Windows socket communication not supported yet
        return false;
#else
        if (socket_fd < 0) {
            MPV_WARN("Socket test failed: invalid socket descriptor");
            return false;
        }
        
        // Verify socket is still valid
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
            MPV_ERROR_FMT("Socket error check failed: {}", strerror(errno));
            return false;
        }
        
        if (error != 0) {
            MPV_ERROR_FMT("Socket has pending error: {}", strerror(error));
            return false;
        }
        
        // Try to send a harmless command to test if the connection is really working
        std::string test_cmd = "{\"command\":[\"get_property\",\"pid\"],\"request_id\":99}\n";
        
        // Try multiple times in case of initial failures
        for (int attempt = 0; attempt < 3; attempt++) {
            if (!send_command(test_cmd)) {
                MPV_WARN_FMT("Socket test connection send failed (attempt {}/3)", attempt+1);
                
                if (attempt < 2) {
                    // Give it a moment before retrying, increasing delay each time
                    std::this_thread::sleep_for(std::chrono::milliseconds(200 * (attempt + 1)));
                    continue;
                }
                return false;
            }
            
            // Wait a bit for the response
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            
            // Try to receive a response
            char buffer[4096];
            memset(buffer, 0, sizeof(buffer));
            
            // Increase timeout for first connection test
            int timeout = (attempt == 0) ? 500 : 300;
            
            if (!receive_response(buffer, sizeof(buffer), timeout)) {
                MPV_WARN_FMT("Socket test connection receive failed (attempt {}/3)", attempt+1);
                
                if (attempt < 2) {
                    // Give it a moment before retrying, increasing delay each time
                    std::this_thread::sleep_for(std::chrono::milliseconds(200 * (attempt + 1)));
                    continue;
                }
                return false;
            }
            
            // Check if response contains PID or any valid JSON
            if (strstr(buffer, "request_id") != nullptr) {
                MPV_INFO("Socket test connection successful");
                return true;
            } else {
                MPV_WARN("Received response but missing expected content");
                if (attempt < 2) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    continue;
                }
            }
        }
        
        MPV_ERROR("Socket test failed after multiple attempts");
        return false;
#endif // !_WIN32
    }
    
    int get_socket_fd() const {
#ifdef _WIN32
        return static_cast<int>(socket_fd);
#else
        return socket_fd;
#endif
    }
    
    const std::string& get_socket_path() const {
        return socket_path;
    }
    
    // Check if the socket file exists
    bool socket_exists() const {
        if (socket_path.empty()) {
            MPV_WARN("Cannot check if socket exists: socket path is empty");
            return false;
        }
        
        bool exists = std::filesystem::exists(socket_path);
        if (!exists) {
            MPV_DEBUG_FMT("Socket does not exist at path: {}", socket_path);
            
            // Check the parent directory to see if it's accessible
            std::filesystem::path parent_dir = std::filesystem::path(socket_path).parent_path();
            try {
                if (std::filesystem::exists(parent_dir)) {
                    MPV_DEBUG_FMT("Parent directory {} exists", parent_dir.string());
                    
                    // Check if parent directory is actually readable
                    std::error_code ec;
                    auto dir_iter = std::filesystem::directory_iterator(parent_dir, ec);
                    if (ec) {
                        MPV_DEBUG_FMT("Cannot read parent directory: {}", ec.message());
                    } else {
                        // Count files in directory for debugging
                        int count = 0;
                        for (const auto& entry : dir_iter) {
                            count++;
                            if (entry.path().filename().string().find('m') == 0) {
                                MPV_DEBUG_FMT("Found potential socket file: {}", entry.path().string());
                            }
                        }
                        MPV_DEBUG_FMT("Parent directory has {} entries", count);
                    }
                } else {
                    MPV_DEBUG_FMT("Parent directory {} does not exist", parent_dir.string());
                }
            } catch (const std::exception& e) {
                MPV_DEBUG_FMT("Error checking parent directory: {}", e.what());
            }
        } else {
            MPV_DEBUG_FMT("Socket exists at path: {}", socket_path);
        }
        
        return exists;
    }
};
