#pragma once

#include <imgui/imgui.h>
#include <string>
#include <memory>
#include <format>
#include <chrono>
#include <ctime>

#include "../interface/card.hpp"
#include "../../helpers/debug.hpp"
#include "../../hosts/weather_host.hpp"
#include "../../registrar.hpp"

namespace rouen::cards {

class weather : public card {
public:
    weather(std::string_view location) {
        // Set custom colors for the Weather card
        colors[0] = {0.4f, 0.6f, 0.8f, 1.0f}; // Blue primary color
        colors[1] = {0.5f, 0.7f, 0.9f, 0.7f}; // Lighter blue secondary color
        
        // Additional colors for specific elements
        get_color(2, ImVec4(0.6f, 0.8f, 1.0f, 1.0f)); // Light blue for titles
        get_color(3, ImVec4(0.3f, 0.8f, 0.3f, 1.0f)); // Green for positive conditions
        get_color(4, ImVec4(0.8f, 0.4f, 0.4f, 1.0f)); // Red for negative conditions
        get_color(5, ImVec4(0.7f, 0.7f, 0.7f, 1.0f)); // Light gray for secondary text
        get_color(6, ImVec4(0.9f, 0.7f, 0.3f, 1.0f)); // Orange for warnings/alerts
        
        name("Weather & Time");
        width = 320.0f;
        requested_fps = 1;  // Update once per second for the clock
        
        // Get the weather host
        weather_host = hosts::WeatherHost::getHost();

        weather_host->setLocation(location);
        weather_host->refreshWeather();
        
        DB_INFO("Weather card: Constructor completed");
    }
    
    ~weather() override = default;

    bool render() override {
        return render_window([this]() {
            // Display current time
            render_time();
            
            ImGui::Separator();
            
            // Display current weather
            render_weather();
            
            ImGui::Separator();
            
            // Display weather forecast
            render_forecast();
            
            // Allow changing location
            render_location_input();
        });
    }

    std::string get_uri() const override {
        return std::format("weather:{}", weather_host->getLocation());
    }
    
private:
    void render_time() {
        // Get current time
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::tm local_tm = *std::localtime(&time_t_now);
        
        // Format time
        std::string time_str = std::format("{:02d}:{:02d}:{:02d}", 
            local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec);
        
        // Format date
        std::string date_str = std::format("{:02d}/{:02d}/{:04d}", 
            local_tm.tm_mday, local_tm.tm_mon + 1, local_tm.tm_year + 1900);
        
        // Display time in large font
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); // Monospaced font
        ImGui::TextColored(colors[2], "%s", time_str.c_str());
        ImGui::PopFont();
        
        ImGui::SameLine();
        // Display date below
        ImGui::TextColored(colors[5], "%s", date_str.c_str());
    }
    
    void render_weather() {
        auto current_weather = weather_host->getCurrentWeather();
        
        if (!current_weather) {
            ImGui::TextColored(colors[5], "Weather data loading...");
            return;
        }
        
        try {
            // Display current weather
            ImGui::TextColored(colors[2], "Current Weather in %s", current_weather->name.c_str());
            ImGui::Spacing();
            
            ImGui::TextColored(colors[0], "%.1f°C", current_weather->main.temp);
            ImGui::SameLine();
            ImGui::TextColored(colors[5], "(Feels like: %.1f°C)", current_weather->main.feels_like);
            
            if (!current_weather->weather.empty()) {
                std::string description = current_weather->weather[0].description;
                
                // Capitalize the weather description
                if (!description.empty()) {
                    description[0] = static_cast<char>(std::toupper(description[0]));
                }
                
                ImGui::Text("%s", description.c_str());
            }
            
            // More weather details
            ImGui::Spacing();
            ImGui::Columns(2, "weather_details", false);
            
            // Column 1
            ImGui::Text("Humidity: %d%%", current_weather->main.humidity);
            ImGui::Text("Pressure: %d hPa", static_cast<int>(current_weather->main.pressure));
            
            // Column 2
            ImGui::NextColumn();
            ImGui::Text("Wind: %.1f m/s", current_weather->wind.speed);
            ImGui::Text("Clouds: %d%%", current_weather->clouds.all);
            
            ImGui::Columns(1);
            
        } catch (const std::exception& e) {
            ImGui::TextColored(colors[4], "%s", ("Error displaying weather data: " + std::string(e.what())).c_str());
        }
    }
    
    void render_forecast() {
        auto forecast = weather_host->getForecast();
        
        if (!forecast) {
            ImGui::TextColored(colors[5], "Forecast data loading...");
            return;
        }
        
        try {
            ImGui::TextColored(colors[2], "Forecast");
            ImGui::Spacing();
            
            // Only show the forecast periods
            const size_t forecast_items_to_show = std::min(5ul, forecast->list.size());
            
            // Start a table
            if (ImGui::BeginTable("forecast_table", 3, ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("Time");
                ImGui::TableSetupColumn("Temp");
                ImGui::TableSetupColumn("Condition");
                ImGui::TableHeadersRow();
                
                for (size_t i = 0; i < forecast_items_to_show; i++) {
                    const auto& item = forecast->list[i];
                    
                    // Get the time
                    std::string time_str = item.dt_txt;
                    // Format shows "YYYY-MM-DD HH:MM:SS" - we just want hours
                    size_t pos = time_str.find(' ');
                    if (pos != std::string::npos) {
                        time_str = time_str.substr(pos + 1, 5); // HH:MM
                    }
                    
                    // Get weather condition
                    std::string condition;
                    if (!item.weather.empty()) {
                        condition = item.weather[0].description;
                        
                        // Capitalize the condition
                        if (!condition.empty()) {
                            condition[0] = static_cast<char>(std::toupper(condition[0]));
                        }
                    }
                    
                    // Add row
                    ImGui::TableNextRow();
                    
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", time_str.c_str());
                    
                    ImGui::TableNextColumn();
                    ImGui::Text("%.1f°C", item.main.temp);
                    
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", condition.c_str());
                }
                
                ImGui::EndTable();
            }
        } catch (const std::exception& e) {
            ImGui::TextColored(colors[4], "%s", ("Error displaying forecast data: " + std::string(e.what())).c_str());
        }
    }
    
    void render_location_input() {
        static char location_buffer[64] = "";
        static bool initialized = false;
        
        if (!initialized) {
            std::string current_location = weather_host->getLocation();
            strncpy(location_buffer, current_location.c_str(), sizeof(location_buffer) - 1);
            location_buffer[sizeof(location_buffer) - 1] = '\0';
            initialized = true;
        }
        
        ImGui::Separator();
        ImGui::TextColored(colors[2], "Location:");
        
        ImGui::PushItemWidth(-1);
        if (ImGui::InputText("##location", location_buffer, sizeof(location_buffer), 
                              ImGuiInputTextFlags_EnterReturnsTrue)) {
            // Update location when Enter is pressed
            weather_host->setLocation(location_buffer);
            // Force a refresh
            weather_host->refreshWeather();
        }
        ImGui::PopItemWidth();
        
        ImGui::SameLine();
        if (ImGui::Button("Update")) {
            weather_host->setLocation(location_buffer);
            weather_host->refreshWeather();
        }
        
        ImGui::TextColored(colors[5], "Format: City,CountryCode (e.g., London,uk)");
    }
    
private:
    std::shared_ptr<hosts::WeatherHost> weather_host;
};

} // namespace rouen::cards