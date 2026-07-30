#include "converter.hpp"
#include <array>
#include <cctype>
#include <cstddef>
#include <exception>
#include <format>
#include <imgui.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

// Implementation of converter class

namespace rouen::cards {

// Constructor
converter::converter() {
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
    categories_.emplace_back(std::make_unique<weight_category>());
    categories_.emplace_back(std::make_unique<volume_category>());
    categories_.emplace_back(std::make_unique<temperature_category>());
    categories_.emplace_back(std::make_unique<time_category>());
    categories_.emplace_back(std::make_unique<speed_category>());
    categories_.emplace_back(std::make_unique<energy_category>());
    categories_.emplace_back(std::make_unique<encoding_category>());
    
    // Initialize with first category
    if (!categories_.empty()) {
        current_category_index_ = 0;
        reset_unit_selections();
    }
}

bool converter::render() {
    return render_window([this]() {
        render_category_selector();
        ImGui::Spacing();
        
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

std::string converter::get_uri() const {
    return "converter";
}

void converter::render_category_selector() {
    // Category selection with better layout
    ImGui::Text("Conversion Category:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    
    if (ImGui::BeginCombo("##category", std::string(categories_[current_category_index_]->name()).c_str())) {
        for (size_t i = 0; i < categories_.size(); ++i) {
            const bool is_selected = (current_category_index_ == i);
            if (ImGui::Selectable(std::string(categories_[i]->name()).c_str(), is_selected)) {
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

void converter::render_numeric_converter() {
    const auto& category = categories_[current_category_index_];
    const auto& units = category->units();
    
    if (units.size() < 2) return;

    // Create two columns for better layout
    ImGui::Columns(2, "converter_columns", false);
    ImGui::SetColumnWidth(0, 240.0f);
    
    // Left column - Input section
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
    ImGui::BeginChild("input_section", ImVec2(0, 200), true, ImGuiWindowFlags_NoScrollbar);
    
    ImGui::PushStyleColor(ImGuiCol_Text, colors[2]);
    ImGui::Text("Input");
    ImGui::PopStyleColor();
    ImGui::Separator();
    
    // From unit selector
    ImGui::Text("From Unit:");
    ImGui::SetNextItemWidth(-1);
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
    
    ImGui::Spacing();
    
    // Input value
    ImGui::Text("Value:");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##numeric_input", numeric_input_.data(), numeric_input_.size(), 
                        ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_CallbackEdit,
                        [](ImGuiInputTextCallbackData* data) -> int {
                            auto* converter_ptr = static_cast<converter*>(data->UserData);
                            converter_ptr->perform_numeric_conversion();
                            return 0;
                        }, this)) {
        perform_numeric_conversion();
    }
    
    ImGui::EndChild();
    ImGui::PopStyleColor();
    
    ImGui::NextColumn();
    
    // Right column - Output section
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
    ImGui::BeginChild("output_section", ImVec2(0, 200), true, ImGuiWindowFlags_NoScrollbar);
    
    ImGui::PushStyleColor(ImGuiCol_Text, colors[2]);
    ImGui::Text("Output");
    ImGui::PopStyleColor();
    ImGui::Separator();
    
    // To unit selector
    ImGui::Text("To Unit:");
    ImGui::SetNextItemWidth(-1);
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
    
    ImGui::Spacing();
    
    // Result display
    ImGui::Text("Result:");
    ImGui::SetNextItemWidth(-1);
    if (!conversion_result_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, colors[3]);
        ImGui::InputText("##result_display", const_cast<char*>(conversion_result_.c_str()), 
                       conversion_result_.size() + 1, ImGuiInputTextFlags_ReadOnly);
        ImGui::PopStyleColor();
        
        // Copy button
        ImGui::SameLine();
        if (ImGui::Button("Copy")) {
            ImGui::SetClipboardText(conversion_result_.c_str());
        }
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Enter a value to convert");
    }
    
    ImGui::EndChild();
    ImGui::PopStyleColor();
    
    ImGui::Columns(1);
    
    // Reverse units button
    ImGui::Spacing();
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 120.0f) * 0.5f); // Center the button
    if (ImGui::Button("🔄 Reverse Units", ImVec2(120.0f, 0))) {
        reverse_units();
    }
    
    // Error display below columns
    if (!error_message_.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.3f, 0.1f, 0.1f, 0.8f));
        ImGui::BeginChild("error_section", ImVec2(0, 60), true, ImGuiWindowFlags_NoScrollbar);
        
        ImGui::PushStyleColor(ImGuiCol_Text, colors[4]);
        ImGui::Text("⚠ Error");
        ImGui::PopStyleColor();
        ImGui::Separator();
        
        ImGui::TextWrapped("%s", error_message_.c_str());
        
        if (ImGui::Button("Clear Error")) {
            error_message_.clear();
        }
        
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
}

void converter::render_encoding_converter() {
    const auto& units = categories_[current_category_index_]->units();
    
    // Create two columns for better layout
    ImGui::Columns(2, "encoding_columns", false);
    ImGui::SetColumnWidth(0, 240.0f);
    
    // Left column - Input section
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
    ImGui::BeginChild("encoding_input_section", ImVec2(0, 300), true);
    
    ImGui::PushStyleColor(ImGuiCol_Text, colors[2]);
    ImGui::Text("Input");
    ImGui::PopStyleColor();
    ImGui::Separator();
    
    // From format selector
    ImGui::Text("From Format:");
    ImGui::SetNextItemWidth(-1);
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
    
    ImGui::Spacing();
    
    // Text input
    ImGui::Text("Text:");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextMultiline("##text_input", text_input_.data(), text_input_.size(),
                                 ImVec2(-1, 200), ImGuiInputTextFlags_CallbackEdit,
                                 [](ImGuiInputTextCallbackData* data) -> int {
                                     auto* converter_ptr = static_cast<converter*>(data->UserData);
                                     converter_ptr->perform_encoding_conversion();
                                     return 0;
                                 }, this)) {
        perform_encoding_conversion();
    }
    
    ImGui::EndChild();
    ImGui::PopStyleColor();
    
    ImGui::NextColumn();
    
    // Right column - Output section
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
    ImGui::BeginChild("encoding_output_section", ImVec2(0, 300), true);
    
    ImGui::PushStyleColor(ImGuiCol_Text, colors[2]);
    ImGui::Text("Output");
    ImGui::PopStyleColor();
    ImGui::Separator();
    
    // To format selector
    ImGui::Text("To Format:");
    ImGui::SetNextItemWidth(-1);
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
    
    ImGui::Spacing();
    
    // Result display
    ImGui::Text("Result:");
    ImGui::SetNextItemWidth(-1);
    if (!conversion_result_.empty()) {
        ImGui::InputTextMultiline("##encoding_result", const_cast<char*>(conversion_result_.c_str()), 
                                 conversion_result_.size() + 1, ImVec2(-1, 180), 
                                 ImGuiInputTextFlags_ReadOnly);
        
        // Copy button
        if (ImGui::Button("Copy Result")) {
            ImGui::SetClipboardText(conversion_result_.c_str());
        }
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Enter text to convert");
    }
    
    ImGui::EndChild();
    ImGui::PopStyleColor();
    
    ImGui::Columns(1);
    
    // Reverse units button
    ImGui::Spacing();
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 120.0f) * 0.5f); // Center the button
    if (ImGui::Button("🔄 Reverse Units", ImVec2(120.0f, 0))) {
        reverse_units();
    }
    
