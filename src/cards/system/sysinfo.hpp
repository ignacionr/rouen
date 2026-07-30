#pragma once

#include <format>
#include <string>
#include <chrono>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <future>
#include <thread>

// Platform-specific includes for system information
#ifdef __APPLE__
#include "../../helpers/compat/sysinfo.hpp" // macOS compatibility layer
#include <sys/mount.h> // for statfs (macOS equivalent of statvfs)
#elif defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#else
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#endif

#include "../../helpers/imgui_include.hpp"
#include "../../helpers/drive_benchmark.hpp"
#include "../../helpers/card_render_metrics.hpp"
#include "../../helpers/vu_meter.hpp"
#include "../interface/card.hpp"

namespace rouen::cards {

struct sysinfo_card : public card {
    sysinfo_card() {
        // Set custom colors for the card
        colors[0] = {0.3f, 0.6f, 0.3f, 1.0f};   // Green primary color (first_color)
        colors[1] = {0.2f, 0.7f, 0.4f, 0.7f};  // Light green secondary color (second_color)
        
        // Additional color for progress bars (index 2)
        colors[2] = {0.2f, 0.7f, 0.2f, 1.0f}; // Green progress bar color
        
        name("System Info");
        width = 470.0f; // Increased width to accommodate benchmark results
        
        // Request higher refresh rate for updating metrics
        requested_fps = 5;  // Update 5 times per second
        
        // Initialize last update time
        last_update = std::chrono::steady_clock::now();
        
        // benchmark state is initialized via member declarations
    }
    
    // Add explicit destructor
    ~sysinfo_card() override {
        // Ensure proper cleanup
        memory_info = {0.0, 0.0, 0.0};
        disk_info = {0.0, 0.0, 0.0};
        cpu_usage = 0.0;
        
        // Wait for benchmark to complete if running
        if (benchmark_future.valid()) {
            benchmark_future.wait();
        }
    }
    
    // Get memory information (total, used, free)
    static std::tuple<double, double, double> get_memory_info() {
#ifdef _WIN32
        MEMORYSTATUSEX memStatus;
        memStatus.dwLength = sizeof(memStatus);
        if (GlobalMemoryStatusEx(&memStatus)) {
            double total = static_cast<double>(memStatus.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0); // Total RAM in GB
            double free = static_cast<double>(memStatus.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);  // Available RAM in GB
            double used = total - free; // Used RAM in GB
            return {total, used, free};
        }
        return {0.0, 0.0, 0.0};
#else
        struct sysinfo memInfo;
        sysinfo(&memInfo);
        
        double total = static_cast<double>(memInfo.totalram) * memInfo.mem_unit / (1024 * 1024 * 1024); // Total RAM in GB
        double free = static_cast<double>(memInfo.freeram) * memInfo.mem_unit / (1024 * 1024 * 1024);  // Free RAM in GB
        double used = total - free; // Used RAM in GB
        
        return {total, used, free};
#endif
    }
    
    // Get disk space information (total, used, free)
    static std::tuple<double, double, double> get_disk_info(const std::string& path = "/") {
#ifdef _WIN32
        // Windows: Use GetDiskFreeSpaceEx for C: drive by default
        std::string drive_path = (path == "/") ? "C:\\" : path;
        ULARGE_INTEGER freeBytesAvailable, totalNumberOfBytes, totalNumberOfFreeBytes;
        
        if (GetDiskFreeSpaceExA(drive_path.c_str(), &freeBytesAvailable, &totalNumberOfBytes, &totalNumberOfFreeBytes)) {
            double total = static_cast<double>(totalNumberOfBytes.QuadPart) / (1024.0 * 1024.0 * 1024.0); // Total space in GB
            double free = static_cast<double>(totalNumberOfFreeBytes.QuadPart) / (1024.0 * 1024.0 * 1024.0);  // Free space in GB
            double used = total - free; // Used space in GB
            return {total, used, free};
        }
        return {0.0, 0.0, 0.0};
#elif defined(__APPLE__)
        struct statfs stat;
        statfs(path.c_str(), &stat);
        
        double total = static_cast<double>(stat.f_blocks) * static_cast<double>(stat.f_bsize) / (1024.0 * 1024.0 * 1024.0); // Total space in GB
        double free = static_cast<double>(stat.f_bfree) * static_cast<double>(stat.f_bsize) / (1024.0 * 1024.0 * 1024.0);  // Free space in GB
        double used = total - free; // Used space in GB
        return {total, used, free};
#else
        struct statvfs stat;
        statvfs(path.c_str(), &stat);
        
        double total = static_cast<double>(stat.f_blocks) * static_cast<double>(stat.f_frsize) / (1024.0 * 1024.0 * 1024.0); // Total space in GB
        double free = static_cast<double>(stat.f_bfree) * static_cast<double>(stat.f_frsize) / (1024.0 * 1024.0 * 1024.0);  // Free space in GB
        double used = total - free; // Used space in GB
        return {total, used, free};
#endif
    }
    
