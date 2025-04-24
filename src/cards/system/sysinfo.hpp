#pragma once

#include <format>
#include <string>
#include <chrono>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <imgui/imgui.h>
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
        width = 350.0f;
        
        // Request higher refresh rate for updating metrics
        requested_fps = 5;  // Update 5 times per second
        
        // Initialize last update time
        last_update = std::chrono::steady_clock::now();
    }
    
    // Add explicit destructor
    ~sysinfo_card() override {
        // Ensure proper cleanup
        memory_info = {0.0, 0.0, 0.0};
        disk_info = {0.0, 0.0, 0.0};
        cpu_usage = 0.0;
    }
    
    // Get memory information (total, used, free)
    std::tuple<double, double, double> get_memory_info() {
        struct sysinfo memInfo;
        sysinfo(&memInfo);
        
        double total = static_cast<double>(memInfo.totalram) * memInfo.mem_unit / (1024 * 1024 * 1024); // Total RAM in GB
        double free = static_cast<double>(memInfo.freeram) * memInfo.mem_unit / (1024 * 1024 * 1024);  // Free RAM in GB
        double used = total - free; // Used RAM in GB
        
        return {total, used, free};
    }
    
    // Get disk space information (total, used, free)
    std::tuple<double, double, double> get_disk_info(const std::string& path = "/") {
        struct statvfs stat;
        statvfs(path.c_str(), &stat);
        
        double total = static_cast<double>(stat.f_blocks) * static_cast<double>(stat.f_frsize) / (1024.0 * 1024.0 * 1024.0); // Total space in GB
        double free = static_cast<double>(stat.f_bfree) * static_cast<double>(stat.f_frsize) / (1024.0 * 1024.0 * 1024.0);  // Free space in GB
        double used = total - free; // Used space in GB
        
        return {total, used, free};
    }
    
    // Get CPU usage in percentage
    double get_cpu_usage() {
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
            double idle_delta = static_cast<double>(idle - prev_idle);
            double total_delta = static_cast<double>(total - prev_total);
            percent = 100.0 * (1.0 - idle_delta / total_delta);
        }
        
        prev_idle = idle;
        prev_total = total;
        
        return percent;
    }
    
    // Draw a progress bar with text overlay
    void draw_progress_bar(const char* label, float fraction, const char* overlay_text) {
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, colors[2]);
        ImGui::ProgressBar(fraction, ImVec2(-1, 0), overlay_text);
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::Text("%s", label);
    }
    
    bool render() override {
        return render_window([this]() {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update).count();
            
            // Update metrics every 500ms (twice per second)
            if (elapsed >= 500) {
                refresh_metrics();
                last_update = now;
            }
            
            // System uptime information
            struct sysinfo si;
            sysinfo(&si);
            long days = si.uptime / (60 * 60 * 24);
            int hours = static_cast<int>((si.uptime / (60 * 60)) % 24);
            int minutes = static_cast<int>((si.uptime / 60) % 60);
            int seconds = static_cast<int>(si.uptime % 60);
            
            ImGui::Text("System Uptime: %ld days, %d:%02d:%02d", days, hours, minutes, seconds);
            ImGui::Separator();
            
            // Memory section
            ImGui::Text("Memory Usage:");
            auto [mem_total, mem_used, mem_free] = memory_info;
            std::string mem_text = std::format("{:.2f}/{:.2f} GB ({:.1f}%)", mem_used, mem_total, (mem_used / mem_total) * 100.0);
            draw_progress_bar("RAM", static_cast<float>(mem_used / mem_total), mem_text.c_str());
            
            ImGui::Spacing();
            
            // Disk space section
            ImGui::Text("Disk Usage:");
            auto [disk_total, disk_used, disk_free] = disk_info;
            std::string disk_text = std::format("{:.2f}/{:.2f} GB ({:.1f}%)", disk_used, disk_total, (disk_used / disk_total) * 100.0);
            draw_progress_bar("Disk", static_cast<float>(disk_used / disk_total), disk_text.c_str());
            
            ImGui::Spacing();
            
            // CPU usage section
            ImGui::Text("CPU Usage:");
            std::string cpu_text = std::format("{:.1f}%", cpu_usage);
            draw_progress_bar("CPU", static_cast<float>(cpu_usage / 100.0), cpu_text.c_str());
            
            // Display number of processes
            ImGui::Text("Running Processes: %d", si.procs);
        });
    }
    
    void refresh_metrics() {
        memory_info = get_memory_info();
        disk_info = get_disk_info();
        cpu_usage = get_cpu_usage();
    }

    std::string get_uri() const override {
        return "sysinfo";
    }
    
private:
    std::chrono::steady_clock::time_point last_update;
    std::tuple<double, double, double> memory_info {0.0, 0.0, 0.0};
    std::tuple<double, double, double> disk_info {0.0, 0.0, 0.0};
    double cpu_usage = 0.0;
};

} // namespace rouen::cards