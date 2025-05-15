#pragma once

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <format>
#include <future>
#include <algorithm>
#include <queue>
#include <condition_variable>
#include <functional>
#include <random>       // For std::random_device, std::mt19937, and std::shuffle
#include <unordered_map> // For std::unordered_map

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>

#include "../../helpers/imgui_include.hpp"
#include "../interface/card.hpp"
#include "../../helpers/debug.hpp"

// Define subnet scanner specific logging macros
#define NET_ERROR(message) LOG_COMPONENT("SUBNET", LOG_LEVEL_ERROR, message)
#define NET_WARN(message) LOG_COMPONENT("SUBNET", LOG_LEVEL_WARN, message)
#define NET_INFO(message) LOG_COMPONENT("SUBNET", LOG_LEVEL_INFO, message)
#define NET_DEBUG(message) LOG_COMPONENT("SUBNET", LOG_LEVEL_DEBUG, message)
#define NET_TRACE(message) LOG_COMPONENT("SUBNET", LOG_LEVEL_TRACE, message)

// Format-enabled macros
#define NET_ERROR_FMT(fmt, ...) NET_ERROR(debug::format_log(fmt, __VA_ARGS__))
#define NET_WARN_FMT(fmt, ...) NET_WARN(debug::format_log(fmt, __VA_ARGS__))
#define NET_INFO_FMT(fmt, ...) NET_INFO(debug::format_log(fmt, __VA_ARGS__))
#define NET_DEBUG_FMT(fmt, ...) NET_DEBUG(debug::format_log(fmt, __VA_ARGS__))
#define NET_TRACE_FMT(fmt, ...) NET_TRACE(debug::format_log(fmt, __VA_ARGS__))

namespace rouen::cards {

struct device_info {
    std::string ip_address;
    std::string hostname;
    std::string mac_address;
    bool is_online{true};
    std::chrono::system_clock::time_point last_seen{std::chrono::system_clock::now()};
};

// ThreadPool class for managing worker threads
class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads) : stop(false) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex);
                        condition.wait(lock, [this] { 
                            return stop || !tasks.empty(); 
                        });
                        
                        if (stop && tasks.empty()) {
                            return;
                        }
                        
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    
                    task();
                }
            });
        }
        
        NET_INFO_FMT("Created thread pool with {} worker threads", num_threads);
    }
    
    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        
        condition.notify_all();
        
        for (std::thread &worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        
        NET_INFO("Thread pool destroyed");
    }
    
    template<class F>
    void enqueue(F&& f) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if (stop) {
                throw std::runtime_error("Cannot enqueue on stopped ThreadPool");
            }
            
            tasks.emplace(std::forward<F>(f));
        }
        
        condition.notify_one();
    }
    
    size_t get_thread_count() const {
        return workers.size();
    }
    
    size_t get_queue_size() {
        std::unique_lock<std::mutex> lock(queue_mutex);
        return tasks.size();
    }
    
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

class subnet_scanner : public card {
public:
    subnet_scanner() 
        : thread_pool(std::thread::hardware_concurrency() > 0 ? 
                     std::thread::hardware_concurrency() : 4) {
        // Set custom colors for the card
        colors[0] = {0.2f, 0.4f, 0.6f, 1.0f};   // Blue primary color
        colors[1] = {0.3f, 0.5f, 0.7f, 0.7f};   // Light blue secondary color
        colors[2] = {0.1f, 0.6f, 0.9f, 1.0f};   // Scan button color
        colors[3] = {0.0f, 0.8f, 0.2f, 1.0f};   // Online device color
        colors[4] = {0.8f, 0.2f, 0.2f, 1.0f};   // Offline device color
        colors[5] = {0.5f, 0.5f, 0.1f, 1.0f};   // Thread activity color
        
        name("Subnet Scanner");
        width = 400.0f;  // Make the card a bit wider
        requested_fps = 10; // Higher refresh rate to update progress more smoothly
        
        // Detect local interfaces and subnets
        detect_local_interfaces();
        
        // Log the number of threads created
        NET_INFO_FMT("Subnet scanner initialized with {} threads", thread_pool.get_thread_count());
    }
    
    ~subnet_scanner() override {
        stop_scan();
    }
    
