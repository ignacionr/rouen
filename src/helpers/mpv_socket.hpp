#pragma once

#include <string>
#include <chrono>
#include <filesystem>
#include <thread>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
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
    int socket_fd{-1};
    std::string socket_path;

public:
    mpv_socket_helper() = default;
    ~mpv_socket_helper() {
        close_socket();
    }

    void close_socket() {
        if (socket_fd >= 0) {
            close(socket_fd);
            socket_fd = -1;
        }
        
        // Remove socket file if it exists
        if (!socket_path.empty() && std::filesystem::exists(socket_path)) {
            std::filesystem::remove(socket_path);
        }
    }
    
    std::string create_socket_path() {
        // Create a unique socket path in /tmp
        auto now = std::chrono::system_clock::now().time_since_epoch();
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        socket_path = "/tmp/mpv-socket-" + std::to_string(millis);
        return socket_path;
    }
    
    bool init_socket(const std::string& path) {
        socket_path = path;
        
        // Create socket
        socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (socket_fd == -1) {
            MPV_ERROR_FMT("Socket creation failed: {}", std::strerror(errno));
            return false;
        }
        
        // Connect to the socket
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
        
        // Wait for the socket to become available (mpv needs a moment to create it)
        for (int i = 0; i < 50; i++) {  // Wait up to 5 seconds max
            if (std::filesystem::exists(socket_path)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Check if socket exists
        if (!std::filesystem::exists(socket_path)) {
            MPV_ERROR("Socket file does not exist");
            close(socket_fd);
            socket_fd = -1;
            return false;
        }
        
        // Connect to socket
        if (connect(socket_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
            MPV_ERROR_FMT("Connection failed: {}", std::strerror(errno));
            close(socket_fd);
            socket_fd = -1;
            return false;
        }
        
        MPV_INFO_FMT("Successfully connected to MPV socket at {}", socket_path);
        return true;
    }
    
    bool send_command(const std::string& command) {
        // First check if the socket is valid
        if (socket_fd < 0) return false;
        
        // Send the command with MSG_NOSIGNAL to prevent SIGPIPE signals
        ssize_t bytes_sent = send(socket_fd, command.c_str(), command.length(), MSG_NOSIGNAL);
        if (bytes_sent != static_cast<ssize_t>(command.length())) {
            // Handle send error
            if (bytes_sent < 0) {
                // Check specifically for broken pipe error
                if (errno == EPIPE) {
                    // MPV process has likely terminated
                    MPV_ERROR("Socket send error: broken pipe (MPV process likely terminated)");
                } else {
                    MPV_ERROR_FMT("Socket send error: {}", std::strerror(errno));
                }
            } else {
                MPV_WARN_FMT("Partial data sent: {} of {} bytes", bytes_sent, command.length());
            }
            // Close the socket on error
            close(socket_fd);
            socket_fd = -1;
            return false;
        }
        
        MPV_DEBUG_FMT("Command sent: {}", command);
        return true;
    }
    
    bool receive_response(char* buffer, size_t buffer_size, int timeout_ms = 200) {
        if (socket_fd < 0) return false;
        
        fd_set readfds;
        struct timeval tv;
        
        FD_ZERO(&readfds);
        FD_SET(socket_fd, &readfds);
        
        tv.tv_sec = 0;
        tv.tv_usec = timeout_ms * 1000; // Convert to microseconds
        
        int select_result = select(socket_fd + 1, &readfds, NULL, NULL, &tv);
        if (select_result > 0) {
            ssize_t bytes_read = recv(socket_fd, buffer, buffer_size - 1, 0);
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0';
                MPV_TRACE_FMT("Received {} bytes from socket", bytes_read);
                return true;
            } else if (bytes_read == 0) {
                // Socket closed
                MPV_WARN("Socket connection closed by MPV");
                close(socket_fd);
                socket_fd = -1;
                return false;
            } else if (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                // Socket error
                MPV_ERROR_FMT("Socket receive error: {}", std::strerror(errno));
                close(socket_fd);
                socket_fd = -1;
                return false;
            }
        } else if (select_result < 0) {
            MPV_ERROR_FMT("Socket select error: {}", std::strerror(errno));
            return false;
        }
        // Timeout occurred
        return false;
    }
    
    bool is_connected() const {
        return socket_fd >= 0;
    }
    
    int get_socket_fd() const {
        return socket_fd;
    }
    
    const std::string& get_socket_path() const {
        return socket_path;
    }
};