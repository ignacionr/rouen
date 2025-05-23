#pragma once

// 1. Standard includes in alphabetic order
#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <format>
#include <functional>
#include <memory>
#include <numbers>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

// 2. Libraries used in the project, in alphabetic order
#include "../../helpers/imgui_include.hpp"

// 3. All other includes
#include "../interface/card.hpp"

namespace rouen::cards {

// C++23 concept for converter functions
template<typename T>
concept ConversionFunction = requires(T t, double value) {
    { t(value) } -> std::convertible_to<double>;
};

// Base class for conversion categories
class conversion_category {
public:
    struct unit_info {
        std::string name;
        std::string symbol;
        std::function<double(double)> to_base;
        std::function<double(double)> from_base;
    };

    virtual ~conversion_category() = default;
    virtual std::string_view name() const = 0;
    virtual const std::vector<unit_info>& units() const = 0;
    virtual std::string format_result(double value, const unit_info& unit) const = 0;
};

// Length conversion category
class length_category final : public conversion_category {
private:
    static inline const std::vector<unit_info> units_ = {
        {"Meters", "m", [](double v) { return v; }, [](double v) { return v; }},
        {"Kilometers", "km", [](double v) { return v * 1000.0; }, [](double v) { return v / 1000.0; }},
        {"Centimeters", "cm", [](double v) { return v / 100.0; }, [](double v) { return v * 100.0; }},
        {"Millimeters", "mm", [](double v) { return v / 1000.0; }, [](double v) { return v * 1000.0; }},
        {"Inches", "in", [](double v) { return v * 0.0254; }, [](double v) { return v / 0.0254; }},
        {"Feet", "ft", [](double v) { return v * 0.3048; }, [](double v) { return v / 0.3048; }},
        {"Yards", "yd", [](double v) { return v * 0.9144; }, [](double v) { return v / 0.9144; }},
        {"Miles", "mi", [](double v) { return v * 1609.344; }, [](double v) { return v / 1609.344; }}
    };

public:
    std::string_view name() const override { return "Length"; }
    const std::vector<unit_info>& units() const override { return units_; }
    
    std::string format_result(double value, const unit_info& unit) const override {
        return std::format("{:.6g} {}", value, unit.symbol);
    }
};

// Area conversion category
class area_category final : public conversion_category {
private:
    static inline const std::vector<unit_info> units_ = {
        {"Square Meters", "m²", [](double v) { return v; }, [](double v) { return v; }},
        {"Square Kilometers", "km²", [](double v) { return v * 1000000.0; }, [](double v) { return v / 1000000.0; }},
        {"Hectares", "ha", [](double v) { return v * 10000.0; }, [](double v) { return v / 10000.0; }},
        {"Acres", "ac", [](double v) { return v * 4046.856; }, [](double v) { return v / 4046.856; }},
        {"Square Feet", "ft²", [](double v) { return v * 0.092903; }, [](double v) { return v / 0.092903; }},
        {"Square Inches", "in²", [](double v) { return v * 0.00064516; }, [](double v) { return v / 0.00064516; }}
    };

public:
    std::string_view name() const override { return "Area"; }
    const std::vector<unit_info>& units() const override { return units_; }
    
    std::string format_result(double value, const unit_info& unit) const override {
        return std::format("{:.6g} {}", value, unit.symbol);
    }
};

// Temperature conversion category
class temperature_category final : public conversion_category {
private:
    static inline const std::vector<unit_info> units_ = {
        {"Celsius", "°C", 
            [](double v) { return v; }, 
            [](double v) { return v; }},
        {"Fahrenheit", "°F", 
            [](double v) { return (v - 32.0) * 5.0 / 9.0; }, 
            [](double v) { return v * 9.0 / 5.0 + 32.0; }},
        {"Kelvin", "K", 
            [](double v) { return v - 273.15; }, 
            [](double v) { return v + 273.15; }},
        {"Rankine", "°R", 
            [](double v) { return (v - 491.67) * 5.0 / 9.0; }, 
            [](double v) { return v * 9.0 / 5.0 + 491.67; }}
    };

public:
    std::string_view name() const override { return "Temperature"; }
    const std::vector<unit_info>& units() const override { return units_; }
    