    // Get CPU usage in percentage
    static double get_cpu_usage() {
#ifdef _WIN32
        // Windows CPU usage calculation using Performance Data Helper (PDH)
        static ULARGE_INTEGER lastCPU, lastSysCPU, lastUserCPU;
        static int numProcessors = 0;
        static HANDLE self = GetCurrentProcess();
        static bool first_run = true;

        if (first_run) {
            SYSTEM_INFO sysInfo;
            FILETIME ftime, fsys, fuser;

            GetSystemInfo(&sysInfo);
            numProcessors = sysInfo.dwNumberOfProcessors;

            GetSystemTimeAsFileTime(&ftime);
            memcpy(&lastCPU, &ftime, sizeof(FILETIME));

            GetProcessTimes(self, &ftime, &ftime, &fsys, &fuser);
            memcpy(&lastSysCPU, &fsys, sizeof(FILETIME));
            memcpy(&lastUserCPU, &fuser, sizeof(FILETIME));
            first_run = false;
            return 0.0;
        }

        FILETIME ftime, fsys, fuser;
        ULARGE_INTEGER now, sys, user;
        double percent;

        GetSystemTimeAsFileTime(&ftime);
        memcpy(&now, &ftime, sizeof(FILETIME));

        GetProcessTimes(self, &ftime, &ftime, &fsys, &fuser);
        memcpy(&sys, &fsys, sizeof(FILETIME));
        memcpy(&user, &fuser, sizeof(FILETIME));

        percent = static_cast<double>(sys.QuadPart - lastSysCPU.QuadPart) +
                  static_cast<double>(user.QuadPart - lastUserCPU.QuadPart);
        percent /= static_cast<double>(now.QuadPart - lastCPU.QuadPart);
        percent /= numProcessors;
        lastCPU = now;
        lastUserCPU = user;
        lastSysCPU = sys;

        return percent * 100.0;
#elif defined(__APPLE__)
        // macOS CPU usage calculation
        static host_cpu_load_info_data_t prev_cpu_load;
        host_cpu_load_info_data_t cpu_load;
        mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
        
        kern_return_t error = host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, 
                                            reinterpret_cast<host_info_t>(&cpu_load), &count);
        if (error != KERN_SUCCESS) {
            return 0.0;
        }
        
        // Calculate CPU usage based on ticks with proper type casting to avoid precision loss warnings
        unsigned long long user_diff = cpu_load.cpu_ticks[CPU_STATE_USER] - prev_cpu_load.cpu_ticks[CPU_STATE_USER];
        unsigned long long system_diff = cpu_load.cpu_ticks[CPU_STATE_SYSTEM] - prev_cpu_load.cpu_ticks[CPU_STATE_SYSTEM];
        unsigned long long idle_diff = cpu_load.cpu_ticks[CPU_STATE_IDLE] - prev_cpu_load.cpu_ticks[CPU_STATE_IDLE];
        unsigned long long nice_diff = cpu_load.cpu_ticks[CPU_STATE_NICE] - prev_cpu_load.cpu_ticks[CPU_STATE_NICE];
        
        unsigned long long total_diff = user_diff + system_diff + idle_diff + nice_diff;
        double percent = 0.0;
        
