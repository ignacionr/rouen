#include "converter.hpp"
#include <array>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <exception>
#include <format>
#include <imgui.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include "../../../external/IconsMaterialDesign.h"
#include "../../fonts.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/imgui_ui_context.hpp"
#include "../../helpers/platform_utils.hpp"

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
        perform_numeric_conversion();
    }
}

bool converter::render() {
    rouen::ui::imgui_ui_context_impl ui;
    return render(ui);
}

bool converter::render(rouen::ui::ui_context& ui) {
    return render_window([this, &ui]() {
        // Presentation mode selector
        ui.checkbox("Adaptive Card View", &adaptive_view_mode);
        ui.separator();

        if (adaptive_view_mode) {
            auto now = std::chrono::steady_clock::now();
            auto parse_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_adaptive_parse_time_).count();
            if (parse_elapsed >= 500 || adaptive_bound_.body.empty()) {
                try {
                    adaptive_bound_ = adaptive_parser_.parse(get_adaptive_card_json());
                    adaptive_error_.clear();
                } catch (const std::exception& e) {
                    adaptive_error_ = e.what();
                }
                last_adaptive_parse_time_ = now;
            }

            if (!adaptive_error_.empty()) {
                ui.text_colored(ImVec4{1.0f, 0.4f, 0.4f, 1.0f}, adaptive_error_);
            } else {
                adaptive_renderer_.render(
                    adaptive_bound_, adaptive_input_state_,
                    helpers::adaptive_cards::renderer::action_callbacks{
                        .open_url = [](const std::string& url) {
                            static_cast<void>(rouen::platform::open_url(url));
                        },
                        .on_submit = [this](const std::string& payload) {
                            handle_action(payload);
                        }
                    },
                    helpers::adaptive_cards::render_config{
                        .font_bold   = rouen::fonts::get_font(rouen::fonts::FontType::Bold),
                        .font_italic = rouen::fonts::get_font(rouen::fonts::FontType::Italic),
                        .font_code   = rouen::fonts::get_font(rouen::fonts::FontType::Mono)
                    }
                );
            }
            return;
        }

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

std::string converter::get_category() const {
    if (current_category_index_ < categories_.size()) {
        return std::string(categories_[current_category_index_]->name());
    }
    return {};
}

std::string converter::get_from_unit() const {
    if (current_category_index_ < categories_.size()) {
        const auto& units = categories_[current_category_index_]->units();
        if (from_unit_index_ < units.size()) {
            return units[from_unit_index_].name;
        }
    }
    return {};
}

std::string converter::get_to_unit() const {
    if (current_category_index_ < categories_.size()) {
        const auto& units = categories_[current_category_index_]->units();
        if (to_unit_index_ < units.size()) {
            return units[to_unit_index_].name;
        }
    }
    return {};
}

double converter::get_input_value() const {
    try {
        return std::stod(std::string(numeric_input_.data()));
    } catch (...) {
        return 0.0;
    }
}

std::string converter::get_adaptive_card_json() const {
    glz::json_t card;
    card["type"] = "AdaptiveCard";
    card["version"] = "1.5";
    card["$schema"] = "http://adaptivecards.io/schemas/adaptive-card.json";

    std::vector<glz::json_t> body;

    // 1. Header Container (emphasis)
    glz::json_t header;
    header["type"] = "Container";
    header["style"] = "emphasis";

    glz::json_t header_cols;
    header_cols["type"] = "ColumnSet";
    std::vector<glz::json_t> header_col_list;

    // Column 1: Icon 🔄 (\ud83d\udd04)
    glz::json_t icon_col;
    icon_col["type"] = "Column";
    icon_col["width"] = "auto";
    std::vector<glz::json_t> icon_items;
    glz::json_t icon_tb;
    icon_tb["type"] = "TextBlock";
    icon_tb["text"] = "\xF0\x9F\x94\x84";
    icon_tb["size"] = "Large";
    icon_items.push_back(std::move(icon_tb));
    icon_col["items"] = std::move(icon_items);
    header_col_list.push_back(std::move(icon_col));

    // Column 2: Title and Subtitle
    glz::json_t title_col;
    title_col["type"] = "Column";
    title_col["width"] = "stretch";
    std::vector<glz::json_t> title_items;
    glz::json_t title_tb;
    title_tb["type"] = "TextBlock";
    title_tb["text"] = "Universal Unit Converter";
    title_tb["weight"] = "Bolder";
    title_tb["size"] = "Large";
    title_items.push_back(std::move(title_tb));

    glz::json_t subtitle_tb;
    subtitle_tb["type"] = "TextBlock";
    subtitle_tb["text"] = "Physical and digital unit transformations";
    subtitle_tb["isSubtle"] = true;
    subtitle_tb["size"] = "Small";
    title_items.push_back(std::move(subtitle_tb));
    title_col["items"] = std::move(title_items);
    header_col_list.push_back(std::move(title_col));

    header_cols["columns"] = std::move(header_col_list);
    std::vector<glz::json_t> header_items;
    header_items.push_back(std::move(header_cols));
    header["items"] = std::move(header_items);
    body.push_back(std::move(header));

    // 2. Input Container
    glz::json_t input_container;
    input_container["type"] = "Container";
    std::vector<glz::json_t> input_items;

    // Category ChoiceSet
    glz::json_t cat_choice;
    cat_choice["type"] = "Input.ChoiceSet";
    cat_choice["id"] = "category";
    cat_choice["label"] = "Conversion Category";
    cat_choice["value"] = get_category();
    std::vector<glz::json_t> cat_choices;
    for (const auto& cat : categories_) {
        std::string cat_name(cat->name());
        cat_choices.push_back(glz::json_t::object_t{
            {"title", cat_name},
            {"value", cat_name}
        });
    }
    cat_choice["choices"] = std::move(cat_choices);
    input_items.push_back(std::move(cat_choice));

    // Input Number
    glz::json_t num_input;
    num_input["type"] = "Input.Number";
    num_input["id"] = "input_value";
    num_input["label"] = "Input Value";
    num_input["value"] = std::string(numeric_input_.data());
    input_items.push_back(std::move(num_input));

    // From and To Units ColumnSet
    glz::json_t unit_cols;
    unit_cols["type"] = "ColumnSet";
    std::vector<glz::json_t> unit_col_list;

    const auto& cur_units = (current_category_index_ < categories_.size())
        ? categories_[current_category_index_]->units()
        : std::vector<conversion_category::unit_info>{};

    // Column 1: From Unit ChoiceSet
    glz::json_t from_col;
    from_col["type"] = "Column";
    from_col["width"] = "50";
    std::vector<glz::json_t> from_col_items;
    glz::json_t from_choice;
    from_choice["type"] = "Input.ChoiceSet";
    from_choice["id"] = "from_unit";
    from_choice["label"] = "From Unit";
    from_choice["value"] = get_from_unit();
    std::vector<glz::json_t> from_choices;
    for (const auto& u : cur_units) {
        std::string title = u.symbol.empty() ? u.name : std::format("{} ({})", u.name, u.symbol);
        from_choices.push_back(glz::json_t::object_t{
            {"title", title},
            {"value", u.name}
        });
    }
    from_choice["choices"] = std::move(from_choices);
    from_col_items.push_back(std::move(from_choice));
    from_col["items"] = std::move(from_col_items);
    unit_col_list.push_back(std::move(from_col));

    // Column 2: To Unit ChoiceSet
    glz::json_t to_col;
    to_col["type"] = "Column";
    to_col["width"] = "50";
    std::vector<glz::json_t> to_col_items;
    glz::json_t to_choice;
    to_choice["type"] = "Input.ChoiceSet";
    to_choice["id"] = "to_unit";
    to_choice["label"] = "To Unit";
    to_choice["value"] = get_to_unit();
    std::vector<glz::json_t> to_choices;
    for (const auto& u : cur_units) {
        std::string title = u.symbol.empty() ? u.name : std::format("{} ({})", u.name, u.symbol);
        to_choices.push_back(glz::json_t::object_t{
            {"title", title},
            {"value", u.name}
        });
    }
    to_choice["choices"] = std::move(to_choices);
    to_col_items.push_back(std::move(to_choice));
    to_col["items"] = std::move(to_col_items);
    unit_col_list.push_back(std::move(to_col));

    unit_cols["columns"] = std::move(unit_col_list);
    input_items.push_back(std::move(unit_cols));

    input_container["items"] = std::move(input_items);
    body.push_back(std::move(input_container));

    // 3. Converted Result Container (style: accent, separator: true)
    glz::json_t result_container;
    result_container["type"] = "Container";
    result_container["style"] = "accent";
    result_container["separator"] = true;

    std::vector<glz::json_t> result_items;
    glz::json_t result_title_tb;
    result_title_tb["type"] = "TextBlock";
    result_title_tb["text"] = "Converted Result";
    result_title_tb["weight"] = "Bolder";
    result_title_tb["color"] = "Accent";
    result_items.push_back(std::move(result_title_tb));

    std::string result_text;
    if (!error_message_.empty()) {
        result_text = "Error: " + error_message_;
    } else if (!conversion_result_.empty()) {
        result_text = std::format("{} {} = {}", numeric_input_.data(), get_from_unit(), conversion_result_);
    } else {
        result_text = "Enter a value to convert";
    }

    glz::json_t result_val_tb;
    result_val_tb["type"] = "TextBlock";
    result_val_tb["text"] = result_text;
    result_val_tb["weight"] = "Bolder";
    result_val_tb["size"] = "Large";
    if (!error_message_.empty()) {
        result_val_tb["color"] = "Attention";
    }
    result_items.push_back(std::move(result_val_tb));

    result_container["items"] = std::move(result_items);
    body.push_back(std::move(result_container));

    // 4. FactSet
    glz::json_t fact_set;
    fact_set["type"] = "FactSet";
    std::vector<glz::json_t> facts;
    facts.push_back(glz::json_t::object_t{{"title", "Category:"}, {"value", get_category()}});
    facts.push_back(glz::json_t::object_t{{"title", "From Unit:"}, {"value", get_from_unit()}});
    facts.push_back(glz::json_t::object_t{{"title", "To Unit:"}, {"value", get_to_unit()}});
    facts.push_back(glz::json_t::object_t{{"title", "Result:"}, {"value", !conversion_result_.empty() ? conversion_result_ : "N/A"}});
    fact_set["facts"] = std::move(facts);
    body.push_back(std::move(fact_set));

    card["body"] = std::move(body);

    // Actions
    std::vector<glz::json_t> actions;
    glz::json_t act_calc;
    act_calc["type"] = "Action.Execute";
    act_calc["title"] = "Calculate";
    act_calc["verb"] = "convert";
    actions.push_back(std::move(act_calc));

    glz::json_t act_swap;
    act_swap["type"] = "Action.Execute";
    act_swap["title"] = "Swap Units";
    act_swap["verb"] = "swap_units";
    actions.push_back(std::move(act_swap));

    glz::json_t act_copy;
    act_copy["type"] = "Action.Execute";
    act_copy["title"] = "Copy Result";
    act_copy["verb"] = "copy_result";
    actions.push_back(std::move(act_copy));

    card["actions"] = std::move(actions);

    std::string out;
    static_cast<void>(glz::write_json(card, out));
    return out;
}

void converter::handle_action(std::string_view action_json) {
    if (action_json.empty()) return;

    try {
        std::string verb;
        glz::json_t data{};
        glz::json_t payload{};

        auto err = glz::read_json(payload, std::string(action_json));
        if (!err && payload.is_object()) {
            if (payload.contains("verb") && payload["verb"].holds<std::string>()) {
                verb = payload["verb"].get<std::string>();
            } else if (payload.contains("action") && payload["action"].holds<std::string>()) {
                verb = payload["action"].get<std::string>();
            }
            if (payload.contains("data") && payload["data"].is_object()) {
                data = payload["data"];
            } else {
                data = payload;
            }
        } else {
            verb = std::string(action_json);
        }

        if (verb == "convert" || verb == "calculate") {
            // 1. Category
            std::string cat_str;
            if (data.contains("category") && data["category"].holds<std::string>()) {
                cat_str = data["category"].get<std::string>();
            } else if (payload.contains("category") && payload["category"].holds<std::string>()) {
                cat_str = payload["category"].get<std::string>();
            }

            if (!cat_str.empty()) {
                for (size_t i = 0; i < categories_.size(); ++i) {
                    std::string const name(categories_[i]->name());
                    if (name == cat_str ||
                        (cat_str == "Mass" && name == "Weight") ||
                        (cat_str == "Mass / Weight" && name == "Weight") ||
                        (cat_str == "Data" && name == "Encoding") ||
                        (cat_str == "Data Storage" && name == "Encoding")) {
                        if (current_category_index_ != i) {
                            current_category_index_ = i;
                            reset_unit_selections();
                            clear_results();
                        }
                        break;
                    }
                }
            }

            // 2. Units in current category
            if (current_category_index_ < categories_.size()) {
                const auto& units = categories_[current_category_index_]->units();

                // From Unit
                std::string from_str;
                if (data.contains("from_unit") && data["from_unit"].holds<std::string>()) {
                    from_str = data["from_unit"].get<std::string>();
                } else if (data.contains("from") && data["from"].holds<std::string>()) {
                    from_str = data["from"].get<std::string>();
                } else if (payload.contains("from_unit") && payload["from_unit"].holds<std::string>()) {
                    from_str = payload["from_unit"].get<std::string>();
                }

                if (!from_str.empty()) {
                    for (size_t i = 0; i < units.size(); ++i) {
                        if (units[i].name == from_str || units[i].symbol == from_str) {
                            from_unit_index_ = i;
                            break;
                        }
                    }
                }

                // To Unit
                std::string to_str;
                if (data.contains("to_unit") && data["to_unit"].holds<std::string>()) {
                    to_str = data["to_unit"].get<std::string>();
                } else if (data.contains("to") && data["to"].holds<std::string>()) {
                    to_str = data["to"].get<std::string>();
                } else if (payload.contains("to_unit") && payload["to_unit"].holds<std::string>()) {
                    to_str = payload["to_unit"].get<std::string>();
                }

                if (!to_str.empty()) {
                    for (size_t i = 0; i < units.size(); ++i) {
                        if (units[i].name == to_str || units[i].symbol == to_str) {
                            to_unit_index_ = i;
                            break;
                        }
                    }
                }
            }

            // 3. Input value
            bool has_input = false;
            std::string input_str;
            if (data.contains("input_value")) {
                has_input = true;
                if (data["input_value"].holds<double>()) {
                    input_str = std::format("{:.6g}", data["input_value"].get<double>());
                } else if (data["input_value"].holds<std::string>()) {
                    input_str = data["input_value"].get<std::string>();
                }
            } else if (data.contains("value")) {
                has_input = true;
                if (data["value"].holds<double>()) {
                    input_str = std::format("{:.6g}", data["value"].get<double>());
                } else if (data["value"].holds<std::string>()) {
                    input_str = data["value"].get<std::string>();
                }
            } else if (payload.contains("input_value")) {
                has_input = true;
                if (payload["input_value"].holds<double>()) {
                    input_str = std::format("{:.6g}", payload["input_value"].get<double>());
                } else if (payload["input_value"].holds<std::string>()) {
                    input_str = payload["input_value"].get<std::string>();
                }
            }

            if (has_input && !input_str.empty()) {
                std::strncpy(numeric_input_.data(), input_str.c_str(), numeric_input_.size() - 1);
                numeric_input_[numeric_input_.size() - 1] = '\0';
                std::strncpy(text_input_.data(), input_str.c_str(), text_input_.size() - 1);
                text_input_[text_input_.size() - 1] = '\0';
            }

            // Perform conversion
            if (current_category_index_ < categories_.size()) {
                if (categories_[current_category_index_]->name() == "Encoding") {
                    perform_encoding_conversion();
                } else {
                    perform_numeric_conversion();
                }
            }
        } else if (verb == "swap_units" || verb == "reverse_units") {
            reverse_units();
        } else if (verb == "copy_result") {
            last_copied_ = conversion_result_;
            if (ImGui::GetCurrentContext() != nullptr) {
                try {
                    ImGui::SetClipboardText(conversion_result_.c_str());
                } catch (...) {}
            }
        } else if (verb == "toggle_adaptive_view") {
            adaptive_view_mode = !adaptive_view_mode;
        }

        last_adaptive_parse_time_ = {};
    } catch (const std::exception& e) {
        std::cerr << "[converter] Action error: " << e.what() << std::endl;
    } catch (...) {
    }
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
    if (ImGui::Button(ICON_MD_SYNC_ALT " Reverse Units", ImVec2(120.0f, 0))) {
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
    if (ImGui::Button(ICON_MD_SYNC_ALT " Reverse Units", ImVec2(120.0f, 0))) {
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