    std::string format_result(double value, const unit_info& unit) const override {
        return std::format("{:.2f} {}", value, unit.symbol);
    }
};

// Data encoding conversion category  
class encoding_category final : public conversion_category {
private:
    static inline const std::vector<unit_info> units_ = {
        {"Text", "txt", 
            [](double) { return 0.0; }, // Placeholder - actual conversion handled differently
            [](double) { return 0.0; }},
        {"Base64", "b64", 
            [](double) { return 0.0; }, 
            [](double) { return 0.0; }},
        {"Hexadecimal", "hex", 
            [](double) { return 0.0; }, 
            [](double) { return 0.0; }},
        {"URL Encoded", "url", 
            [](double) { return 0.0; }, 
            [](double) { return 0.0; }}
    };

public:
    std::string_view name() const override { return "Encoding"; }
    const std::vector<unit_info>& units() const override { return units_; }
    
    std::string format_result(double, const unit_info&) const override {
        return ""; // Handled separately for text conversions
    }
};

// Main converter card
class converter final : public card {
public:
    converter() {
        // Set custom colors
        colors[0] = {0.2f, 0.6f, 0.4f, 1.0f}; // Green primary color
        colors[1] = {0.3f, 0.7f, 0.5f, 0.7f}; // Light green secondary color
        
        // Additional colors for specific elements
        get_color(2, ImVec4(0.4f, 0.8f, 0.6f, 1.0f)); // Light green for titles
        get_color(3, ImVec4(0.2f, 0.8f, 0.2f, 1.0f)); // Bright green for success
        get_color(4, ImVec4(0.8f, 0.4f, 0.4f, 1.0f)); // Red for errors
        get_color(5, ImVec4(0.6f, 0.6f, 0.6f, 1.0f)); // Gray for disabled/secondary text
        
        name("Unit Converter");
        width = 500.0f;
        requested_fps = 2; // Lower refresh rate for static content
        
        // Initialize conversion categories
        categories_.emplace_back(std::make_unique<length_category>());
        categories_.emplace_back(std::make_unique<area_category>());
        categories_.emplace_back(std::make_unique<temperature_category>());
        categories_.emplace_back(std::make_unique<encoding_category>());
        
        // Initialize with first category
        if (!categories_.empty()) {
            current_category_index_ = 0;
            reset_unit_selections();
        }
    }

    bool render() override {
        return render_window([this]() {
            render_category_selector();
            ImGui::Separator();
            
            if (current_category_index_ < categories_.size()) {
                const auto& category = categories_[current_category_index_];
                
                if (category->name() == "Encoding") {
                    render_encoding_converter();
                } else {
                    render_numeric_converter();
                }
            }
        });
    }

    std::string get_uri() const override {
        return "converter";
    }

private:
    std::vector<std::unique_ptr<conversion_category>> categories_;
    size_t current_category_index_ = 0;
    size_t from_unit_index_ = 0;
    size_t to_unit_index_ = 1;
    
    // Input buffers
    std::array<char, 256> numeric_input_{"1.0"};
    std::array<char, 1024> text_input_{"Hello, World!"};
    std::string conversion_result_;
    std::string error_message_;