        if (total_diff > 0) {
            // Cast to double before arithmetic operations to avoid precision loss
            double user_diff_d = static_cast<double>(user_diff);
            double system_diff_d = static_cast<double>(system_diff);
            double nice_diff_d = static_cast<double>(nice_diff);
            double total_diff_d = static_cast<double>(total_diff);
            
            percent = (user_diff_d + system_diff_d + nice_diff_d) * 100.0 / total_diff_d;
        }
        
        // Save current CPU load for next calculation
        prev_cpu_load = cpu_load;
        
        return percent;
#else
        // Linux CPU usage calculation
        static unsigned long long prev_idle = 0, prev_total = 0;
        unsigned long long idle = 0, total = 0;
        
        std::ifstream file("/proc/stat");
        if (!file.is_open()) {
            return 0.0; // Return 0 if we can't open the file
        }
        
        std::string line;
        if (!std::getline(file, line)) {
            file.close();
            return 0.0; // Return 0 if we can't read the line
        }
        file.close();
        
        std::istringstream iss(line);
        std::string cpu;
        unsigned long long user = 0, nice = 0, system = 0, idle_time = 0, 
                           iowait = 0, irq = 0, softirq = 0, steal = 0, 
                           guest = 0, guest_nice = 0;
        
        iss >> cpu >> user >> nice >> system >> idle_time >> iowait >> irq >> softirq >> steal >> guest >> guest_nice;
        
        // Check if we successfully read all values
        if (iss.fail()) {
            return 0.0; // Return 0 if parsing failed
        }
        
        idle = idle_time + iowait;
        total = idle + user + nice + system + irq + softirq + steal;
        
        double percent = 0.0;
        if (prev_total > 0 && total > prev_total) {
            auto idle_delta = static_cast<double>(idle - prev_idle);
            auto total_delta = static_cast<double>(total - prev_total);
            percent = 100.0 * (1.0 - idle_delta / total_delta);
        }
        
        prev_idle = idle;
        prev_total = total;
        
        return percent;
#endif
    }
    
    // Draw a progress bar with text overlay
    void draw_progress_bar(rouen::ui::ui_context& ui, const char* label, float fraction, const char* overlay_text) {
        ui.push_style_color(rouen::ui::style_color::plot_histogram, colors[2]);
        ui.progress_bar(fraction, ImVec2(400, 0), overlay_text);
        ui.pop_style_color();
        ui.same_line(0.0f, ui.get_item_inner_spacing_x());
        ui.text(label);
    }

    // Get system uptime in seconds
    static long get_system_uptime() {
#ifdef _WIN32
        return static_cast<long>(GetTickCount64() / 1000);
#elif defined(__APPLE__) || defined(__linux__)
        struct sysinfo si;
        sysinfo(&si);
        return si.uptime;
#else
        return 0;
#endif
    }

