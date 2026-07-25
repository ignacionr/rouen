#include <iostream>
#include <fstream>
#include <chrono>
#include <vector>
#include <string>
#include <filesystem>
#include <iomanip>
#include <random>

class DriveBenchmark {
private:
    static constexpr size_t BUFFER_SIZE = 1024 * 1024; // 1MB buffer
    static constexpr size_t TEST_SIZE = 100 * 1024 * 1024; // 100MB test file
    
    std::vector<char> buffer;
    std::mt19937 rng;
    
public:
    DriveBenchmark() : buffer(BUFFER_SIZE), rng(std::random_device{}()) {
        // Fill buffer with random data
        std::uniform_int_distribution<unsigned char> dist(0, 255);
        for (auto& byte : buffer) {
            byte = static_cast<char>(dist(rng));
        }
    }
    
    struct BenchmarkResult {
        double write_speed_mbps;
        double read_speed_mbps;
        double sequential_read_mbps;
        std::string path;
        bool success;
        std::string error_message;
    };
    
    BenchmarkResult benchmark_path(const std::string& path) {
        BenchmarkResult result;
        result.path = path;
        result.success = false;
        
        try {
            // Ensure directory exists
            std::filesystem::create_directories(path);
            
            std::string test_file = path + "/benchmark_test.dat";
            
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
    
private:
    double benchmark_write(const std::string& filename) {
        try {
            auto start = std::chrono::high_resolution_clock::now();
            
            std::ofstream file(filename, std::ios::binary);
            if (!file) return -1;
            
            size_t total_written = 0;
            while (total_written < TEST_SIZE) {
                size_t to_write = std::min(BUFFER_SIZE, TEST_SIZE - total_written);
                file.write(buffer.data(), to_write);
                if (!file) return -1;
                total_written += to_write;
            }
            
            file.close();
            
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            
            double seconds = duration.count() / 1000000.0;
            double mb_written = TEST_SIZE / (1024.0 * 1024.0);
            
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
                file.read(read_buffer.data(), to_read);
                total_read += file.gcount();
            }
            
            file.close();
            
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            
            double seconds = duration.count() / 1000000.0;
            double mb_read = total_read / (1024.0 * 1024.0);
            
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
                file.read(read_buffer.data(), read_buffer.size());
                total_read += file.gcount();
                if (file.gcount() == 0) break;
            }
            
            file.close();
            
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            
            double seconds = duration.count() / 1000000.0;
            double mb_read = total_read / (1024.0 * 1024.0);
            
            return mb_read / seconds;
            
        } catch (...) {
            return -1;
        }
    }
};

std::vector<std::string> get_common_drive_paths() {
    std::vector<std::string> paths;
    
    // Internal drive (home directory)
    paths.push_back(std::getenv("HOME") ? std::getenv("HOME") : "/tmp");
    
    // Common external drive mount points on macOS
    std::vector<std::string> external_candidates = {
        "/Volumes",
        "/System/Volumes/Data", // macOS system data volume
        "/tmp" // fallback
    };
    
    for (const auto& candidate : external_candidates) {
        if (std::filesystem::exists(candidate) && 
            std::filesystem::is_directory(candidate)) {
            
            if (candidate == "/Volumes") {
                // List all mounted volumes
                try {
                    for (const auto& entry : std::filesystem::directory_iterator(candidate)) {
                        if (entry.is_directory() && 
                            entry.path().filename() != "Macintosh HD") {
                            paths.push_back(entry.path().string());
                        }
                    }
                } catch (...) {
                    // If we can't list volumes, just add the base path
                    paths.push_back(candidate);
                }
            } else {
                paths.push_back(candidate);
            }
        }
    }
    
    return paths;
}

void print_result(const DriveBenchmark::BenchmarkResult& result) {
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "Path: " << result.path << std::endl;
    
    if (!result.success) {
        std::cout << "❌ FAILED: " << result.error_message << std::endl;
        return;
    }
    
    std::cout << "✅ SUCCESS" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Write Speed:      " << std::setw(8) << result.write_speed_mbps << " MB/s" << std::endl;
    std::cout << "Read Speed:       " << std::setw(8) << result.read_speed_mbps << " MB/s" << std::endl;
    std::cout << "Sequential Read:  " << std::setw(8) << result.sequential_read_mbps << " MB/s" << std::endl;
    
    // Classify drive type based on performance
    double avg_speed = (result.write_speed_mbps + result.read_speed_mbps) / 2.0;
    std::string drive_type;
    if (avg_speed > 500) {
        drive_type = "🚀 NVMe SSD";
    } else if (avg_speed > 200) {
        drive_type = "⚡ SATA SSD";
    } else if (avg_speed > 50) {
        drive_type = "💾 Fast HDD";
    } else {
        drive_type = "🐌 Slow Drive";
    }
    
    std::cout << "Estimated Type:   " << drive_type << std::endl;
}

int main() {
    std::cout << "🔍 Drive Benchmark Tool" << std::endl;
    std::cout << "Testing 100MB read/write operations..." << std::endl;
    
    DriveBenchmark benchmark;
    auto paths = get_common_drive_paths();
    
    if (paths.empty()) {
        std::cout << "❌ No accessible drive paths found!" << std::endl;
        return 1;
    }
    
    std::vector<DriveBenchmark::BenchmarkResult> results;
    
    for (const auto& path : paths) {
        std::cout << "\n📊 Testing: " << path << " ..." << std::endl;
        
        auto result = benchmark.benchmark_path(path + "/rouen_benchmark");
        results.push_back(result);
        
        // Print quick status
        if (result.success) {
            std::cout << "   ✅ Complete - Write: " << std::fixed << std::setprecision(1) 
                      << result.write_speed_mbps << " MB/s, Read: " 
                      << result.read_speed_mbps << " MB/s" << std::endl;
        } else {
            std::cout << "   ❌ Failed: " << result.error_message << std::endl;
        }
    }
    
    // Print detailed results
    std::cout << "\n\n📈 DETAILED RESULTS:" << std::endl;
    for (const auto& result : results) {
        print_result(result);
    }
    
    // Summary comparison
    std::cout << "\n\n📊 SUMMARY COMPARISON:" << std::endl;
    std::cout << std::string(80, '=') << std::endl;
    std::cout << std::left << std::setw(30) << "Drive Path" 
              << std::setw(12) << "Write MB/s" 
              << std::setw(12) << "Read MB/s"
              << std::setw(15) << "Seq Read MB/s"
              << "Type" << std::endl;
    std::cout << std::string(80, '-') << std::endl;
    
    for (const auto& result : results) {
        if (result.success) {
            std::string short_path = result.path.length() > 28 ? 
                                   "..." + result.path.substr(result.path.length() - 25) : 
                                   result.path;
            
            double avg_speed = (result.write_speed_mbps + result.read_speed_mbps) / 2.0;
            std::string type = avg_speed > 500 ? "NVMe" : 
                              avg_speed > 200 ? "SSD" : 
                              avg_speed > 50 ? "HDD" : "Slow";
            
            std::cout << std::left << std::setw(30) << short_path
                      << std::fixed << std::setprecision(1)
                      << std::setw(12) << result.write_speed_mbps
                      << std::setw(12) << result.read_speed_mbps
                      << std::setw(15) << result.sequential_read_mbps
                      << type << std::endl;
        }
    }
    
    return 0;
}
