#pragma once

#include "../../helpers/imgui_include.hpp"
#include "../../external/IconsMaterialDesign.h"
#include <string>
#include <memory>
#include <format>
#include <chrono>
#include <ctime>
#include <cmath>
#include <sstream>
#include <fstream>
#include <filesystem>

#include "../../helpers/glaze_include.hpp"
#include "../../helpers/platform_utils.hpp"
#include "../interface/card.hpp"
#include "../../helpers/debug.hpp"
#include "../../helpers/config_service.hpp"
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
        width = 520.0f;
        requested_fps = 1;  // Update once per second for the clock
        
        // Get the weather host
        weather_host = std::make_shared<hosts::WeatherHost>();

        setLocation(location);
        
        DB_INFO("Weather card: Constructor completed");
    }
    
    ~weather() override = default;

    std::vector<mcp_function> get_mcp_functions() const override {
        std::vector<mcp_function> functions;
        
        // Function 1: Create a weather card for a specific city
        functions.emplace_back(
            "create_weather_card",
            "Create a new weather card for a specific city. Format: City,CountryCode (e.g., 'London,uk' or 'Tokyo,jp')",
            R"mcp({"type":"object","properties":{"city":{"type":"string","description":"City name and optional country code in format 'City,CountryCode' (e.g., 'London,uk', 'New York,us', 'Tokyo,jp')"}},"required":["city"]})mcp",
            [](const std::string& params) -> std::string {
                if (params.empty()) {
                    return R"({"status":"error","message":"Missing params"})";
                }
                
                // Parse the city parameter
                struct create_weather_params {
                    std::string city;
                };
                
                create_weather_params request{};
                auto parse_result = glz::read_json(request, params);
                if (parse_result || request.city.empty()) {
                    return R"({"status":"error","message":"Invalid params. Expected JSON with 'city' field."})";
                }
                
                // Get the create_card function from the registrar
                try {
                    auto create_card_fn = registrar::get<std::function<void(std::string const&)>>("create_card");
                    
                    // Create the weather card with the specified city
                    std::string card_uri = std::format("weather:{}", request.city);
                    (*create_card_fn)(card_uri);
                    
                    return std::format(R"({{"status":"success","message":"Weather card created for {}","city":"{}"}})", 
                                     request.city, request.city);
                } catch (const std::exception& e) {
                    return std::format(R"({{"status":"error","message":"create_card service is not available: {}"}})", e.what());
                }
            }
        );
        
        // Function 2: Get current weather for a city
        functions.emplace_back(
            "get_current_weather",
            "Get current weather information for a specific city. Returns temperature, conditions, humidity, wind speed, etc.",
            R"mcp({"type":"object","properties":{"city":{"type":"string","description":"City name and optional country code in format 'City,CountryCode' (e.g., 'London,uk', 'Paris,fr')"}},"required":["city"]})mcp",
            [](const std::string& params) -> std::string {
                if (params.empty()) {
                    return R"({"status":"error","message":"Missing params"})";
                }
                
                struct weather_params {
                    std::string city;
                };
                
                weather_params request{};
                auto parse_result = glz::read_json(request, params);
                if (parse_result || request.city.empty()) {
                    return R"({"status":"error","message":"Invalid params. Expected JSON with 'city' field."})";
                }
                
                // Ensure config is loaded
                auto config = rouen::helpers::ConfigService::instance();
                std::string api_key = config->get_env("OPENWEATHER_KEY");
                
                if (api_key.empty()) {
                    return R"({"status":"error","message":"OpenWeather API key not configured. Please set OPENWEATHER_KEY in your environment or .env file."})";
                }
                
                // Create a temporary weather host to fetch data
                auto temp_weather_host = std::make_shared<hosts::WeatherHost>();
                temp_weather_host->setLocation(request.city);
                temp_weather_host->refreshWeather();
                
                auto current_weather = temp_weather_host->getCurrentWeather();
                if (!current_weather) {
                    return std::format(R"({{"status":"error","message":"Failed to fetch weather data for {}. The API may be unavailable or the city name may be incorrect.","city":"{}"}})", 
                                     request.city, request.city);
                }
                
                // Build the response JSON
                std::string weather_desc = !current_weather->weather.empty() ? 
                    current_weather->weather[0].description : "Unknown";
                std::string weather_main = !current_weather->weather.empty() ? 
                    current_weather->weather[0].main : "Unknown";
                
                return std::format(
                    R"({{"status":"success","city":"{}","country":"{}","temperature":{:.1f},"feels_like":{:.1f},"humidity":{},"pressure":{},"wind_speed":{:.1f},"clouds":{},"weather":"{}","description":"{}"}})",
                    current_weather->name,
                    current_weather->sys.country,
                    current_weather->main.temp,
                    current_weather->main.feels_like,
                    current_weather->main.humidity,
                    static_cast<int>(current_weather->main.pressure),
                    current_weather->wind.speed,
                    current_weather->clouds.all,
                    weather_main,
                    weather_desc
                );
            }
        );
        
        // Function 3: Get weather forecast for a city
        functions.emplace_back(
            "get_weather_forecast",
            "Get weather forecast for a specific city. Returns the next 5 forecast periods (typically 3-hour intervals).",
            R"mcp({"type":"object","properties":{"city":{"type":"string","description":"City name and optional country code in format 'City,CountryCode' (e.g., 'London,uk', 'Berlin,de')"}},"required":["city"]})mcp",
            [](const std::string& params) -> std::string {
                if (params.empty()) {
                    return R"({"status":"error","message":"Missing params"})";
                }
                
                struct forecast_params {
                    std::string city;
                };
                
                forecast_params request{};
                auto parse_result = glz::read_json(request, params);
                if (parse_result || request.city.empty()) {
                    return R"({"status":"error","message":"Invalid params. Expected JSON with 'city' field."})";
                }
                
                // Ensure config is loaded
                auto config = rouen::helpers::ConfigService::instance();
                std::string api_key = config->get_env("OPENWEATHER_KEY");
                
                if (api_key.empty()) {
                    return R"({"status":"error","message":"OpenWeather API key not configured. Please set OPENWEATHER_KEY in your environment or .env file."})";
                }
                
                // Create a temporary weather host to fetch data
                auto temp_weather_host = std::make_shared<hosts::WeatherHost>();
                temp_weather_host->setLocation(request.city);
                temp_weather_host->refreshWeather();
                
                auto forecast = temp_weather_host->getForecast();
                if (!forecast) {
                    return std::format(R"({{"status":"error","message":"Failed to fetch forecast data for {}. The API may be unavailable or the city name may be incorrect.","city":"{}"}})", 
                                     request.city, request.city);
                }
                
                // Build the forecast JSON array (limit to 5 items)
                std::ostringstream forecast_json;
                forecast_json << R"({"status":"success","city":")" << forecast->city.name 
                             << R"(","country":")" << forecast->city.country 
                             << R"(","forecast":[)";
                
                const size_t max_items = std::min(static_cast<size_t>(5), forecast->list.size());
                for (size_t i = 0; i < max_items; ++i) {
                    const auto& item = forecast->list[i];
                    if (i > 0) forecast_json << ",";
                    
                    std::string weather_desc = !item.weather.empty() ? item.weather[0].description : "Unknown";
                    std::string weather_main = !item.weather.empty() ? item.weather[0].main : "Unknown";
                    
                    forecast_json << std::format(
                        R"({{"time":"{}","temperature":{:.1f},"humidity":{},"wind_speed":{:.1f},"weather":"{}","description":"{}","precipitation_probability":{:.0f}}})",
                        item.dt_txt,
                        item.main.temp,
                        item.main.humidity,
                        item.wind.speed,
                        weather_main,
                        weather_desc,
                        item.pop * 100.0
                    );
                }
                
                forecast_json << "]}";
                return forecast_json.str();
            }
        );
        
        return functions;
    }

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

    void render_video_ui() override {
        auto current_weather = weather_host->getCurrentWeather();
        if (!current_weather) return;

        // Dynamic Accent Color depending on weather condition
        ImVec4 accent_color = colors[0]; // Default blue
        std::string weather_main = current_weather->weather.empty() ? "Clear" : current_weather->weather[0].main;
        
        if (weather_main == "Clear") {
            accent_color = ImVec4(1.0f, 0.7f, 0.2f, 1.0f); // Warm sun orange
        } else if (weather_main == "Clouds") {
            accent_color = ImVec4(0.55f, 0.65f, 0.75f, 1.0f); // Soft slate gray/blue
        } else if (weather_main == "Rain" || weather_main == "Drizzle") {
            accent_color = ImVec4(0.2f, 0.7f, 1.0f, 1.0f); // Rain cyan/blue
        } else if (weather_main == "Thunderstorm") {
            accent_color = ImVec4(0.6f, 0.4f, 1.0f, 1.0f); // Storm purple
        } else if (weather_main == "Snow") {
            accent_color = ImVec4(0.85f, 0.95f, 1.0f, 1.0f); // Frosty ice white
        }

        ImGuiViewport* vp = ImGui::GetMainViewport();
        float vp_w = vp ? vp->Size.x : 1920.0f;
        float vp_h = vp ? vp->Size.y : 1080.0f;

        float win_w = std::min(620.0f, vp_w - 80.0f);
        float win_h = 370.0f;

        float pos_x = 40.0f;
        float pos_y = 40.0f;

        if (cast_position == 1) { // Top Right
            pos_x = std::max(40.0f, vp_w - win_w - 40.0f);
            pos_y = 40.0f;
        } else if (cast_position == 2) { // Bottom Left
            pos_x = 40.0f;
            pos_y = std::max(40.0f, vp_h - win_h - 40.0f);
        } else if (cast_position == 3) { // Bottom Right
            pos_x = std::max(40.0f, vp_w - win_w - 40.0f);
            pos_y = std::max(40.0f, vp_h - win_h - 40.0f);
        }

        ImGui::SetNextWindowPos(ImVec2(pos_x, pos_y));
        ImGui::SetNextWindowSize(ImVec2(win_w, win_h));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 3.0f);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.05f, 0.10f, 0.90f)); // Premium dark translucent backing
        ImGui::PushStyleColor(ImGuiCol_Border, accent_color); 

        if (ImGui::Begin("##WeatherVideoOverlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs)) {
            // Local Time Calculation with Timezone Offset
            auto now_tp = std::chrono::system_clock::now();
            auto timezone_offset = std::chrono::seconds(current_weather->timezone);
            auto local_tp = now_tp + timezone_offset;
            
            std::time_t local_tt = std::chrono::system_clock::to_time_t(local_tp);
            std::tm tm_local;
            gmtime_r(&local_tt, &tm_local);

            std::string time_str = std::format("{:02d}:{:02d}:{:02d}", tm_local.tm_hour, tm_local.tm_min, tm_local.tm_sec);
            std::string date_str = std::format("{:02d}/{:02d}/{:04d}", tm_local.tm_mday, tm_local.tm_mon + 1, tm_local.tm_year + 1900);

            // Top Header: City & Weather Icon
            ImGui::SetWindowFontScale(1.4f);
            ImGui::TextColored(accent_color, "%s  %s", get_weather_icon(weather_main), current_weather->name.c_str());
            ImGui::SameLine(ImGui::GetWindowWidth() - 180.0f);
            ImGui::SetWindowFontScale(1.05f);
            ImGui::TextColored(colors[5], "%s", date_str.c_str());
            
            ImGui::Separator();
            ImGui::Spacing();

            // Main Body: Left (Analog Clock + Digital Time), Right (Weather Metrics)
            // Left Group: Analog Clock & Digital Time
            ImGui::BeginGroup();
            ImVec2 clock_box_size(180.0f, 175.0f);
            ImGui::Dummy(clock_box_size);
            ImVec2 box_min = ImGui::GetItemRectMin();
            ImVec2 clock_center = ImVec2(box_min.x + clock_box_size.x * 0.5f, box_min.y + 60.0f);

            draw_analog_clock(ImGui::GetWindowDrawList(), clock_center, 56.0f, local_tp, accent_color);

            // Digital Time below clock
            ImGui::SetWindowFontScale(1.35f);
            ImVec2 t_size = ImGui::CalcTextSize(time_str.c_str());
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (clock_box_size.x - t_size.x * 1.35f) * 0.5f);
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", time_str.c_str());

            ImGui::EndGroup();

            // Right Group: Temperature & Telemetry
            ImGui::SameLine();
            ImGui::BeginGroup();

            ImGui::SetWindowFontScale(2.4f);
            ImGui::TextColored(accent_color, "%.1f°C", current_weather->main.temp);
            ImGui::SameLine();
            ImGui::SetWindowFontScale(1.15f);
            ImGui::TextColored(colors[5], "(Feels: %.1f°C)", current_weather->main.feels_like);

            if (!current_weather->weather.empty()) {
                std::string description = current_weather->weather[0].description;
                if (!description.empty()) {
                    description[0] = static_cast<char>(std::toupper(description[0]));
                }
                ImGui::TextColored(colors[3], "%s", description.c_str());
            }

            ImGui::Spacing();
            ImGui::Columns(2, "weather_details_video", false);
            
            // Humid / Press
            auto humidity_color = colors[0];
            if (current_weather->main.humidity > 80) humidity_color = ImVec4(0.2f, 0.8f, 1.0f, 1.0f);
            else if (current_weather->main.humidity < 30) humidity_color = ImVec4(1.0f, 0.6f, 0.2f, 1.0f);
            else humidity_color = ImVec4(0.2f, 1.0f, 0.4f, 1.0f);

            ImGui::TextColored(humidity_color, "%s Humidity: %d%%", ICON_MD_OPACITY, current_weather->main.humidity);
            ImGui::TextColored(colors[5], "%s Pressure: %d hPa", ICON_MD_SPEED, static_cast<int>(current_weather->main.pressure));

            ImGui::NextColumn();
            // Wind / Cloud
            auto wind_color = colors[0];
            if (current_weather->wind.speed > 10.0) wind_color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
            else if (current_weather->wind.speed > 5.0) wind_color = ImVec4(1.0f, 0.7f, 0.2f, 1.0f);
            else wind_color = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);

            ImGui::TextColored(wind_color, "%s Wind: %.1f m/s", ICON_MD_AIR, current_weather->wind.speed);
            
            auto cloud_color = colors[5];
            if (current_weather->clouds.all > 80) cloud_color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
            else if (current_weather->clouds.all > 50) cloud_color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
            else cloud_color = ImVec4(0.7f, 0.9f, 1.0f, 1.0f);
            ImGui::TextColored(cloud_color, "%s Clouds: %d%%", ICON_MD_CLOUD, current_weather->clouds.all);

            ImGui::Columns(1);
            ImGui::EndGroup();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Forecast block (Compact 3 items)
            auto forecast = weather_host->getForecast();
            if (forecast) {
                const size_t forecast_items_to_show = std::min(static_cast<size_t>(3), forecast->list.size());
                if (ImGui::BeginTable("forecast_table_video", 3, ImGuiTableFlags_BordersInnerV)) {
                    for (const auto& item : forecast->list | std::views::take(forecast_items_to_show)) {
                        std::string f_time_str = item.dt_txt;
                        size_t pos = f_time_str.find(' ');
                        if (pos != std::string::npos) {
                            f_time_str = f_time_str.substr(pos + 1, 5); // HH:MM
                        }
                        
                        std::string condition;
                        if (!item.weather.empty()) {
                            condition = item.weather[0].main;
                        }

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextColored(colors[5], "%s", f_time_str.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text("%.1f°C", item.main.temp);
                        ImGui::TableNextColumn();
                        const char* f_icon = get_weather_icon(condition);
                        ImGui::TextColored(colors[3], "%s %s", f_icon, condition.c_str());
                    }
                    ImGui::EndTable();
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }

    std::string get_uri() const override {
        return std::format("weather:{}", weather_host->getLocation());
    }
    
private:
    void draw_analog_clock(ImDrawList* draw_list, ImVec2 center, float radius, std::chrono::system_clock::time_point local_tp, ImVec4 accent_color) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(local_tp.time_since_epoch()).count() % 1000;
        std::time_t local_tt = std::chrono::system_clock::to_time_t(local_tp);
        std::tm tm_local;
        gmtime_r(&local_tt, &tm_local);

        float sec_f = static_cast<float>(tm_local.tm_sec) + static_cast<float>(ms) / 1000.0f;
        float min_f = static_cast<float>(tm_local.tm_min) + sec_f / 60.0f;
        float hour_f = static_cast<float>(tm_local.tm_hour % 12) + min_f / 60.0f;

        float hour_angle = (hour_f / 12.0f) * 2.0f * static_cast<float>(M_PI) - (static_cast<float>(M_PI) / 2.0f);
        float min_angle = (min_f / 60.0f) * 2.0f * static_cast<float>(M_PI) - (static_cast<float>(M_PI) / 2.0f);
        float sec_angle = (sec_f / 60.0f) * 2.0f * static_cast<float>(M_PI) - (static_cast<float>(M_PI) / 2.0f);

        // 1. Clock Face Backdrop & Bezel Glow
        ImU32 glow_col       = ImGui::ColorConvertFloat4ToU32(ImVec4(accent_color.x, accent_color.y, accent_color.z, 0.20f));
        ImU32 bg_col         = ImGui::ColorConvertFloat4ToU32(ImVec4(0.05f, 0.06f, 0.11f, 0.95f));
        ImU32 ring_col       = ImGui::ColorConvertFloat4ToU32(accent_color);
        ImU32 inner_ring_col = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.15f));

        draw_list->AddCircleFilled(center, radius + 5.0f, glow_col, 64);
        draw_list->AddCircleFilled(center, radius, bg_col, 64);
        draw_list->AddCircle(center, radius, ring_col, 64, 2.5f);
        draw_list->AddCircle(center, radius * 0.92f, inner_ring_col, 64, 1.0f);

        // 2. Ticks
        for (int i = 0; i < 60; ++i) {
            float angle = static_cast<float>(i) * (2.0f * static_cast<float>(M_PI) / 60.0f);
            float c = std::cos(angle);
            float s = std::sin(angle);

            if (i % 5 == 0) { // Hour ticks
                float r_in  = radius * 0.76f;
                float r_out = radius * 0.90f;
                ImVec2 p_in  = ImVec2(center.x + c * r_in,  center.y + s * r_in);
                ImVec2 p_out = ImVec2(center.x + c * r_out, center.y + s * r_out);
                
                ImU32 tick_col = (i % 15 == 0) ? ring_col : ImGui::ColorConvertFloat4ToU32(ImVec4(0.9f, 0.95f, 1.0f, 0.85f));
                draw_list->AddLine(p_in, p_out, tick_col, 2.2f);
            } else { // Minute ticks
                float r_in  = radius * 0.84f;
                float r_out = radius * 0.90f;
                ImVec2 p_in  = ImVec2(center.x + c * r_in,  center.y + s * r_in);
                ImVec2 p_out = ImVec2(center.x + c * r_out, center.y + s * r_out);
                
                draw_list->AddLine(p_in, p_out, ImGui::ColorConvertFloat4ToU32(ImVec4(0.7f, 0.75f, 0.85f, 0.35f)), 1.0f);
            }
        }

        // 3. Hour numbers (12, 3, 6, 9)
        struct CardinalText { const char* text; float angle_deg; };
        static const CardinalText cardinals[] = {
            {"12", -90.0f}, {"3", 0.0f}, {"6", 90.0f}, {"9", 180.0f}
        };
        for (const auto& card : cardinals) {
            float rad = card.angle_deg * (static_cast<float>(M_PI) / 180.0f);
            float dist = radius * 0.58f;
            ImVec2 txt_pos = ImVec2(center.x + std::cos(rad) * dist, center.y + std::sin(rad) * dist);
            ImVec2 txt_size = ImGui::CalcTextSize(card.text);
            draw_list->AddText(ImVec2(txt_pos.x - txt_size.x * 0.5f, txt_pos.y - txt_size.y * 0.5f), 
                               ImGui::ColorConvertFloat4ToU32(ImVec4(0.9f, 0.92f, 0.98f, 0.90f)), card.text);
        }

        // 4. Hands
        // Hour Hand
        float h_len = radius * 0.48f;
        ImVec2 h_pt = ImVec2(center.x + std::cos(hour_angle) * h_len, center.y + std::sin(hour_angle) * h_len);
        draw_list->AddLine(center, h_pt, ImGui::ColorConvertFloat4ToU32(ImVec4(0.95f, 0.96f, 1.0f, 1.0f)), 3.8f);

        // Minute Hand
        float m_len = radius * 0.74f;
        ImVec2 m_pt = ImVec2(center.x + std::cos(min_angle) * m_len, center.y + std::sin(min_angle) * m_len);
        draw_list->AddLine(center, m_pt, ImGui::ColorConvertFloat4ToU32(ImVec4(0.85f, 0.90f, 1.0f, 0.92f)), 2.4f);

        // Second Hand
        float s_len = radius * 0.85f;
        float s_tail_len = radius * 0.20f;
        ImVec2 s_pt = ImVec2(center.x + std::cos(sec_angle) * s_len, center.y + std::sin(sec_angle) * s_len);
        ImVec2 s_tail = ImVec2(center.x - std::cos(sec_angle) * s_tail_len, center.y - std::sin(sec_angle) * s_tail_len);
        ImU32 sec_col = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.35f, 0.35f, 1.0f));

        draw_list->AddLine(s_tail, s_pt, sec_col, 1.5f);
        draw_list->AddCircleFilled(s_tail, 3.0f, sec_col);

        // Center Pivot
        draw_list->AddCircleFilled(center, 4.5f, sec_col);
        draw_list->AddCircleFilled(center, 2.0f, ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));
    }

    void render_time() {
        // Get current local time with timezone offset
        auto now_tp = std::chrono::system_clock::now();
        auto current_weather = weather_host->getCurrentWeather();
        if (current_weather) {
            auto timezone_offset = std::chrono::seconds(current_weather->timezone);
            now_tp += timezone_offset;
        }

        std::time_t local_tt = std::chrono::system_clock::to_time_t(now_tp);
        std::tm tm_local;
        gmtime_r(&local_tt, &tm_local);

        std::string time_str = std::format("{:02d}:{:02d}:{:02d}", tm_local.tm_hour, tm_local.tm_min, tm_local.tm_sec);
        std::string date_str = std::format("{:02d}/{:02d}/{:04d}", tm_local.tm_mday, tm_local.tm_mon + 1, tm_local.tm_year + 1900);

        ImVec4 accent_color = colors[0];
        if (current_weather && !current_weather->weather.empty()) {
            std::string weather_main = current_weather->weather[0].main;
            if (weather_main == "Clear") accent_color = ImVec4(1.0f, 0.7f, 0.2f, 1.0f);
            else if (weather_main == "Clouds") accent_color = ImVec4(0.55f, 0.65f, 0.75f, 1.0f);
            else if (weather_main == "Rain" || weather_main == "Drizzle") accent_color = ImVec4(0.2f, 0.7f, 1.0f, 1.0f);
            else if (weather_main == "Thunderstorm") accent_color = ImVec4(0.6f, 0.4f, 1.0f, 1.0f);
            else if (weather_main == "Snow") accent_color = ImVec4(0.85f, 0.95f, 1.0f, 1.0f);
        }

        // Draw Clock and Digital readout side-by-side
        ImGui::BeginGroup();
        ImVec2 clock_box_size(88.0f, 88.0f);
        ImGui::Dummy(clock_box_size);
        ImVec2 box_min = ImGui::GetItemRectMin();
        ImVec2 clock_center = ImVec2(box_min.x + clock_box_size.x * 0.5f, box_min.y + clock_box_size.y * 0.5f);

        draw_analog_clock(ImGui::GetWindowDrawList(), clock_center, 38.0f, now_tp, accent_color);
        ImGui::EndGroup();

        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Spacing();
        // Display time in large font
        ImGui::SetWindowFontScale(1.6f);
        ImGui::TextColored(accent_color, "%s", time_str.c_str());
        ImGui::SetWindowFontScale(1.0f);

        ImGui::TextColored(colors[5], "%s", date_str.c_str());
        if (current_weather && !current_weather->name.empty()) {
            int tz_hours = current_weather->timezone / 3600;
            ImGui::TextColored(colors[5], "%s (UTC%+d)", current_weather->name.c_str(), tz_hours);
        }
        ImGui::EndGroup();
    }
    
    // Helper function to get weather icon based on weather condition
    inline auto get_weather_icon(const std::string& weather_main) -> const char* {
        if (weather_main == "Clear") return ICON_MD_WB_SUNNY;
        if (weather_main == "Clouds") return ICON_MD_WB_CLOUDY;
        if (weather_main == "Rain") return ICON_MD_UMBRELLA;
        if (weather_main == "Drizzle") return ICON_MD_GRAIN;
        if (weather_main == "Thunderstorm") return ICON_MD_FLASH_ON;
        if (weather_main == "Snow") return ICON_MD_AC_UNIT;
        if (weather_main == "Mist" || weather_main == "Fog") return ICON_MD_VISIBILITY;
        if (weather_main == "Haze") return ICON_MD_BLUR_ON;
        return ICON_MD_HELP_OUTLINE; // Default icon for unknown conditions
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
            
            // Display temperature with color coding based on temperature ranges
            auto temp_color = colors[0]; // Default blue color
            if (current_weather->main.temp >= 25.0) {
                temp_color = ImVec4(1.0f, 0.4f, 0.2f, 1.0f); // Hot - orange/red
            } else if (current_weather->main.temp >= 15.0) {
                temp_color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f); // Warm - yellow
            } else if (current_weather->main.temp <= 0.0) {
                temp_color = ImVec4(0.6f, 0.8f, 1.0f, 1.0f); // Cold - light blue
            }
            
            ImGui::TextColored(temp_color, "%.1f°C", current_weather->main.temp);
            ImGui::SameLine();
            ImGui::TextColored(colors[5], "(Feels like: %.1f°C)", current_weather->main.feels_like);
            
            if (!current_weather->weather.empty()) {
                std::string description = current_weather->weather[0].description;
                std::string weather_main = current_weather->weather[0].main;
                
                // Capitalize the weather description
                if (!description.empty()) {
                    description[0] = static_cast<char>(std::toupper(description[0]));
                }
                
                // Display weather icon with description
                const char* weather_icon = get_weather_icon(weather_main);
                ImGui::TextColored(colors[3], "%s", weather_icon);
                ImGui::SameLine();
                ImGui::Text("%s", description.c_str());
            }
            
            // More weather details
            ImGui::Spacing();
            ImGui::Columns(2, "weather_details", false);
            
            // Column 1 - Enhanced with color coding
            // Humidity with color based on level
            auto humidity_color = colors[0]; // Default blue
            if (current_weather->main.humidity > 80) {
                humidity_color = ImVec4(0.2f, 0.8f, 1.0f, 1.0f); // High humidity - cyan
            } else if (current_weather->main.humidity < 30) {
                humidity_color = ImVec4(1.0f, 0.6f, 0.2f, 1.0f); // Low humidity - orange
            } else {
                humidity_color = ImVec4(0.2f, 1.0f, 0.4f, 1.0f); // Normal humidity - green
            }
            
            ImGui::TextColored(humidity_color, "Humidity: %d%%", current_weather->main.humidity);
            ImGui::TextColored(colors[5], "Pressure: %d hPa", static_cast<int>(current_weather->main.pressure));
            
            // Column 2 - Enhanced with color coding
            ImGui::NextColumn();
            
            // Wind speed with color based on intensity
            auto wind_color = colors[0]; // Default blue
            if (current_weather->wind.speed > 10.0) {
                wind_color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); // Strong wind - red
            } else if (current_weather->wind.speed > 5.0) {
                wind_color = ImVec4(1.0f, 0.7f, 0.2f, 1.0f); // Moderate wind - orange
            } else {
                wind_color = ImVec4(0.4f, 1.0f, 0.4f, 1.0f); // Light wind - green
            }
            
            ImGui::TextColored(wind_color, "Wind: %.1f m/s", current_weather->wind.speed);
            
            // Cloud coverage with color
            auto cloud_color = colors[5]; // Default gray
            if (current_weather->clouds.all > 80) {
                cloud_color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f); // Very cloudy - dark gray
            } else if (current_weather->clouds.all > 50) {
                cloud_color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f); // Partly cloudy - light gray
            } else {
                cloud_color = ImVec4(0.7f, 0.9f, 1.0f, 1.0f); // Clear - light blue
            }
            
            ImGui::TextColored(cloud_color, "Clouds: %d%%", current_weather->clouds.all);
            
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
            
            // Only show the forecast periods - cast to same type to avoid template deduction issues
            const size_t forecast_items_to_show = std::min(static_cast<size_t>(5), forecast->list.size());
            
            // Start a table
            if (ImGui::BeginTable("forecast_table", 3, ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("Time");
                ImGui::TableSetupColumn("Temp");
                ImGui::TableSetupColumn("Condition");
                ImGui::TableHeadersRow();
                
                for (const auto& item : forecast->list | std::views::take(forecast_items_to_show)) {
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
        
        if (!initialized_) {
            std::string current_location = weather_host->getLocation();
            auto len = std::min(current_location.size(), sizeof(location_buffer_) - 1);
            strncpy(location_buffer_, current_location.c_str(), len + 1);
            initialized_ = true;
        }
        
        ImGui::Separator();
        ImGui::TextColored(colors[2], "Location:");
        
        // Use a table to align input and button within the available width
        if (ImGui::BeginTable("location_input_table", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableNextColumn();
            ImGui::PushItemWidth(-FLT_MIN); // Use all available width in the cell
            if (ImGui::InputText("##location", location_buffer_, sizeof(location_buffer_),
                     ImGuiInputTextFlags_EnterReturnsTrue)) {
            setLocation(location_buffer_);
            }
            ImGui::PopItemWidth();

            ImGui::TableNextColumn();
            if (ImGui::Button("Update")) {
            setLocation(location_buffer_);
            }
            ImGui::EndTable();
        }
        
        ImGui::TextColored(colors[5], "Format: City,CountryCode (e.g., London,uk)");

        ImGui::Spacing();
        ImGui::TextColored(colors[2], "%s Video Cast Position:", ICON_MD_SETTINGS);
        const char* pos_options[] = { "Top Left (Default)", "Top Right", "Bottom Left", "Bottom Right" };
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::Combo("##WeatherCastPos", &cast_position, pos_options, IM_ARRAYSIZE(pos_options))) {
            save_settings();
        }
    }
    
private:
    void save_settings() {
        try {
            auto config_dir = rouen::platform::get_user_config_directory();
            std::filesystem::create_directories(config_dir);
            auto config_path = config_dir / "weather_settings.json";
            
            glz::json_t json;
            json["cast_position"] = static_cast<double>(cast_position);
            
            std::ofstream file(config_path);
            if (file.is_open()) {
                std::string json_str;
                auto result = glz::write_json(json, json_str);
                if (!result) {
                    file << json_str;
                }
                file.close();
            }
        } catch (...) {}
    }

    void load_settings() {
        try {
            auto config_path = rouen::platform::get_user_config_directory() / "weather_settings.json";
            if (!std::filesystem::exists(config_path)) return;
            std::ifstream file(config_path);
            if (file.is_open()) {
                std::string json_str((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                glz::json_t json;
                auto ec = glz::read_json(json, json_str);
                if (!ec && json.contains("cast_position")) {
                    cast_position = static_cast<int>(json["cast_position"].get<double>());
                }
                file.close();
            }
        } catch (...) {}
    }

    void setLocation(std::string_view location) {
        weather_host->setLocation(location);
        weather_host->refreshWeather();
        auto current_weather = weather_host->getCurrentWeather();
        if (current_weather) {
            auto location_name = current_weather->name;
            std::string country = current_weather->sys.country;
            if (!country.empty()) {
                location_name += ", " + country;
            }
            name(std::format("Weather in {}", location_name));
        }
    }

    std::shared_ptr<hosts::WeatherHost> weather_host;
    bool initialized_{false};
    char location_buffer_[64]{};
    int cast_position{0};
};

} // namespace rouen::cards