    // Get number of running processes
    static int get_process_count() {
#ifdef _WIN32
        int process_count = 0;
        HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hProcessSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32 pe32;
            pe32.dwSize = sizeof(PROCESSENTRY32);
            
            if (Process32First(hProcessSnap, &pe32)) {
                do {
                    process_count++;
                } while (Process32Next(hProcessSnap, &pe32));
            }
            CloseHandle(hProcessSnap);
        }
        return process_count;
#elif defined(__APPLE__) || defined(__linux__)
        struct sysinfo si;
        sysinfo(&si);
        return si.procs;
#else
        return 0;
#endif
    }
    
    bool render(rouen::ui::ui_context& ui) override {
        return render_window([this, &ui]() {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update).count();
            
            // Update metrics every 500ms (twice per second)
            if (elapsed >= 500) {
                refresh_metrics();
                last_update = now;
            }
            
            // System uptime information
            long uptime_seconds = get_system_uptime();
            long days = uptime_seconds / (60L * 60L * 24L);
            int hours = static_cast<int>((uptime_seconds / (60L * 60L)) % 24L);
            int minutes = static_cast<int>((uptime_seconds / 60) % 60);
            int seconds = static_cast<int>(uptime_seconds % 60);
            
            ui.text(std::format("System Uptime: {} days, {}:{:02d}:{:02d}", days, hours, minutes, seconds));
            ui.separator();
            
            // Memory section
            auto [mem_total, mem_used, mem_free] = memory_info;
            std::string mem_text = std::format("{:.2f}/{:.2f} GB ({:.1f}%)", mem_used, mem_total, (mem_used / mem_total) * 100.0);
            draw_progress_bar(ui, "RAM", static_cast<float>(mem_used / mem_total), mem_text.c_str());
            
            ui.spacing();
            
            // Disk space section
            auto [disk_total, disk_used, disk_free] = disk_info;
            std::string disk_text = std::format("{:.2f}/{:.2f} GB ({:.1f}%)", disk_used, disk_total, (disk_used / disk_total) * 100.0);
            draw_progress_bar(ui, "Disk", static_cast<float>(disk_used / disk_total), disk_text.c_str());
            
            ui.spacing();
            
            // CPU usage section
            std::string cpu_text = std::format("{:.1f}%", cpu_usage);
            draw_progress_bar(ui, "CPU", static_cast<float>(cpu_usage / 100.0), cpu_text.c_str());
            
            // Display number of processes
            int process_count = get_process_count();
            ui.text(std::format("Running Processes: {}", process_count));
            
            ui.separator();

            // Card Render Times Section
            ui.text("Active Card Render Performance:");
            auto card_metrics = rouen::helpers::CardRenderMetrics::instance().get_all_metrics();

            float total_avg_ms = 0.0f;
            for (const auto& metric : card_metrics) {
                total_avg_ms += static_cast<float>(metric.avg_render_ms);
            }

            // Render sum average render time VU meter (max 14.0 ms budget)
            float norm_val = std::clamp(total_avg_ms / 14.0f, 0.0f, 1.0f);

            static float card_render_watermark = 0.0f;

            if (norm_val >= card_render_watermark) {
                card_render_watermark = norm_val;
            } else {
		card_render_watermark = std::max(norm_val, card_render_watermark * 0.99f);
            }

            rouen::helpers::vu_meter::VUMeterConfig vu_cfg;
            vu_cfg.scale_type = rouen::helpers::vu_meter::VUMeterScaleType::Custom;
            vu_cfg.custom_ticks = {
                { 0.00f, "0ms", true },
                { 0.25f, "3.5ms", true },
                { 0.50f, "7ms", true },
                { 0.75f, "10.5ms", true },
                { 1.00f, "14ms", true }
            };
            vu_cfg.title = std::format("SUM AVG: {:.2f} ms", total_avg_ms);
            vu_cfg.left_channel_title = "TOTAL RENDER TIME";
            vu_cfg.show_titles = true;
            vu_cfg.style.theme = rouen::helpers::vu_meter::VUMeterTheme::VintageLitAmber;
            vu_cfg.style.overload_threshold = 0.75f;

            float avail_w = ImGui::GetContentRegionAvail().x;
            float meter_w = std::min(avail_w, 240.0f);
            float meter_h = 95.0f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - meter_w) * 0.5f);
            rouen::helpers::vu_meter::render_analog_dial(ImVec2(meter_w, meter_h), norm_val, card_render_watermark, "TOTAL RENDER TIME", vu_cfg);
            ui.spacing();

            if (card_metrics.empty()) {
                ui.text("No active card metrics recorded yet.");
            } else {
                if (ui.begin_table("CardRenderMetricsTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                    ui.table_setup_column("Card Title", ImGuiTableColumnFlags_WidthStretch);
                    ui.table_setup_column("Last (ms)", ImGuiTableColumnFlags_WidthFixed, 75.0f);
                    ui.table_setup_column("Avg (ms)", ImGuiTableColumnFlags_WidthFixed, 75.0f);
                    ui.table_setup_column("Max (ms)", ImGuiTableColumnFlags_WidthFixed, 75.0f);
                    ui.table_headers_row();

                    for (const auto& metric : card_metrics) {
                        ui.table_next_row();

                        ui.table_set_column_index(0);
                        ui.text(metric.title);

                        ui.table_set_column_index(1);
                        ui.text(std::format("{:.2f}", metric.last_render_ms));

                        ui.table_set_column_index(2);
                        ui.text(std::format("{:.2f}", metric.avg_render_ms));

                        ui.table_set_column_index(3);
                        ui.text(std::format("{:.2f}", metric.max_render_ms));
                    }
                    ui.end_table();
                }
            }

            ui.separator();
            
            // Drive Benchmark section
            ui.text("Drive Benchmark:");
            
            if (!benchmark_running) {
                if (ui.button("Run Drive Benchmark", ImVec2(-1, 0))) {
                    start_drive_benchmark();
                }
                
                // Display previous results if available
                if (!benchmark_results.empty()) {
                    ui.spacing();
                    ui.text("Last Benchmark Results:");
                    
                    // Create a table for the results
                    if (ui.begin_table("BenchmarkTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                        ui.table_setup_column("Drive", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                        ui.table_setup_column("Write MB/s", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                        ui.table_setup_column("Read MB/s", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                        ui.table_setup_column("Type", ImGuiTableColumnFlags_WidthStretch);
                        ui.table_headers_row();
                        
                        for (const auto& result : benchmark_results) {
                            ui.table_next_row();
                            
                            ui.table_set_column_index(0);
                            ui.text(result.display_name);
                            
                            ui.table_set_column_index(1);
                            if (result.success) {
                                ui.text(std::format("{:.1f}", result.write_speed_mbps));
                            } else {
                                ui.text("Error");
                            }
                            
                            ui.table_set_column_index(2);
                            if (result.success) {
                                ui.text(std::format("{:.1f}", result.read_speed_mbps));
                            } else {
                                ui.text("Error");
                            }
                            
                            ui.table_set_column_index(3);
                            ui.text(result.get_drive_type());
                        }
                        
                        ui.end_table();
                    }
                }
            } else {
                // Show progress when benchmark is running
                ui.text("Benchmarking drives...");
                ui.progress_bar(benchmark_progress, ImVec2(-1, 0), "");
                ui.text("This may take a few moments...");
                
                // Check if benchmark is complete
                if (benchmark_future.valid() && 
                    benchmark_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                    benchmark_results = benchmark_future.get();
                    benchmark_running = false;
                    benchmark_progress = 0.0f;
                }
            }
        });
    }
    
    void refresh_metrics() {
        memory_info = get_memory_info();
        disk_info = get_disk_info();
        cpu_usage = get_cpu_usage();
    }
    
    void start_drive_benchmark() {
        if (benchmark_running) return;
        
        benchmark_running = true;
        benchmark_progress = 0.0f;
        
        // Run benchmark in a separate thread
        benchmark_future = std::async(std::launch::async, [this]() {
            rouen::helpers::DriveBenchmark benchmark;
            auto drive_paths = benchmark.get_common_drive_paths();
            std::vector<rouen::helpers::DriveBenchmark::BenchmarkResult> results;
            
            float progress_step = 1.0f / static_cast<float>(drive_paths.size());
            
            for (size_t i = 0; i < drive_paths.size(); ++i) {
                const auto& [path, display_name] = drive_paths[i];
                
                auto result = benchmark.benchmark_path(path, display_name);
                results.push_back(result);
                
                // Update progress
                benchmark_progress = static_cast<float>(i + 1) * progress_step;
            }
            
            return results;
        });
    }

    std::string get_uri() const override {
        return "sysinfo";
    }
    
private:
    std::chrono::steady_clock::time_point last_update;
    std::tuple<double, double, double> memory_info {0.0, 0.0, 0.0};
    std::tuple<double, double, double> disk_info {0.0, 0.0, 0.0};
    double cpu_usage = 0.0;
    
    // Drive benchmark members
    bool benchmark_running{false};
    float benchmark_progress{0.0F};
    std::future<std::vector<rouen::helpers::DriveBenchmark::BenchmarkResult>> benchmark_future;
    std::vector<rouen::helpers::DriveBenchmark::BenchmarkResult> benchmark_results;
};

} // namespace rouen::cards
