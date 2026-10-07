#include "fs-directory.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <imgui.h>
#include <regex>
#include <string>
#include <string_view>
#include <system_error>

#include "../../../external/IconsMaterialDesign.h"
#include "../../helpers/config_service.hpp"
#include "../../helpers/filetype_handler.hpp"
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/platform_utils.hpp"

#include "../information/image_viewer.hpp"
#include "../media/media_card.hpp"
#include "registrar.hpp"

namespace rouen::cards {

    std::string resolve_env_variables(std::string_view path_with_vars) {
        std::string result(path_with_vars);
        const std::regex env_var_regex(R"(\$(\w+))");

        auto config_service = helpers::ConfigService::instance();

        std::smatch match;
        std::string temp = result;
        while (std::regex_search(temp, match, env_var_regex)) {
            std::string const var_name = match[1].str();
            std::string const var_value = config_service->get_env(var_name);

            size_t const pos = result.find("$" + var_name);
            if (pos != std::string::npos) {
                result.replace(pos, var_name.length() + 1, var_value);
            }

            temp = match.suffix();
        }

        return result;
    }

    fs_directory::fs_directory(std::string_view path)
        : path_{resolve_env_variables(path)} {
        // Base colors
        colors[0] = {0.37f, 0.53f, 0.71f, 1.0f};    // Primary color - blue accent
        colors[1] = {0.251f, 0.878f, 0.816f, 0.7f}; // Secondary color - turquoise

        // Additional colors for file types
        get_color(2, {255.0f / 255.0f, 100.0f / 255.0f, 100.0f / 255.0f, 1.0f}); // Executable files - Red/Error
        get_color(3, {120.0f / 255.0f, 220.0f / 255.0f, 120.0f / 255.0f, 1.0f}); // Code files - Green/Success
        get_color(4, {220.0f / 255.0f, 220.0f / 255.0f, 120.0f / 255.0f, 1.0f}); // Text files - Yellow/Warning
        get_color(5, {150.0f / 255.0f, 150.0f / 255.0f, 255.0f / 255.0f, 1.0f}); // Parent & Directories - Blue/Info
        get_color(6, {220.0f / 255.0f, 120.0f / 255.0f, 220.0f / 255.0f, 1.0f}); // Image files - Purple/Special 1
        get_color(8, {120.0f / 255.0f, 220.0f / 255.0f, 220.0f / 255.0f, 1.0f}); // Symlinks - Cyan/Special 3
        get_color(9, {200.0f / 255.0f, 200.0f / 255.0f, 200.0f / 255.0f, 1.0f}); // Other files - Light Gray/Special 4

        if (path_.empty()) {
            path_ = std::filesystem::current_path();
        }

        name(path_.string());
        refresh_cache();
    }

    std::string fs_directory::get_uri() const {
        return std::format("dir:{}", path_.string());
    }

    bool fs_directory::matches_uri(std::string_view uri) const {
        if (uri == "dir") return true;
        if (uri.starts_with("dir:")) {
            std::string_view req = uri.substr(4);
            return req.empty() || path_.string() == req;
        }
        return get_uri() == uri;
    }

    namespace {
        [[nodiscard]] std::string format_file_size(uintmax_t bytes) {
            constexpr double KB = 1024.0;
            constexpr double MB = KB * 1024.0;
            constexpr double GB = MB * 1024.0;

            double const b = static_cast<double>(bytes);
            if (b >= GB) {
                return std::format("{:.2f} GB", b / GB);
            }
            if (b >= MB) {
                return std::format("{:.1f} MB", b / MB);
            }
            if (b >= KB) {
                return std::format("{:.1f} KB", b / KB);
            }
            return std::format("{} B", bytes);
        }
    } // namespace

