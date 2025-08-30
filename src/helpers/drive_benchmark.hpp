#pragma once

#include <iostream>
#include <fstream>
#include <chrono>
#include <vector>
#include <string>
#include <filesystem>
#include <iomanip>
#include <random>
#include <future>
#include <thread>

namespace rouen::helpers {

class DriveBenchmark {
private:
    static constexpr size_t BUFFER_SIZE = 1024 * 1024; // 1MB buffer
    static constexpr size_t TEST_SIZE = 50 * 1024 * 1024; // 50MB test file (reduced for UI responsiveness)
    
    std::vector<char> buffer;
    std::mt19937 rng;
    
public:
    DriveBenchmark() : buffer(BUFFER_SIZE), rng(std::random_device{}()) {
        // Fill buffer with random data
        std::uniform_int_distribution<unsigned int> dist(0, 255);
        for (auto& byte : buffer) {
            byte = static_cast<char>(dist(rng));
        }
    }
    
    struct BenchmarkResult {
        double write_speed_mbps;
        double read_speed_mbps;
        double sequential_read_mbps;
        std::string path;
        std::string display_name;
        bool success;
        std::string error_message;
        
        std::string get_drive_type() const {
            if (!success) return "Error";
            
            double avg_speed = (write_speed_mbps + read_speed_mbps) / 2.0;
            if (avg_speed > 500) {
                return "NVMe SSD";
            } else if (avg_speed > 200) {
                return "SATA SSD";
            } else if (avg_speed > 50) {
                return "Fast HDD";
            } else {
                return "Slow Drive";
            }
        }
    };
    
    BenchmarkResult benchmark_path(const std::string& path, const std::string& display_name = "") {
        BenchmarkResult result;
        result.path = path;
        result.display_name = display_name.empty() ? path : display_name;
        result.success = false;
        
        try {
            // Ensure directory exists
            std::filesystem::create_directories(path);
            
            std::string test_file = path + "/rouen_benchmark_test.dat";
            
            // Write test
            auto write_speed = benchmark_write(test_file);
            if (write_speed < 0) {
                result.error_message = "Write test failed";
                return result;
            }
            result.write_speed_mbps = write_speed;
            
            // Read test
            auto read_speed = benchmark_read(test_file);
            if (read_speed < 0) {
                result.error_message = "Read test failed";
                return result;
            }
            result.read_speed_mbps = read_speed;
            
            // Sequential read test
            auto seq_read_speed = benchmark_sequential_read(test_file);
            if (seq_read_speed < 0) {
                result.error_message = "Sequential read test failed";
                return result;
            }
            result.sequential_read_mbps = seq_read_speed;
            
            // Clean up
            std::filesystem::remove(test_file);
            
            result.success = true;
            
        } catch (const std::exception& e) {
            result.error_message = e.what();
        }
        
        return result;
    }
    
