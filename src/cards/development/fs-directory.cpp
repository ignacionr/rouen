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

        if (ImGui::Selectable(display_label.c_str())) {
            if (entry.is_directory()) {
                if (ImGui::GetIO().KeyCtrl) {
                    if (ImGui::GetIO().KeyShift) {
                        "create_card"_sfn(std::format("terminal:{}", entry.path().string()));
                    } else {
                        "create_card"_sfn(std::format("dir:{}", entry.path().string()));
                    }
                } else {
                    nav_target = entry.path();
                }
            } else {
                auto uri = helpers::FiletypeHandler::instance().resolve(entry.path(), ImGui::GetIO().KeyCtrl);
                if (uri.has_value()) {
                    "create_card"_sfn(uri.value());
                } else {
                    "edit"_sfn(entry.path().string());
                }
            }
        }
        ImGui::PopStyleColor();
        return nav_target;
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
            if (c == '\b') {
                if (!filter_.empty()) {
                    filter_.pop_back();
                }
            } else if (c == '\n' || c == '\033' || c == '\r') {
                filter_.clear();
            } else if (c == '\t') {
                // ignore
            } else {
                filter_ += c;
            }
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

            std::optional<std::filesystem::path> pending_nav;

            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(colors[5]));
            if (ImGui::Selectable(ICON_MD_ARROW_UPWARD " ..")) {
                auto entry = path_.parent_path();
                if (ImGui::GetIO().KeyCtrl) {
                    "create_card"_sfn(std::format("dir:{}", entry.string()));
                } else {
                    pending_nav = entry;
                }
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
                        if (filter_.empty() || entry.path().filename().string().starts_with(filter_)) {
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
                search_active_ = false;
                search_results_.clear();
                refresh_cache();
            }
        });
    }

} // namespace rouen::cards