    std::string fs_directory::get_adaptive_card_json() const {
        glz::json_t card;
        card["type"] = "AdaptiveCard";
        card["version"] = "1.5";
        card["schema"] = "http://adaptivecards.io/schemas/adaptive-card.json";

        std::vector<glz::json_t> body;

        // 1. Header with current directory path
        {
            glz::json_t header;
            header["type"] = "TextBlock";
            header["text"] = std::format(ICON_MD_FOLDER " {}", path_.string());
            header["weight"] = "Bolder";
            header["size"] = "Medium";
            header["color"] = "Accent";
            header["wrap"] = true;
            body.push_back(std::move(header));
        }

        // Subtitle with stats
        size_t dir_count = 0;
        size_t file_count = 0;
        for (const auto& entry : cached_entries_) {
            std::error_code ec;
            if (entry.is_directory(ec)) dir_count++;
            else if (entry.is_regular_file(ec)) file_count++;
        }

        {
            glz::json_t sub;
            sub["type"] = "TextBlock";
            std::string meta = std::format("{} folder{}, {} file{}",
                dir_count, dir_count == 1 ? "" : "s",
                file_count, file_count == 1 ? "" : "s");
            if (is_git_repo_) {
                meta += " • Git Repository";
            }
            sub["text"] = meta;
            sub["isSubtle"] = true;
            sub["size"] = "Small";
            body.push_back(std::move(sub));
        }

        // 2. Navigation toolbar: Up, Refresh
        {
            glz::json_t act_set;
            act_set["type"] = "ActionSet";
            std::vector<glz::json_t> acts;

            if (path_.has_parent_path() && path_.parent_path() != path_) {
                glz::json_t up_act;
                up_act["type"] = "Action.Execute";
                up_act["title"] = ICON_MD_ARROW_UPWARD " Up (..)";
                up_act["verb"] = "cd";
                up_act["data"] = glz::json_t::object_t{{"path", path_.parent_path().string()}};
                acts.push_back(std::move(up_act));
            }

            glz::json_t ref_act;
            ref_act["type"] = "Action.Execute";
            ref_act["title"] = ICON_MD_REFRESH " Refresh";
            ref_act["verb"] = "refresh";
            acts.push_back(std::move(ref_act));

            act_set["actions"] = std::move(acts);
            body.push_back(std::move(act_set));
        }

        // 3. Entries listing
        std::error_code ec;
        if (!std::filesystem::exists(path_, ec)) {
            glz::json_t err_text;
            err_text["type"] = "TextBlock";
            err_text["text"] = ICON_MD_WARNING " Directory does not exist or is inaccessible";
            err_text["color"] = "Attention";
            body.push_back(std::move(err_text));
        } else if (cached_entries_.empty()) {
            glz::json_t empty_text;
            empty_text["type"] = "TextBlock";
            empty_text["text"] = "(Empty directory)";
            empty_text["isSubtle"] = true;
            body.push_back(std::move(empty_text));
        } else {
            // Sort: directories first, then files
            std::vector<std::filesystem::directory_entry> sorted_entries = cached_entries_;
            std::sort(sorted_entries.begin(), sorted_entries.end(), [](const auto& a, const auto& b) {
                std::error_code ec1, ec2;
                bool a_dir = a.is_directory(ec1);
                bool b_dir = b.is_directory(ec2);
                if (a_dir != b_dir) return a_dir > b_dir;
                return a.path().filename().string() < b.path().filename().string();
            });

            constexpr size_t max_card_entries = 150;
            size_t const count = std::min(sorted_entries.size(), max_card_entries);

            for (size_t i = 0; i < count; ++i) {
                const auto& entry = sorted_entries[i];
                std::error_code entry_ec;
                bool is_dir = entry.is_directory(entry_ec);
                std::string const filename = entry.path().filename().string();

                glz::json_t item_container;
                item_container["type"] = "Container";

                if (is_dir) {
                    glz::json_t act;
                    act["type"] = "Action.Execute";
                    act["verb"] = "cd";
                    act["data"] = glz::json_t::object_t{{"path", entry.path().string()}};
                    item_container["selectAction"] = std::move(act);
                }

                glz::json_t col_set;
                col_set["type"] = "ColumnSet";
                std::vector<glz::json_t> cols;

                {
                    glz::json_t col;
                    col["type"] = "Column";
                    col["width"] = "stretch";
                    std::vector<glz::json_t> col_items;
                    glz::json_t text;
                    text["type"] = "TextBlock";
                    text["text"] = std::format("{} {}", is_dir ? ICON_MD_FOLDER : ICON_MD_DESCRIPTION, filename);
                    if (is_dir) {
                        text["color"] = "Accent";
                    }
                    col_items.push_back(std::move(text));
                    col["items"] = std::move(col_items);
                    cols.push_back(std::move(col));
                }

                {
                    glz::json_t col;
                    col["type"] = "Column";
                    col["width"] = "80px";
                    std::vector<glz::json_t> col_items;
                    glz::json_t text;
                    text["type"] = "TextBlock";
                    if (is_dir) {
                        text["text"] = "<DIR>";
                    } else {
                        std::error_code sz_ec;
                        uintmax_t sz = entry.file_size(sz_ec);
                        text["text"] = sz_ec ? "" : format_file_size(sz);
                    }
                    text["isSubtle"] = true;
                    text["horizontalAlignment"] = "Right";
                    col_items.push_back(std::move(text));
                    col["items"] = std::move(col_items);
                    cols.push_back(std::move(col));
                }

                col_set["columns"] = std::move(cols);
                item_container["items"] = std::vector<glz::json_t>{std::move(col_set)};
                body.push_back(std::move(item_container));
            }

            if (sorted_entries.size() > max_card_entries) {
                glz::json_t more_text;
                more_text["type"] = "TextBlock";
                more_text["text"] = std::format("... and {} more items", sorted_entries.size() - max_card_entries);
                more_text["isSubtle"] = true;
                body.push_back(std::move(more_text));
            }
        }

        card["body"] = std::move(body);

        std::string out;
        static_cast<void>(glz::write_json(card, out));
        return out;
    }