    bool render() override {
        return render_window([this]() {
            // Show scanning status if active
            if (is_scanning) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Scanning in progress...");
                // Use load() to get current values from atomic variables
                int scanned = scanned_hosts.load();
                int total = total_hosts.load();
                float progress = total > 0 ? static_cast<float>(scanned) / static_cast<float>(total) : 0.0f;
                
                // Format using regular integers, not atomic
                std::string progress_text = std::format("{}/{}", scanned, total);
                ImGui::ProgressBar(progress, ImVec2(-1, 0), progress_text.c_str());
                
                // Show thread pool stats
                ImGui::TextColored(colors[5], "Active threads: %zu, Queued tasks: %zu", 
                                 thread_pool.get_thread_count(), thread_pool.get_queue_size());
                
                if (ImGui::Button("Stop Scan", ImVec2(120, 0))) {
                    stop_scan();
                }
            } else {
                ImGui::Text("Select Network Interface:");
                
                if (interfaces.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "No network interfaces detected");
                } else {
                    if (ImGui::BeginCombo("##interfaces", current_interface.c_str())) {
                        for (const auto& interface : interfaces) {
                            bool is_selected = (current_interface == interface.first);
                            if (ImGui::Selectable(interface.first.c_str(), is_selected)) {
                                current_interface = interface.first;
                                selected_subnet = interface.second;
                            }
                            if (is_selected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    
                    ImGui::SameLine();
                    if (ImGui::Button("Refresh", ImVec2(80, 0))) {
                        detect_local_interfaces();
                    }
                    
                    ImGui::Separator();
                    
                    ImGui::Text("Subnet: %s", selected_subnet.c_str());
                    
                    ImGui::Separator();
                    
                    if (ImGui::RadioButton("Fast Ping Scan", scan_type == 0)) scan_type = 0;
                    ImGui::SameLine();
                    if (ImGui::RadioButton("Deep Port Scan", scan_type == 1)) scan_type = 1;
                    
                    ImGui::Separator();
                    
                    // Timeout slider (in milliseconds)
                    ImGui::Text("Timeout (ms):");
                    ImGui::SliderInt("##timeout", &ping_timeout_ms, 100, 3000);
                    
                    // Number of worker threads slider
                    size_t hw_threads = std::thread::hardware_concurrency();
                    ImGui::Text("Threads to use (%zu available):", hw_threads);
                    ImGui::SliderInt("##threads", &num_threads, 1, static_cast<int>(hw_threads * 2));
                    
                    ImGui::Separator();
                    
                    ImGui::PushStyleColor(ImGuiCol_Button, colors[2]);
                    if (ImGui::Button("Start Scan", ImVec2(120, 0)) && !selected_subnet.empty()) {
                        start_scan();
                    }
                    ImGui::PopStyleColor();
                    
                    if (!devices.empty()) {
                        ImGui::SameLine();
                        if (ImGui::Button("Clear Results", ImVec2(120, 0))) {
                            devices.clear();
                        }
                    }
                }
            }
            
            ImGui::Separator();
            
            if (!devices.empty()) {
                ImGui::Text("Discovered Devices: %zu", devices.size());
                
                // Create columns for the results table
                if (ImGui::BeginTable("devices_table", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("IP Address", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                    ImGui::TableSetupColumn("Hostname", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                    ImGui::TableHeadersRow();
                    
                    // Use a mutex to safely access the devices list
                    std::lock_guard<std::mutex> lock(devices_mutex);
                    
                    for (const auto& device : devices) {
                        ImGui::TableNextRow();
                        
                        // IP Address
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", device.ip_address.c_str());
                        
                        // Hostname
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", device.hostname.empty() ? "Unknown" : device.hostname.c_str());
                        
                        // Status
                        ImGui::TableNextColumn();
                        if (device.is_online) {
                            ImGui::TextColored(colors[3], "Online");
                        } else {
                            ImGui::TextColored(colors[4], "Offline");
                        }
                    }
                    
                    ImGui::EndTable();
                }
            } else if (!is_scanning) {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No devices discovered yet");
            }
        });
    }
    
    std::string get_uri() const override {
        return "subnet-scanner";
    }
    
private:
    // Network interfaces and subnets
    std::unordered_map<std::string, std::string> interfaces;
    std::string current_interface;
    std::string selected_subnet;
    
    // Scan settings
    int scan_type = 0;  // 0 = ping scan, 1 = port scan
    int ping_timeout_ms = 500;
    int num_threads = static_cast<int>(std::thread::hardware_concurrency());
    
    // Thread pool for parallel scanning
    ThreadPool thread_pool;
    
    // Scan status
    std::atomic<bool> is_scanning{false};
    std::atomic<int> scanned_hosts{0};
    std::atomic<int> total_hosts{0};
    std::thread coordinator_thread;
    
    // Results
    std::vector<device_info> devices;
    std::mutex devices_mutex;
    
    // Detect available network interfaces and their subnets
    void detect_local_interfaces() {
        interfaces.clear();
        current_interface.clear();
        selected_subnet.clear();
        
        struct ifaddrs *ifaddr, *ifa;
        
        if (getifaddrs(&ifaddr) == -1) {
            NET_ERROR("Failed to get interface addresses");
            return;
        }
        
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL)
                continue;
            
            // Only consider IPv4 interfaces
            if (ifa->ifa_addr->sa_family == AF_INET) {
                struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
                struct sockaddr_in *netmask = (struct sockaddr_in *)ifa->ifa_netmask;
                
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &(addr->sin_addr), ip, INET_ADDRSTRLEN);
                
                // Skip loopback interfaces
                if (strcmp(ip, "127.0.0.1") == 0)
                    continue;
                
                // Calculate subnet
                unsigned int ip_int = ntohl(addr->sin_addr.s_addr);
                unsigned int mask_int = ntohl(netmask->sin_addr.s_addr);
                unsigned int network_int = ip_int & mask_int;
                
                struct in_addr network_addr;
                network_addr.s_addr = htonl(network_int);
                
                char network[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &network_addr, network, INET_ADDRSTRLEN);
                
                // Count netmask bits
                int cidr = 0;
                for (unsigned int temp = mask_int; temp; temp >>= 1) {
                    cidr += temp & 1;
                }
                
                std::string subnet = std::format("{}/{}", network, cidr);
                
                // Add interface to our list
                std::string interface_name = ifa->ifa_name;
                std::string display_name = std::format("{} ({})", interface_name, ip);
                
                interfaces[display_name] = subnet;
                
                // Select first non-loopback interface by default
                if (current_interface.empty()) {
                    current_interface = display_name;
                    selected_subnet = subnet;
                }
            }
        }
        
        freeifaddrs(ifaddr);
    }
    
    // Start a network scan
    void start_scan() {
        if (is_scanning || selected_subnet.empty()) {
            return;
        }
        
        is_scanning = true;
        scanned_hosts = 0;
        
        // Parse the subnet (e.g., 192.168.1.0/24)
        size_t slash_pos = selected_subnet.find('/');
        if (slash_pos == std::string::npos) {
            is_scanning = false;
            NET_ERROR_FMT("Invalid subnet format: {}", selected_subnet);
            return;
        }
        
        std::string network_str = selected_subnet.substr(0, slash_pos);
        int cidr = std::stoi(selected_subnet.substr(slash_pos + 1));
        
        // Calculate the number of hosts in this subnet
        unsigned int host_bits = static_cast<unsigned int>(32 - cidr);
        total_hosts = (1 << host_bits) - 2;  // Exclude network and broadcast addresses
        
        // Convert network to integer
        struct in_addr network_addr;
        inet_pton(AF_INET, network_str.c_str(), &network_addr);
        unsigned int network_int = ntohl(network_addr.s_addr);
        
        // Start the scan coordinator thread
        coordinator_thread = std::thread([this, network_int, host_bits]() {
            NET_INFO_FMT("Starting subnet scan on {} using {} threads", 
                        selected_subnet, num_threads);
            
            unsigned int network_size = 1 << host_bits;
            
            // Create a synchronized queue of IP addresses to scan
            std::vector<std::string> ip_queue;
            ip_queue.reserve(network_size - 2);
            
            // Generate all IP addresses in the subnet
            for (unsigned int i = 1; i < network_size - 1; i++) {
                unsigned int host_ip = network_int + i;
                
                struct in_addr ip_addr;
                ip_addr.s_addr = htonl(host_ip);
                
                char ip_str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &ip_addr, ip_str, INET_ADDRSTRLEN);
                
                ip_queue.push_back(ip_str);
            }
            
            // Shuffle the IP addresses to distribute the load more evenly
            std::random_device rd;
            std::mt19937 g(rd());
            std::shuffle(ip_queue.begin(), ip_queue.end(), g);
            
            std::atomic<size_t> completed_tasks{0};
            std::atomic<size_t> total_tasks{ip_queue.size()};
            
            // Submit scanning tasks to the thread pool
            for (const auto& ip : ip_queue) {
                if (!is_scanning) {
                    break;  // Stop if scanning was cancelled
                }
                
                thread_pool.enqueue([this, ip, &completed_tasks]() {
                    // Check if the host is online
                    bool is_online = ping_host_with_ports(ip.c_str());
                    
                    if (is_online || scan_type == 1) {  // Always add if deep scan or if online
                        device_info device;
                        device.ip_address = ip;
                        device.is_online = is_online;
                        
                        // Try to resolve hostname
                        resolve_hostname(ip.c_str(), device.hostname);
                        
                        // Add the device to our list
                        {
                            std::lock_guard<std::mutex> lock(devices_mutex);
                            
                            // Check if the device is already in our list
                            auto it = std::find_if(devices.begin(), devices.end(),
                                [&](const auto& d) { return d.ip_address == device.ip_address; });
                            
                            if (it != devices.end()) {
                                // Update existing device
                                it->hostname = device.hostname;
                                it->is_online = device.is_online;
                                it->last_seen = std::chrono::system_clock::now();
                            } else {
                                // Add new device
                                devices.push_back(device);
                            }
                        }
                    }
                    
                    scanned_hosts++;
                    completed_tasks++;
                });
            }
            
            // Wait for all tasks to complete or for scanning to be cancelled
            while (completed_tasks < total_tasks && is_scanning) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            // Sort the devices by IP address (numeric order)
            if (!devices.empty()) {
                std::lock_guard<std::mutex> lock(devices_mutex);
                std::sort(devices.begin(), devices.end(), [](const auto& a, const auto& b) {
                    return inet_addr(a.ip_address.c_str()) < inet_addr(b.ip_address.c_str());
                });
            }
            
            is_scanning = false;
            NET_INFO("Subnet scan completed");
        });
        
        coordinator_thread.detach();
    }
    