    std::vector<std::pair<std::string, std::string>> get_common_drive_paths() {
        std::vector<std::pair<std::string, std::string>> paths; // path, display_name
        
        // Internal drive (home directory)
        std::string home = std::getenv("HOME") ? std::getenv("HOME") : "/tmp";
        paths.emplace_back(home + "/rouen_benchmark", "Internal Drive (Home)");
        
#ifdef __APPLE__
        // macOS external drive mount points
        try {
            std::string volumes_path = "/Volumes";
            if (std::filesystem::exists(volumes_path) && 
                std::filesystem::is_directory(volumes_path)) {
                
                for (const auto& entry : std::filesystem::directory_iterator(volumes_path)) {
                    if (entry.is_directory()) {
                        std::string volume_name = entry.path().filename().string();
                        if (volume_name != "Macintosh HD" && volume_name != ".localized") {
                            paths.emplace_back(
                                entry.path().string() + "/rouen_benchmark",
                                "External: " + volume_name
                            );
                        }
                    }
                }
            }
        } catch (...) {
            // If we can't list volumes, just continue
        }
#elif defined(_WIN32)
        // Windows drive letters
        for (char drive = 'C'; drive <= 'Z'; ++drive) {
            std::string drive_path = std::string(1, drive) + ":\\";
            if (std::filesystem::exists(drive_path)) {
                std::string display_name = drive == 'C' ? "Internal Drive (C:)" : 
                                         std::string("Drive ") + drive + ":";
                paths.emplace_back(drive_path + "rouen_benchmark", display_name);
            }
        }
#else
        // Linux mount points
        paths.emplace_back("/tmp/rouen_benchmark", "System Temp");
        
        // Check common mount points
        std::vector<std::string> mount_candidates = {
            "/mnt", "/media", "/run/media"
        };
        
        for (const auto& mount_base : mount_candidates) {
            try {
                if (std::filesystem::exists(mount_base) && 
                    std::filesystem::is_directory(mount_base)) {
                    
                    for (const auto& entry : std::filesystem::directory_iterator(mount_base)) {
                        if (entry.is_directory()) {
                            paths.emplace_back(
                                entry.path().string() + "/rouen_benchmark",
                                "External: " + entry.path().filename().string()
                            );
                        }
                    }
                }
            } catch (...) {
                // Continue if we can't access mount points
            }
        }
#endif
        
        return paths;
    }
    
private:
    double benchmark_write(const std::string& filename) {
        try {
            auto start = std::chrono::high_resolution_clock::now();
            
            std::ofstream file(filename, std::ios::binary);
            if (!file) return -1;
            
            size_t total_written = 0;
            while (total_written < TEST_SIZE) {
                size_t to_write = std::min(BUFFER_SIZE, TEST_SIZE - total_written);
                file.write(buffer.data(), static_cast<std::streamsize>(to_write));
                if (!file) return -1;
                total_written += to_write;
            }
            
            file.close();
            
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            
            double seconds = static_cast<double>(duration.count()) / 1000000.0;
            double mb_written = static_cast<double>(TEST_SIZE) / (1024.0 * 1024.0);
            
            return mb_written / seconds;
            
        } catch (...) {
            return -1;
        }
    }
    
    double benchmark_read(const std::string& filename) {
        try {
            auto start = std::chrono::high_resolution_clock::now();
            
            std::ifstream file(filename, std::ios::binary);
            if (!file) return -1;
            
            std::vector<char> read_buffer(BUFFER_SIZE);
            size_t total_read = 0;
            
            while (total_read < TEST_SIZE && file) {
                size_t to_read = std::min(BUFFER_SIZE, TEST_SIZE - total_read);
                file.read(read_buffer.data(), static_cast<std::streamsize>(to_read));
                total_read += static_cast<size_t>(file.gcount());
            }
            
            file.close();
            
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            
            double seconds = static_cast<double>(duration.count()) / 1000000.0;
            double mb_read = static_cast<double>(total_read) / (1024.0 * 1024.0);
            
            return mb_read / seconds;
            
        } catch (...) {
            return -1;
        }
    }
    
    double benchmark_sequential_read(const std::string& filename) {
        try {
            auto start = std::chrono::high_resolution_clock::now();
            
            std::ifstream file(filename, std::ios::binary);
            if (!file) return -1;
            
            // Read in larger chunks for sequential access
            std::vector<char> read_buffer(4 * BUFFER_SIZE);
            size_t total_read = 0;
            
            while (file) {
                file.read(read_buffer.data(), static_cast<std::streamsize>(read_buffer.size()));
                total_read += static_cast<size_t>(file.gcount());
                if (file.gcount() == 0) break;
            }
            
            file.close();
            
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            
            double seconds = static_cast<double>(duration.count()) / 1000000.0;
            double mb_read = static_cast<double>(total_read) / (1024.0 * 1024.0);
            
            return mb_read / seconds;
            
        } catch (...) {
            return -1;
        }
    }
};

} // namespace rouen::helpers