    void render_category_selector() {
        ImGui::Text("Category:");
        ImGui::SameLine();
        
        if (ImGui::BeginCombo("##category", categories_[current_category_index_]->name().data())) {
            for (size_t i = 0; i < categories_.size(); ++i) {
                const bool is_selected = (current_category_index_ == i);
                if (ImGui::Selectable(categories_[i]->name().data(), is_selected)) {
                    if (current_category_index_ != i) {
                        current_category_index_ = i;
                        reset_unit_selections();
                        clear_results();
                    }
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    void render_numeric_converter() {
        const auto& category = categories_[current_category_index_];
        const auto& units = category->units();
        
        if (units.size() < 2) return;

        // From unit selector
        ImGui::Text("From:");
        ImGui::SameLine();
        if (ImGui::BeginCombo("##from_unit", units[from_unit_index_].name.c_str())) {
            for (size_t i = 0; i < units.size(); ++i) {
                const bool is_selected = (from_unit_index_ == i);
                if (ImGui::Selectable(units[i].name.c_str(), is_selected)) {
                    from_unit_index_ = i;
                    perform_numeric_conversion();
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        // To unit selector
        ImGui::Text("To:");
        ImGui::SameLine();
        if (ImGui::BeginCombo("##to_unit", units[to_unit_index_].name.c_str())) {
            for (size_t i = 0; i < units.size(); ++i) {
                const bool is_selected = (to_unit_index_ == i);
                if (ImGui::Selectable(units[i].name.c_str(), is_selected)) {
                    to_unit_index_ = i;
                    perform_numeric_conversion();
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Separator();

        // Input field
        ImGui::Text("Value:");
        ImGui::SameLine();
        if (ImGui::InputText("##numeric_input", numeric_input_.data(), numeric_input_.size(), 
                            ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_CallbackEdit,
                            [](ImGuiInputTextCallbackData* data) -> int {
                                auto* converter_ptr = static_cast<converter*>(data->UserData);
                                converter_ptr->perform_numeric_conversion();
                                return 0;
                            }, this)) {
            perform_numeric_conversion();
        }

        // Results
        ImGui::Separator();
        if (!error_message_.empty()) {
            ImGui::TextColored(colors[4], "Error: %s", error_message_.c_str());
        } else if (!conversion_result_.empty()) {
            ImGui::TextColored(colors[3], "Result: %s", conversion_result_.c_str());
        }
    }

    void render_encoding_converter() {
        const auto& units = categories_[current_category_index_]->units();
        
        // From format selector
        ImGui::Text("From:");
        ImGui::SameLine();
        if (ImGui::BeginCombo("##from_encoding", units[from_unit_index_].name.c_str())) {
            for (size_t i = 0; i < units.size(); ++i) {
                const bool is_selected = (from_unit_index_ == i);
                if (ImGui::Selectable(units[i].name.c_str(), is_selected)) {
                    from_unit_index_ = i;
                    perform_encoding_conversion();
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        // To format selector
        ImGui::Text("To:");
        ImGui::SameLine();
        if (ImGui::BeginCombo("##to_encoding", units[to_unit_index_].name.c_str())) {
            for (size_t i = 0; i < units.size(); ++i) {
                const bool is_selected = (to_unit_index_ == i);
                if (ImGui::Selectable(units[i].name.c_str(), is_selected)) {
                    to_unit_index_ = i;
                    perform_encoding_conversion();
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Separator();

        // Text input
        ImGui::Text("Input:");
        if (ImGui::InputTextMultiline("##text_input", text_input_.data(), text_input_.size(),
                                     ImVec2(-1, 100), ImGuiInputTextFlags_CallbackEdit,
                                     [](ImGuiInputTextCallbackData* data) -> int {
                                         auto* converter_ptr = static_cast<converter*>(data->UserData);
                                         converter_ptr->perform_encoding_conversion();
                                         return 0;
                                     }, this)) {
            perform_encoding_conversion();
        }

        // Results
        ImGui::Separator();
        if (!error_message_.empty()) {
            ImGui::TextColored(colors[4], "Error: %s", error_message_.c_str());
        } else if (!conversion_result_.empty()) {
            ImGui::Text("Result:");
            ImGui::InputTextMultiline("##result", const_cast<char*>(conversion_result_.c_str()), 
                                     conversion_result_.size() + 1, ImVec2(-1, 100), 
                                     ImGuiInputTextFlags_ReadOnly);
        }
    }

    void perform_numeric_conversion() {
        error_message_.clear();
        conversion_result_.clear();
        
        try {
            const auto& category = categories_[current_category_index_];
            const auto& units = category->units();
            
            if (from_unit_index_ >= units.size() || to_unit_index_ >= units.size()) {
                error_message_ = "Invalid unit selection";
                return;
            }

            // Parse input value
            double input_value = std::stod(std::string(numeric_input_.data()));
            
            // Convert to base unit, then to target unit
            const auto& from_unit = units[from_unit_index_];
            const auto& to_unit = units[to_unit_index_];
            
            double base_value = from_unit.to_base(input_value);
            double result_value = to_unit.from_base(base_value);
            
            conversion_result_ = category->format_result(result_value, to_unit);
            
        } catch (const std::invalid_argument&) {
            error_message_ = "Invalid number format";
        } catch (const std::out_of_range&) {
            error_message_ = "Number out of range";
        } catch (const std::exception& e) {
            error_message_ = std::format("Conversion error: {}", e.what());
        }
    }

    void perform_encoding_conversion() {
        error_message_.clear();
        conversion_result_.clear();
        
        try {
            std::string input(text_input_.data());
            
            if (input.empty()) {
                conversion_result_ = "";
                return;
            }

            // Determine conversion path
            if (from_unit_index_ == 0 && to_unit_index_ == 1) { // Text to Base64
                conversion_result_ = encode_base64(input);
            } else if (from_unit_index_ == 1 && to_unit_index_ == 0) { // Base64 to Text
                conversion_result_ = decode_base64(input);
            } else if (from_unit_index_ == 0 && to_unit_index_ == 2) { // Text to Hex
                conversion_result_ = encode_hex(input);
            } else if (from_unit_index_ == 2 && to_unit_index_ == 0) { // Hex to Text
                conversion_result_ = decode_hex(input);
            } else if (from_unit_index_ == 0 && to_unit_index_ == 3) { // Text to URL
                conversion_result_ = encode_url(input);
            } else if (from_unit_index_ == 3 && to_unit_index_ == 0) { // URL to Text
                conversion_result_ = decode_url(input);
            } else if (from_unit_index_ == to_unit_index_) { // Same format
                conversion_result_ = input;
            } else {
                error_message_ = "Direct conversion not supported. Convert through Text format.";
            }
            
        } catch (const std::exception& e) {
            error_message_ = std::format("Encoding error: {}", e.what());
        }
    }

    // Encoding helper functions
    std::string encode_base64(const std::string& input) {
        static constexpr std::string_view chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string result;
        
        int val = 0, valb = -6;
        for (auto c : input) {
            val = (val << 8) + static_cast<int>(static_cast<unsigned char>(c));
            valb += 8;
            while (valb >= 0) {
                result.push_back(chars[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }
        if (valb > -6) {
            result.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);
        }
        while (result.size() % 4) {
            result.push_back('=');
        }
        return result;
    }

    std::string decode_base64(const std::string& input) {
        static constexpr std::array<int, 128> lookup = []() {
            std::array<int, 128> arr{};
            arr.fill(-1);
            for (int i = 0; i < 26; ++i) arr[static_cast<std::size_t>('A') + static_cast<std::size_t>(i)] = i;
            for (int i = 0; i < 26; ++i) arr[static_cast<std::size_t>('a') + static_cast<std::size_t>(i)] = i + 26;
            for (int i = 0; i < 10; ++i) arr[static_cast<std::size_t>('0') + static_cast<std::size_t>(i)] = i + 52;
            arr[static_cast<std::size_t>('+')] = 62; arr[static_cast<std::size_t>('/')] = 63;
            return arr;
        }();
        
        std::string result;
        int val = 0, valb = -8;
        
        for (char c : input) {
            if (lookup[static_cast<unsigned char>(c)] == -1) break;
            val = (val << 6) + lookup[static_cast<unsigned char>(c)];
            valb += 6;
            if (valb >= 0) {
                result.push_back(char((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
        return result;
    }

    std::string encode_hex(const std::string& input) {
        std::string result;
        for (auto c : input) {
            result += std::format("{:02x}", c);
        }
        return result;
    }

    std::string decode_hex(const std::string& input) {
        if (input.length() % 2 != 0) {
            throw std::invalid_argument("Invalid hex string length");
        }
        
        std::string result;
        for (size_t i = 0; i < input.length(); i += 2) {
            std::string byte = input.substr(i, 2);
            char chr = static_cast<char>(std::stoi(byte, nullptr, 16));
            result.push_back(chr);
        }
        return result;
    }

    std::string encode_url(const std::string& input) {
        std::string result;
        for (char c : input) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                result += c;
            } else {
                result += std::format("%{:02X}", static_cast<unsigned char>(c));
            }
        }
        return result;
    }

    std::string decode_url(const std::string& input) {
        std::string result;
        for (size_t i = 0; i < input.length(); ++i) {
            if (input[i] == '%' && i + 2 < input.length()) {
                std::string hex = input.substr(i + 1, 2);
                char chr = static_cast<char>(std::stoi(hex, nullptr, 16));
                result.push_back(chr);
                i += 2;
            } else if (input[i] == '+') {
                result.push_back(' ');
            } else {
                result.push_back(input[i]);
            }
        }
        return result;
    }

    void reset_unit_selections() {
        from_unit_index_ = 0;
        to_unit_index_ = categories_[current_category_index_]->units().size() > 1 ? 1 : 0;
    }

    void clear_results() {
        conversion_result_.clear();
        error_message_.clear();
    }
};

} // namespace rouen::cards