    // Stop an ongoing scan
    void stop_scan() {
        if (is_scanning) {
            is_scanning = false;
            
            // Wait for the coordinator thread to finish
            if (coordinator_thread.joinable()) {
                coordinator_thread.join();
            }
            
            NET_INFO("Subnet scan stopped by user");
        }
    }
    
    // Try connecting to a specific port with timeout
    bool try_port(const char* ip_str, int port) {
        // Create the socket
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            return false;
        }
        
        // Set the socket to non-blocking mode
        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
        
        // Prepare the address
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip_str, &addr.sin_addr);
        
        // Attempt to connect (non-blocking)
        int result = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
        
        // Handle the non-blocking connect
        if (result < 0) {
            if (errno == EINPROGRESS) {
                // Select with timeout to wait for connection
                fd_set write_fds;
                FD_ZERO(&write_fds);
                FD_SET(sock, &write_fds);
                
                // Prepare timeout
                struct timeval timeout;
                timeout.tv_sec = ping_timeout_ms / 1000;
                timeout.tv_usec = (ping_timeout_ms % 1000) * 1000;
                
                // Wait for the socket to become writable (connection complete) or timeout
                result = select(sock + 1, nullptr, &write_fds, nullptr, &timeout);
                
                if (result > 0) {
                    // Socket became writable, but we need to check if connection succeeded
                    int error = 0;
                    socklen_t len = sizeof(error);
                    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error) {
                        // Connection failed
                        close(sock);
                        return false;
                    }
                    // Connection succeeded
                    close(sock);
                    return true;
                } else {
                    // Timeout or error
                    close(sock);
                    return false;
                }
            } else {
                // Immediate connection failure
                close(sock);
                return false;
            }
        } else {
            // Immediate connection success (rare but possible)
            close(sock);
            return true;
        }
    }
    
    bool ping_host_with_ports(const char* ip_str) {
        // First try HTTP port (80)
        if (try_port(ip_str, 80)) {
            return true;
        }
        
        // If the deep scan is enabled, try additional common ports
        if (scan_type == 1) {
            // Common ports to try: SSH (22), HTTPS (443), SMB (445)
            const int common_ports[] = {22, 443, 445, 3389, 8080, 8443};
            
            for (int port : common_ports) {
                if (try_port(ip_str, port)) {
                    return true;
                }
            }
        }
        
        // As a fallback, try ICMP ping using the system's ping command
        // Use a short timeout for the ping command as well
        std::string cmd = std::format("ping -c 1 -W 1 {} > /dev/null 2>&1", ip_str);
        int ping_result = system(cmd.c_str());
        
        return (ping_result == 0);
    }
    
    // Resolve hostname from IP
    void resolve_hostname(const char* ip_str, std::string& hostname) {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = 0;
        inet_pton(AF_INET, ip_str, &addr.sin_addr);
        
        char host[NI_MAXHOST];
        int result = getnameinfo((struct sockaddr*)&addr, sizeof(addr),
                                host, sizeof(host), NULL, 0, NI_NAMEREQD);
        
        if (result == 0) {
            hostname = host;
        } else {
            hostname = "";  // No hostname found
        }
    }
};

} // namespace rouen::cards
