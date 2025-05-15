#include "chess_com_integration.hpp"
#include <imgui.h>
#include <algorithm>

namespace rouen::cards {

void ChessComIntegration::render_ui(ImVec4 info_color, ImVec4 error_color, std::function<void()> on_game_selected) {
    ImGui::TextColored(info_color, "Chess.com Player Games");
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputText("##ChessUsername", state_.username_buffer, sizeof(state_.username_buffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
        fetch_player_archives(state_.username_buffer);
        state_.chess_com_username = state_.username_buffer;
        state_.from_chess_com = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Search")) {
        fetch_player_archives(state_.username_buffer);
        state_.chess_com_username = state_.username_buffer;
        state_.from_chess_com = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enter a Chess.com username and click Search to find their games");
    }
    if (state_.is_fetching_archives || state_.is_fetching_games) {
        ImGui::SameLine();
        ImGui::Text("Loading...");
    }
    if (!state_.api_error.empty()) {
        ImGui::TextColored(error_color, "Error: %s", state_.api_error.c_str());
    }
    if (!state_.player_archives.empty()) {
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::BeginCombo("##ArchiveMonths", state_.selected_archive.empty() ? "Select Month" : state_.selected_archive.c_str())) {
            for (const auto& archive : state_.player_archives) {
                std::string month_display = archive.substr(archive.length() - 7);
                month_display[4] = '-';
                bool is_selected = (state_.selected_archive == month_display);
                if (ImGui::Selectable(month_display.c_str(), is_selected)) {
                    state_.selected_archive = month_display;
                    fetch_archive_games(archive);
                }
                if (is_selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (!state_.archive_games.empty()) {
            ImGui::SetNextItemWidth(500.0f);
            if (ImGui::BeginCombo("##GamesList", state_.selected_game_index < 0 ? "Select Game" : api_.format_game_display(state_.archive_games[static_cast<size_t>(state_.selected_game_index)]).c_str())) {
                for (size_t i = 0; i < state_.archive_games.size(); ++i) {
                    const auto& chess_game_item = state_.archive_games[i];
                    std::string game_display = api_.format_game_display(chess_game_item);
                    bool is_selected = (state_.selected_game_index == static_cast<int>(i));
                    if (ImGui::Selectable(game_display.c_str(), is_selected)) {
                        state_.selected_game_index = static_cast<int>(i);
                        if (on_game_selected) on_game_selected();
                    }
                    if (is_selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }
    }
}

void ChessComIntegration::fetch_player_archives(const std::string& username) {
    if (username.empty()) {
        state_.api_error = "Please enter a username";
        return;
    }
    state_.player_archives.clear();
    state_.archive_games.clear();
    state_.selected_archive.clear();
    state_.selected_game_index = -1;
    state_.api_error.clear();
    state_.is_fetching_archives = true;
    state_.archives_future = api_.fetch_player_archives(username);
}

void ChessComIntegration::fetch_archive_games(const std::string& archive_url) {
    state_.archive_games.clear();
    state_.selected_game_index = -1;
    state_.api_error.clear();
    state_.is_fetching_games = true;
    state_.games_future = api_.fetch_archive_games(archive_url);
}

void ChessComIntegration::process_api_responses() {
    if (state_.is_fetching_archives && state_.archives_future.valid()) {
        auto status = state_.archives_future.wait_for(std::chrono::seconds(0));
        if (status == std::future_status::ready) {
            auto [success, result] = state_.archives_future.get();
            state_.is_fetching_archives = false;
            if (success) {
                bool parsed = api_.process_archives_response(result, state_.player_archives);
                if (!parsed) state_.api_error = "Error parsing archives response";
            } else {
                state_.api_error = result;
            }
        }
    }
    if (state_.is_fetching_games && state_.games_future.valid()) {
        auto status = state_.games_future.wait_for(std::chrono::seconds(0));
        if (status == std::future_status::ready) {
            auto [success, result] = state_.games_future.get();
            state_.is_fetching_games = false;
            if (success) {
                bool parsed = api_.process_games_response(result, state_.archive_games);
                if (!parsed) state_.api_error = "Error parsing games response";
            } else {
                state_.api_error = result;
            }
        }
    }
}

std::string ChessComIntegration::get_selected_pgn() const {
    if (state_.selected_game_index < 0 || static_cast<size_t>(state_.selected_game_index) >= state_.archive_games.size())
        return {};
    return state_.archive_games[static_cast<size_t>(state_.selected_game_index)].pgn;
}

std::string ChessComIntegration::get_selected_game_display() const {
    if (state_.selected_game_index < 0 || static_cast<size_t>(state_.selected_game_index) >= state_.archive_games.size())
        return {};
    return api_.format_game_display(state_.archive_games[static_cast<size_t>(state_.selected_game_index)]);
}

bool ChessComIntegration::has_selected_game() const {
    return state_.selected_game_index >= 0 && static_cast<size_t>(state_.selected_game_index) < state_.archive_games.size();
}

} // namespace rouen::cards