    void fs_directory::handle_action(std::string_view action_json) {
        try {
            glz::json_t payload;
            auto err = glz::read_json(payload, std::string(action_json));
            if (err) return;

            std::string verb;
            if (payload.contains("verb") && payload["verb"].holds<std::string>()) {
                verb = payload["verb"].get<std::string>();
            } else if (payload.contains("action") && payload["action"].holds<std::string>()) {
                verb = payload["action"].get<std::string>();
            }

            glz::json_t data = payload.contains("data") ? payload["data"] : payload;

            if (verb == "cd" || verb == "navigate") {
                if (data.contains("path") && data["path"].holds<std::string>()) {
                    std::filesystem::path new_path = data["path"].get<std::string>();
                    std::error_code ec;
                    if (std::filesystem::is_directory(new_path, ec)) {
                        path_ = new_path;
                        name(path_.string());
                        filter_.clear();
                        last_rejected_char_ = '\0';
                        search_active_ = false;
                        search_results_.clear();
                        refresh_cache();
                    }
                }
            } else if (verb == "up") {
                if (path_.has_parent_path() && path_.parent_path() != path_) {
                    path_ = path_.parent_path();
                    name(path_.string());
                    filter_.clear();
                    last_rejected_char_ = '\0';
                    search_active_ = false;
                    search_results_.clear();
                    refresh_cache();
                }
            } else if (verb == "refresh") {
                refresh_cache();
            }
        } catch (...) {}
    }

    std::string fs_directory::to_lower(std::string_view s) {
        std::string res(s);
        std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return res;
    }

    void fs_directory::refresh_cache() {
        is_git_repo_ = false;
        cached_entries_.clear();

        std::error_code ec;
        if (std::filesystem::exists(path_, ec)) {
            is_git_repo_ = std::filesystem::exists(path_ / ".git", ec);

            for (const auto& entry : std::filesystem::directory_iterator(path_, ec)) {
                cached_entries_.push_back(entry);
            }
            std::sort(cached_entries_.begin(), cached_entries_.end(), [](const auto& a, const auto& b) {
                return a.path() < b.path();
            });
        }
        last_refresh_ = std::chrono::steady_clock::now();
    }

    void fs_directory::perform_search(std::string_view query) {
        search_results_.clear();
        if (query.empty()) return;

        std::string const q_lower = to_lower(query);
        std::error_code ec;

        auto options = std::filesystem::directory_options::skip_permission_denied;
        auto iter = std::filesystem::recursive_directory_iterator(path_, options, ec);
        auto end = std::filesystem::recursive_directory_iterator();

        size_t count = 0;
        constexpr size_t max_results = 200;

        while (iter != end && !ec && count < max_results) {
            if (iter.depth() > 6) {
                iter.pop();
                continue;
            }

            const auto& entry = *iter;
            std::string const filename = entry.path().filename().string();

            if (entry.is_directory() && (filename == ".git" || filename == "node_modules" || filename == "build" || filename == ".agent")) {
                iter.disable_recursion_pending();
            }

            std::error_code rel_ec;
            auto rel_path = std::filesystem::relative(entry.path(), path_, rel_ec);
            std::string const target_str = rel_ec ? filename : rel_path.string();

            if (to_lower(target_str).find(q_lower) != std::string::npos) {
                search_results_.push_back(entry);
                count++;
            }

            iter.increment(ec);
        }

        std::sort(search_results_.begin(), search_results_.end(), [](const auto& a, const auto& b) {
            if (a.is_directory() != b.is_directory()) {
                return a.is_directory() > b.is_directory();
            }
            return a.path() < b.path();
        });
    }