    // Error display below columns
    if (!error_message_.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, colors[4]);
        ImGui::Text("Error: %s", error_message_.c_str());
        ImGui::PopStyleColor();
    }
}

void converter::perform_numeric_conversion() {
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
        double const input_value = std::stod(std::string(numeric_input_.data()));
        
        // Convert to base unit, then to target unit
        const auto& from_unit = units[from_unit_index_];
        const auto& to_unit = units[to_unit_index_];
        
        double const base_value = from_unit.to_base(input_value);
        double const result_value = to_unit.from_base(base_value);
        
        conversion_result_ = category->format_result(result_value, to_unit);
        
    } catch (const std::invalid_argument&) {
        error_message_ = "Invalid number format";
    } catch (const std::out_of_range&) {
        error_message_ = "Number out of range";
    } catch (const std::exception& e) {
        error_message_ = std::format("Conversion error: {}", e.what());
    }
}

void converter::perform_encoding_conversion() {
    error_message_.clear();
    conversion_result_.clear();
    
    try {
        std::string const input(text_input_.data());
        
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
std::string converter::encode_base64(const std::string& input) {
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

std::string converter::decode_base64(const std::string& input) {
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
    
    for (char const c : input) {
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

std::string converter::encode_hex(const std::string& input) {
    std::string result;
    for (auto c : input) {
        result += std::format("{:02x}", c);
    }
    return result;
}

std::string converter::decode_hex(const std::string& input) {
    if (input.length() % 2 != 0) {
        throw std::invalid_argument("Invalid hex string length");
    }
    
    std::string result;
    for (size_t i = 0; i < input.length(); i += 2) {
        std::string const byte = input.substr(i, 2);
        auto chr = static_cast<char>(std::stoi(byte, nullptr, 16));
        result.push_back(chr);
    }
    return result;
}

std::string converter::encode_url(const std::string& input) {
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

std::string converter::decode_url(const std::string& input) {
    std::string result;
    for (size_t i = 0; i < input.length(); ++i) {
        if (input[i] == '%' && i + 2 < input.length()) {
            std::string const hex = input.substr(i + 1, 2);
            auto chr = static_cast<char>(std::stoi(hex, nullptr, 16));
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

void converter::reset_unit_selections() {
    from_unit_index_ = 0;
    to_unit_index_ = categories_[current_category_index_]->units().size() > 1 ? 1 : 0;
}

void converter::clear_results() {
    conversion_result_.clear();
    error_message_.clear();
}

void converter::reverse_units() {
    std::swap(from_unit_index_, to_unit_index_);
    
    // Re-perform conversion with swapped units
    if (categories_[current_category_index_]->name() == "Encoding") {
        perform_encoding_conversion();
    } else {
        perform_numeric_conversion();
    }
}

} // namespace rouen::cards