    std::optional<std::filesystem::path> fs_directory::render_entry(const std::filesystem::directory_entry& entry, const std::string& display_label) {
        std::optional<std::filesystem::path> nav_target;

        if (entry.is_directory()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[5]));
        } else if (entry.is_regular_file()) {
            std::string const ext = entry.path().extension().string();
            if (ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".c" || ext == ".cc") {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[3]));
            } else if (ext == ".txt" || ext == ".md" || ext == ".json" || ext == ".yaml" || ext == ".yml") {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[4]));
            } else if (is_supported_image_extension(ext)) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[6]));
            } else if (is_supported_media_extension(ext)) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[1]));
            } else if (ext == ".pdf" || ext == ".PDF") {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[0]));
            } else if (ext == ".exe" || ext.empty() || ext == ".bin" || ext == ".sh") {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[2]));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[9]));
            }
        } else if (entry.is_symlink()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[8]));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[9]));
        }

        const bool ctrl_or_cmd = ImGui::GetIO().KeySuper || ImGui::GetIO().KeyCtrl;

        if (ImGui::Selectable(display_label.c_str())) {
            if (entry.is_directory()) {
                if (ctrl_or_cmd) {
                    if (ImGui::GetIO().KeyShift) {
                        "create_card"_sfn(std::format("terminal:{}", entry.path().string()));
                    } else {
                        "create_card"_sfn(std::format("dir:{}", entry.path().string()));
                    }
                } else {
                    nav_target = entry.path();
                }
            } else {
                auto uri = helpers::FiletypeHandler::instance().resolve(entry.path(), ctrl_or_cmd);
                if (uri.has_value()) {
                    "create_card"_sfn(uri.value());
                } else {
                    "edit"_sfn(entry.path().string());
                }
            }
        }

        if (ImGui::BeginPopupContextItem()) {
            if (entry.is_directory()) {
                if (ImGui::MenuItem(ICON_MD_FOLDER_OPEN " Open")) {
                    nav_target = entry.path();
                }
                if (ImGui::MenuItem(ICON_MD_TAB " Open in New Card")) {
                    "create_card"_sfn(std::format("dir:{}", entry.path().string()));
                }
                if (ImGui::MenuItem(ICON_MD_TERMINAL " Open in Terminal")) {
                    "create_card"_sfn(std::format("terminal:{}", entry.path().string()));
                }
            } else {
                std::string const ext = to_lower(entry.path().extension().string());
                const bool is_md = (ext == ".md" || ext == ".markdown");

                if (is_md) {
                    if (ImGui::MenuItem(ICON_MD_VISIBILITY " View Markdown", "Ctrl/Cmd+Click")) {
                        "create_card"_sfn(std::format("markdown:{}", entry.path().string()));
                    }
                    if (ImGui::MenuItem(ICON_MD_EDIT " Edit in Text Editor", "Click")) {
                        "edit"_sfn(entry.path().string());
                    }
                } else {
                    if (ImGui::MenuItem(ICON_MD_EDIT " Edit in Text Editor")) {
                        "edit"_sfn(entry.path().string());
                    }
                    auto card_uri = helpers::FiletypeHandler::instance().resolve(entry.path(), true);
                    if (card_uri.has_value()) {
                        if (ImGui::MenuItem(ICON_MD_OPEN_IN_NEW " Open with Card", "Ctrl/Cmd+Click")) {
                            "create_card"_sfn(card_uri.value());
                        }
                    }
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_MD_CONTENT_COPY " Copy Path")) {
                ImGui::SetClipboardText(entry.path().string().c_str());
            }
            ImGui::EndPopup();
        }

        ImGui::PopStyleColor();
        return nav_target;
    }

    bool fs_directory::matches_filter(const std::filesystem::directory_entry& entry, std::string_view filter) const {
        if (filter.empty()) {
            return true;
        }
        std::string const filename = entry.path().filename().string();
        if (filter.size() > filename.size()) {
            return false;
        }
        auto const filter_lower = to_lower(filter);
        auto const prefix_lower = to_lower(std::string_view(filename.data(), filter.size()));
        return prefix_lower == filter_lower;
    }

    bool fs_directory::has_any_match(std::string_view filter) const {
        if (filter.empty()) {
            return true;
        }
        return std::any_of(cached_entries_.begin(), cached_entries_.end(), [this, filter](const auto& entry) {
            return matches_filter(entry, filter);
        });
    }

    size_t fs_directory::count_matches(std::string_view filter) const {
        if (filter.empty()) {
            return cached_entries_.size();
        }
        return static_cast<size_t>(std::count_if(cached_entries_.begin(), cached_entries_.end(), [this, filter](const auto& entry) {
            return matches_filter(entry, filter);
        }));
    }

    bool fs_directory::process_filter_char(char c) {
        if (c == '\b') {
            if (!filter_.empty()) {
                filter_.pop_back();
            }
            return true;
        } else if (c == '\n' || c == '\033' || c == '\r') {
            filter_.clear();
            last_rejected_char_ = '\0';
            return true;
        } else if (c == '\t') {
            return true; // ignore tab navigation
        } else if (static_cast<unsigned char>(c) >= 32 && static_cast<unsigned char>(c) <= 126) {
            std::string candidate = filter_;
            candidate += c;

            if (has_any_match(candidate)) {
                filter_ = std::move(candidate);
                last_rejected_char_ = '\0';
                return true;
            } else {
                rouen::platform::system_beep();
                last_rejected_char_ = c;
                last_rejected_keystroke_time_ = std::chrono::steady_clock::now();
                return false;
            }
        }
        return false;
    }

    void fs_directory::receive_keystrokes() {
        const bool ctrl_or_cmd = ImGui::GetIO().KeySuper || ImGui::GetIO().KeyCtrl;
        if (ImGui::IsWindowFocused() && ctrl_or_cmd && ImGui::IsKeyPressed(ImGuiKey_F)) {
            search_active_ = !search_active_;
            if (search_active_) {
                focus_search_input_ = true;
                search_query_buf_[0] = '\0';
                last_typed_query_.clear();
                last_searched_query_.clear();
                search_results_.clear();
                search_pending_ = false;
            }
            [[maybe_unused]] auto r = "keystrokes"_fns();
            return;
        }

        if (search_active_) {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                search_active_ = false;
                search_results_.clear();
                search_pending_ = false;
            }
            [[maybe_unused]] auto r = "keystrokes"_fns();
            return;
        }

        if (ImGui::IsWindowFocused() && ctrl_or_cmd && ImGui::IsKeyPressed(ImGuiKey_T)) {
            "create_card"_sfn(std::format("terminal:{}", path_.string()));
            [[maybe_unused]] auto r = "keystrokes"_fns();
            return;
        }

        for (char const c : "keystrokes"_fns()) {
            process_filter_char(c);
        }
    }

    bool fs_directory::render() {
        return render_window([this]() {
            if (ImGui::IsWindowFocused()) {
                receive_keystrokes();
            }

            auto now = std::chrono::steady_clock::now();
            if (now - last_refresh_ >= std::chrono::seconds(20)) {
                refresh_cache();
            }

            if (search_active_) {
                if (focus_search_input_) {
                    ImGui::SetKeyboardFocusHere(0);
                    focus_search_input_ = false;
                }
                float const avail_width = ImGui::GetContentRegionAvail().x;
                ImGui::PushItemWidth(std::max(100.0f, avail_width - 35.0f));
                ImGui::InputTextWithHint("##find_input", ICON_MD_SEARCH " Find file or directory...", search_query_buf_, sizeof(search_query_buf_));
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::Button(ICON_MD_CLOSE "##close_find")) {
                    search_active_ = false;
                    search_results_.clear();
                    search_pending_ = false;
                }

                if (std::string_view(search_query_buf_) != last_typed_query_) {
                    last_typed_query_ = search_query_buf_;
                    last_type_time_ = std::chrono::steady_clock::now();
                    search_pending_ = !last_typed_query_.empty();
                    if (last_typed_query_.empty()) {
                        search_results_.clear();
                        last_searched_query_.clear();
                    }
                }

                if (search_pending_ && !last_typed_query_.empty()) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_type_time_).count();
                    if (elapsed >= 500) {
                        perform_search(last_typed_query_);
                        last_searched_query_ = last_typed_query_;
                        search_pending_ = false;
                    } else {
                        ImGui::TextDisabled("Searching in %d ms...", static_cast<int>(500 - elapsed));
                    }
                } else if (!last_searched_query_.empty()) {
                    ImGui::TextDisabled("Found %zu item(s) matching '%s'", search_results_.size(), last_searched_query_.c_str());
                }

                ImGui::Separator();
            }

            if (is_git_repo_) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.37f, 0.53f, 0.71f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.47f, 0.63f, 0.81f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.27f, 0.43f, 0.61f, 1.0f));

                if (ImGui::Button(ICON_MD_CALL_SPLIT " Open as Git Repo")) {
                    "create_card"_sfn(std::format("git:{}", path_.string()));
                }

                ImGui::PopStyleColor(3);
                ImGui::Separator();
            }

            if (!filter_.empty() || last_rejected_char_ != '\0') {
                auto const elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_rejected_keystroke_time_
                ).count();
                bool const show_rejection = (last_rejected_char_ != '\0' && elapsed_ms < 1200);

                size_t const match_count = count_matches(filter_);

                ImGui::PushStyleColor(ImGuiCol_ChildBg, show_rejection 
                    ? ImVec4(0.35f, 0.12f, 0.12f, 0.60f)  // Subtle red tint on error
                    : ImVec4(0.18f, 0.22f, 0.28f, 0.60f)); // Normal filter badge bg

                if (ImGui::BeginChild("##active_filter_bar", ImVec2(0.0f, 28.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar)) {
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("%s Filter: \"%s\"", ICON_MD_FILTER_LIST, filter_.c_str());
                    
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%zu %s)", match_count, match_count == 1 ? "match" : "matches");

                    if (show_rejection) {
                        ImGui::SameLine();
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
                        ImGui::Text("- '%c' rejected (0 matches)", last_rejected_char_);
                        ImGui::PopStyleColor();
                    }

                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.0f);
                    if (ImGui::SmallButton(ICON_MD_CLOSE " Clear")) {
                        filter_.clear();
                        last_rejected_char_ = '\0';
                    }
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::Spacing();
            }

            std::optional<std::filesystem::path> pending_nav;

            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[5]));
            const bool ctrl_or_cmd = ImGui::GetIO().KeySuper || ImGui::GetIO().KeyCtrl;
            if (ImGui::Selectable(ICON_MD_ARROW_UPWARD " ..")) {
                auto entry = path_.parent_path();
                if (ctrl_or_cmd) {
                    "create_card"_sfn(std::format("dir:{}", entry.string()));
                } else {
                    pending_nav = entry;
                }
            }
            if (ImGui::BeginPopupContextItem("parent_dir_ctx")) {
                auto entry = path_.parent_path();
                if (ImGui::MenuItem(ICON_MD_FOLDER_OPEN " Open")) {
                    pending_nav = entry;
                }
                if (ImGui::MenuItem(ICON_MD_TAB " Open in New Card")) {
                    "create_card"_sfn(std::format("dir:{}", entry.string()));
                }
                if (ImGui::MenuItem(ICON_MD_TERMINAL " Open in Terminal")) {
                    "create_card"_sfn(std::format("terminal:{}", entry.string()));
                }
                ImGui::EndPopup();
            }
            ImGui::PopStyleColor();

            if (!pending_nav.has_value()) {
                if (search_active_ && !last_searched_query_.empty()) {
                    for (const auto& entry : search_results_) {
                        std::error_code rel_ec;
                        auto rel_path = std::filesystem::relative(entry.path(), path_, rel_ec);
                        std::string const prefix = entry.is_directory() ? ICON_MD_FOLDER " " : ICON_MD_DESCRIPTION " ";
                        std::string const display_label = prefix + (rel_ec ? entry.path().filename().string() : rel_path.string());
                        auto nav = render_entry(entry, display_label);
                        if (nav.has_value()) {
                            pending_nav = nav;
                            break;
                        }
                    }
                } else {
                    for (const auto& entry : cached_entries_) {
                        if (matches_filter(entry, filter_)) {
                            std::string const prefix = entry.is_directory() ? ICON_MD_FOLDER " " : ICON_MD_DESCRIPTION " ";
                            auto nav = render_entry(entry, prefix + entry.path().filename().string());
                            if (nav.has_value()) {
                                pending_nav = nav;
                                break;
                            }
                        }
                    }
                }
            }

            if (pending_nav.has_value()) {
                path_ = pending_nav.value();
                name(path_.string());
                filter_.clear();
                last_rejected_char_ = '\0';
                search_active_ = false;
                search_results_.clear();
                refresh_cache();
            }
        });
    }

} // namespace rouen::cards
