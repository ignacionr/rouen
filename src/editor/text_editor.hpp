#pragma once

#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <sstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <optional>
#include <filesystem>
#include <format>
#include <algorithm>

#include "../helpers/imgui_include.hpp"
#include <TextEditor.h>

#include "editor_interface.hpp"
#include "../fonts.hpp"
#include "../helpers/syntax_checker.hpp"
#include "../helpers/persona_manager.hpp"
#include "../helpers/string_helper.hpp"
#include "../../external/IconsMaterialDesign.h"
#include "../registrar.hpp"

namespace rouen {
namespace editor {

class TextEditor : public EditorInterface {
public:
    TextEditor() 
        : file_modified_(false)
        , should_focus_(false)
    {
        // Initialize colors
        success_color = {0.0f, 1.0f, 0.0f, 1.0f}; // Green success text
        error_color = {1.0f, 0.0f, 0.0f, 1.0f};   // Red error text
        warning_color = {1.0f, 1.0f, 0.0f, 1.0f}; // Yellow warning text
        
        // Initialize the text editor
        text_editor_.SetShowWhitespaces(false);
        text_editor_.SetTabSize(4);
        
        // Set up a dark theme for the editor
        auto lang = ::TextEditor::LanguageDefinition::CPlusPlus();
        text_editor_.SetLanguageDefinition(lang);
        
        // Use a dark palette
        ::TextEditor::Palette palette = text_editor_.GetDarkPalette();
        text_editor_.SetPalette(palette);
    }
    
    bool empty() const override {
        return !is_active_;
    }

    void clear() override {
        source_file_.clear();
        buffer_.clear();
        error_.clear();
        file_modified_ = false;
        save_message_.clear();
        ai_notification_message_.clear();
        is_active_ = false;
        show_diagnostics_drawer_ = false;
        clearDiagnostics();
        
        text_editor_.SetText("");
        text_editor_.SetLanguageDefinition(::TextEditor::LanguageDefinition::CPlusPlus());
    }

    void select(const std::string& uri) override {
        source_file_ = uri;
        is_active_ = true;
        
        // Set flag to focus the window on next render
        should_focus_ = true;
        
        buffer_.clear();
        error_.clear();
        clearDiagnostics();

        if (uri.empty()) {
            text_editor_.SetText("");
            text_editor_.SetLanguageDefinition(::TextEditor::LanguageDefinition::CPlusPlus());
            return;
        }
        
        // Handle as text file
        try {
            std::ifstream input{uri};
            if (!input) {
                throw std::runtime_error("Could not open file: " + uri);
            }
            
            // Read the entire file into buffer_ using a safer method to avoid GCC null pointer warnings
            input.seekg(0, std::ios::end);
            const auto file_size = input.tellg();
            input.seekg(0, std::ios::beg);
            
            if (file_size > 0) {
                buffer_.resize(static_cast<size_t>(file_size));
                input.read(buffer_.data(), file_size);
            } else {
                buffer_.clear();
            }
            
            // Set text in the TextEditor widget
            text_editor_.SetText(buffer_);
            
            // Try to detect language from file extension
            if (endsWithCaseInsensitive(uri, ".cpp") || endsWithCaseInsensitive(uri, ".h") || 
                endsWithCaseInsensitive(uri, ".hpp") || endsWithCaseInsensitive(uri, ".cc") ||
                endsWithCaseInsensitive(uri, ".cxx") || endsWithCaseInsensitive(uri, ".cppm")) {
                text_editor_.SetLanguageDefinition(::TextEditor::LanguageDefinition::CPlusPlus());
            } else if (endsWithCaseInsensitive(uri, ".c")) {
                text_editor_.SetLanguageDefinition(::TextEditor::LanguageDefinition::C());
            } else if (endsWithCaseInsensitive(uri, ".glsl") || endsWithCaseInsensitive(uri, ".frag") || 
                        endsWithCaseInsensitive(uri, ".vert")) {
                text_editor_.SetLanguageDefinition(::TextEditor::LanguageDefinition::GLSL());
            } else if (endsWithCaseInsensitive(uri, ".sql")) {
                text_editor_.SetLanguageDefinition(::TextEditor::LanguageDefinition::SQL());
            } else if (endsWithCaseInsensitive(uri, ".lua")) {
                text_editor_.SetLanguageDefinition(::TextEditor::LanguageDefinition::Lua());
            } else {
                text_editor_.SetLanguageDefinition(::TextEditor::LanguageDefinition::CPlusPlus());
            }

            // Automatically check syntax on open for source files
            runSyntaxCheckAsync();
        }
        catch(std::exception const &e) {
            error_ = e.what();
        }
    }

    bool saveFile() override {
        if (source_file_.empty()) {
            return false;
        }
        
        try {
            // Get text from the TextEditor widget
            buffer_ = text_editor_.GetText();
            
            std::ofstream output{source_file_};
            if (!output) {
                throw std::runtime_error("Could not open file for writing: " + source_file_);
            }
            
            output << buffer_;
            file_modified_ = false;
            save_message_ = "File saved successfully!";
            save_message_time_ = 3.0f; // Message will display for 3 seconds

            // Automatically re-check syntax on save
            runSyntaxCheckAsync();
            return true;
        }
        catch(std::exception const &e) {
            error_ = std::string("Error saving file: ") + e.what();
            return false;
        }
    }

    void setDiagnostics(const std::vector<rouen::helpers::Diagnostic>& diags) {
        diagnostics_ = diags;
        errors_count_ = 0;
        warnings_count_ = 0;
        ::TextEditor::ErrorMarkers markers;

        for (const auto& d : diagnostics_) {
            if (d.severity == "error" || d.severity == "fatal error") {
                errors_count_++;
            } else if (d.severity == "warning") {
                warnings_count_++;
            }
            if (d.line > 0 && markers.find(d.line) == markers.end()) {
                markers[d.line] = std::format("[{}] {}", d.severity, d.message);
            }
        }
        text_editor_.SetErrorMarkers(markers);
    }

    const std::vector<rouen::helpers::Diagnostic>& getDiagnostics() const {
        return diagnostics_;
    }

    void clearDiagnostics() {
        diagnostics_.clear();
        errors_count_ = 0;
        warnings_count_ = 0;
        text_editor_.SetErrorMarkers({});
    }

    int getErrorsCount() const { return errors_count_; }
    int getWarningsCount() const { return warnings_count_; }
    bool isCheckingSyntax() const { return is_checking_syntax_; }
    bool isDiagnosticsDrawerOpen() const { return show_diagnostics_drawer_; }
    void toggleDiagnosticsDrawer() { show_diagnostics_drawer_ = !show_diagnostics_drawer_; }
    void showDiagnosticsDrawer(bool show) { show_diagnostics_drawer_ = show; }

    void jumpToLine(int line) {
        if (line < 1) line = 1;
        int total = std::max(1, text_editor_.GetTotalLines());
        if (line > total) line = total;
        text_editor_.SetCursorPosition(::TextEditor::Coordinates(line - 1, 0));
        should_focus_ = true;
    }

    void jumpToNextDiagnostic() {
        if (diagnostics_.empty()) return;
        int cur_line = text_editor_.GetCursorPosition().mLine + 1;
        std::set<int> lines;
        for (const auto& d : diagnostics_) {
            if (d.line > 0) lines.insert(d.line);
        }
        if (lines.empty()) return;

        auto it = lines.upper_bound(cur_line);
        if (it == lines.end()) {
            it = lines.begin(); // Wrap around
        }
        jumpToLine(*it);
    }

    void jumpToPrevDiagnostic() {
        if (diagnostics_.empty()) return;
        int cur_line = text_editor_.GetCursorPosition().mLine + 1;
        std::set<int> lines;
        for (const auto& d : diagnostics_) {
            if (d.line > 0) lines.insert(d.line);
        }
        if (lines.empty()) return;

        auto it = lines.lower_bound(cur_line);
        if (it == lines.begin()) {
            it = std::prev(lines.end()); // Wrap around to end
        } else {
            --it;
        }
        jumpToLine(*it);
    }

    std::string getSurroundingCode(int target_line, int context_lines = 8) {
        std::string full_text = text_editor_.GetText();
        std::vector<std::string> lines;
        std::istringstream stream(full_text);
        std::string line_content;
        while (std::getline(stream, line_content)) {
            if (!line_content.empty() && line_content.back() == '\r') {
                line_content.pop_back();
            }
            lines.push_back(line_content);
        }

        if (lines.empty()) return "";

        int start_l = std::max(1, target_line - context_lines);
        int end_l = std::min(static_cast<int>(lines.size()), target_line + context_lines);

        std::string snippet;
        for (int i = start_l; i <= end_l; ++i) {
            snippet += std::format("{:4d} | {}\n", i, lines[static_cast<size_t>(i - 1)]);
        }
        return snippet;
    }

    std::string buildFixWithAIPrompt(int line, const rouen::helpers::Diagnostic* matched_diag = nullptr) {
        if (!matched_diag) {
            for (const auto& d : diagnostics_) {
                if (d.line == line) {
                    matched_diag = &d;
                    break;
                }
            }
        }
        if (!matched_diag && !diagnostics_.empty()) {
            matched_diag = &diagnostics_[0];
        }

        std::string filename = std::filesystem::path(source_file_).filename().string();
        if (filename.empty()) filename = "source file";

        std::string diag_msg = matched_diag ? matched_diag->message : "Compiler diagnostic on line";
        std::string severity = matched_diag ? matched_diag->severity : "error";
        int col = matched_diag ? matched_diag->column : 1;

        std::string snippet = getSurroundingCode(line, 8);

        return std::format(
            "Please investigate and fix the compiler diagnostic in `{}`:\n\n"
            "- **Target File**: `{}`\n"
            "- **Line**: {}, **Column**: {}\n"
            "- **Severity**: {}\n"
            "- **Compiler Diagnostic**: `{}`\n\n"
            "**Surrounding Code Context (Lines around {})**:\n"
            "```cpp\n"
            "{}"
            "```\n\n"
            "Please inspect the issue, explain the fix concisely, and apply the correction using `code_apply_patch`.\n"
            "After applying the patch, verify the fix using `code_check_syntax`.",
            filename,
            source_file_,
            line, col, severity, diag_msg,
            line, snippet
        );
    }

    void triggerFixWithAI(int line) {
        std::string prompt = buildFixWithAIPrompt(line);

        // Switch to Code & Git Architect persona for code repair
        rouen::helpers::PersonaManager::instance().select_persona_by_name("Code & Git Architect");

        // Send to active AI chat card if open, otherwise launch a new AI chat card
        auto send_fn = registrar::get<std::function<void(const std::string&)>>("ai_chat_send_message");
        if (send_fn && *send_fn) {
            (*send_fn)(prompt);
        } else {
            "create_card"_sfn(std::format("ai_chat:{}", ::helpers::StringHelper::url_encode(prompt)));
        }

        ai_notification_message_ = std::format("Dispatched Line {} diagnostic to AI Chat (Code & Git Architect)", line);
        ai_notification_time_ = 4.0f;
    }

    void runSyntaxCheckAsync() {
        if (source_file_.empty() || is_checking_syntax_) return;

        if (!endsWithCaseInsensitive(source_file_, ".cpp") && !endsWithCaseInsensitive(source_file_, ".c") &&
            !endsWithCaseInsensitive(source_file_, ".h") && !endsWithCaseInsensitive(source_file_, ".hpp") &&
            !endsWithCaseInsensitive(source_file_, ".cc") && !endsWithCaseInsensitive(source_file_, ".cxx") &&
            !endsWithCaseInsensitive(source_file_, ".cppm")) {
            return;
        }

        is_checking_syntax_ = true;
        std::string file_path = source_file_;
        std::thread([this, file_path]() {
            try {
                auto result = rouen::helpers::SyntaxChecker::instance().check_file(file_path);
                std::lock_guard<std::mutex> lock(syntax_mutex_);
                pending_syntax_result_ = result;
            } catch (...) {
                std::lock_guard<std::mutex> lock(syntax_mutex_);
                rouen::helpers::SyntaxCheckResult empty_res{};
                empty_res.file_path = file_path;
                empty_res.success = true;
                pending_syntax_result_ = empty_res;
            }
        }).detach();
    }

    void render() override {
        if (empty()) {
            return;
        }

        // Apply pending asynchronous syntax check results
        {
            std::lock_guard<std::mutex> lock(syntax_mutex_);
            if (pending_syntax_result_.has_value()) {
                setDiagnostics(pending_syntax_result_->diagnostics);
                pending_syntax_result_.reset();
                is_checking_syntax_ = false;
            }
        }

        ImGuiIO& io = ImGui::GetIO();
        float const dt = io.DeltaTime;
        if (save_message_time_ > 0.0f) {
            save_message_time_ -= dt;
            if (save_message_time_ <= 0.0f) save_message_.clear();
        }
        if (ai_notification_time_ > 0.0f) {
            ai_notification_time_ -= dt;
            if (ai_notification_time_ <= 0.0f) ai_notification_message_.clear();
        }

        if (!error_.empty()) {
            ImGui::TextColored(error_color, "Error: %s", error_.c_str());
            return;
        }

        rouen::fonts::with_font fnt{rouen::fonts::FontType::Mono};
        ImGui::Separator();

        // Calculate available area and reserve space for bottom status bar and optional diagnostics drawer
        float const status_bar_h = 26.0f;
        ImVec2 total_avail = ImGui::GetContentRegionAvail();
        if (total_avail.y < 120.0f) {
            float window_h = ImGui::GetWindowSize().y;
            float cursor_y = ImGui::GetCursorPosY();
            total_avail.y = std::max(120.0f, window_h - cursor_y - ImGui::GetStyle().WindowPadding.y);
        }

        float drawer_h = 0.0f;
        if (show_diagnostics_drawer_) {
            drawer_h = std::min(160.0f, std::max(90.0f, total_avail.y * 0.35f));
        }

        ImVec2 editor_avail = total_avail;
        editor_avail.y = std::max(50.0f, total_avail.y - status_bar_h - drawer_h - 4.0f);

        if (should_focus_) {
            ImGui::SetNextWindowFocus();
            should_focus_ = false;
        }

        // Keyboard shortcuts for diagnostics
        auto ctrl = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;
        int current_cursor_line = text_editor_.GetCursorPosition().mLine + 1;

        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
            // Alt+Enter / Cmd+Enter: Fix with AI for current line
            if (ImGui::IsKeyPressed(ImGuiKey_Enter) && (io.KeyAlt || io.KeyCtrl)) {
                triggerFixWithAI(current_cursor_line);
            }
            // F4: Next diagnostic
            if (ImGui::IsKeyPressed(ImGuiKey_F4) && !io.KeyShift) {
                jumpToNextDiagnostic();
            }
            // Shift+F4: Previous diagnostic
            if (ImGui::IsKeyPressed(ImGuiKey_F4) && io.KeyShift) {
                jumpToPrevDiagnostic();
            }
            // Cmd+E / Ctrl+E: Toggle diagnostics drawer
            if (ImGui::IsKeyPressed(ImGuiKey_E) && ctrl) {
                toggleDiagnosticsDrawer();
            }
            // F7 or Cmd+Shift+B: Check syntax now
            if (ImGui::IsKeyPressed(ImGuiKey_F7) || (ImGui::IsKeyPressed(ImGuiKey_B) && ctrl && io.KeyShift)) {
                runSyntaxCheckAsync();
            }
        }

        // Render the TextEditor widget
        text_editor_.Render("##editor", editor_avail, true);

        // Track text changes
        if (text_editor_.IsTextChanged()) {
            file_modified_ = true;
        }

        // Detect right-click in editor to open context menu
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            ImVec2 mouse_pos = ImGui::GetMousePos();
            auto coord = text_editor_.ScreenPosToCoordinates(mouse_pos);
            context_menu_line_ = coord.mLine + 1;
            ImGui::OpenPopup("EditorContextMenu");
        }

        // Render Context Menu
        renderContextMenu();

        // Render Diagnostics Drawer (if toggled open)
        if (show_diagnostics_drawer_) {
            renderDiagnosticsDrawer(drawer_h);
        }

        // Render Editor Footer / Status Bar
        renderStatusBar();
    }

    bool shouldFocus() const {
        return should_focus_;
    }

    void resetFocus() {
        should_focus_ = false;
    }

    bool isModified() const override {
        return file_modified_;
    }

    // Expose text editor methods
    bool canUndo() const { return text_editor_.CanUndo(); }
    bool canRedo() const { return text_editor_.CanRedo(); }
    bool hasSelection() const { return text_editor_.HasSelection(); }
    void undo() { text_editor_.Undo(); }
    void redo() { text_editor_.Redo(); }
    void cut() { text_editor_.Cut(); }
    void copy() { text_editor_.Copy(); }
    void paste() { text_editor_.Paste(); }
    void selectAll() { 
        text_editor_.SetSelection(
            ::TextEditor::Coordinates(), 
            ::TextEditor::Coordinates(text_editor_.GetTotalLines(), 0)
        ); 
    }
    bool isShowingWhitespaces() const { return text_editor_.IsShowingWhitespaces(); }
    void setShowWhitespaces(bool show) { text_editor_.SetShowWhitespaces(show); }
    std::string getText() const { return text_editor_.GetText(); }
    const std::string& getSourceFile() const { return source_file_; }
    void setErrorMarkers(const ::TextEditor::ErrorMarkers& markers) { text_editor_.SetErrorMarkers(markers); }
    void clearErrorMarkers() { clearDiagnostics(); }
    const ::TextEditor::ErrorMarkers& getErrorMarkers() const { return text_editor_.GetErrorMarkers(); }
    int getCurrentLine() const { return text_editor_.GetCursorPosition().mLine + 1; }
    int getCurrentColumn() const { return text_editor_.GetCursorPosition().mColumn + 1; }

private:
    void renderContextMenu() {
        if (ImGui::BeginPopup("EditorContextMenu")) {
            int line = (context_menu_line_ > 0) ? context_menu_line_ : getCurrentLine();
            
            // Look for diagnostic on the clicked / cursor line
            const rouen::helpers::Diagnostic* line_diag = nullptr;
            for (const auto& d : diagnostics_) {
                if (d.line == line) {
                    line_diag = &d;
                    break;
                }
            }

            if (line_diag) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.85f, 1.0f, 1.0f));
                std::string fix_label = std::format("⚡ Fix with AI (Line {})", line);
                if (ImGui::MenuItem(fix_label.c_str(), "Alt+Enter")) {
                    triggerFixWithAI(line);
                }
                ImGui::PopStyleColor();

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                std::string short_msg = line_diag->message;
                if (short_msg.length() > 40) short_msg = short_msg.substr(0, 37) + "...";
                ImGui::TextDisabled("Issue: %s", short_msg.c_str());
                ImGui::PopStyleColor();

                if (ImGui::MenuItem("Copy Diagnostic Message")) {
                    ImGui::SetClipboardText(line_diag->message.c_str());
                }
                ImGui::Separator();
            }

            if (ImGui::MenuItem("Check Syntax Now", "F7")) {
                runSyntaxCheckAsync();
            }

            if (!diagnostics_.empty()) {
                if (ImGui::MenuItem("Next Issue", "F4")) {
                    jumpToNextDiagnostic();
                }
                if (ImGui::MenuItem("Previous Issue", "Shift+F4")) {
                    jumpToPrevDiagnostic();
                }
                bool drawer_open = show_diagnostics_drawer_;
                if (ImGui::MenuItem("Diagnostics Drawer", "Cmd+E", &drawer_open)) {
                    show_diagnostics_drawer_ = drawer_open;
                }
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Undo", "Cmd+Z", nullptr, canUndo())) undo();
            if (ImGui::MenuItem("Redo", "Cmd+Y", nullptr, canRedo())) redo();
            ImGui::Separator();
            if (ImGui::MenuItem("Cut", "Cmd+X", nullptr, hasSelection())) cut();
            if (ImGui::MenuItem("Copy", "Cmd+C", nullptr, hasSelection())) copy();
            if (ImGui::MenuItem("Paste", "Cmd+V")) paste();
            if (ImGui::MenuItem("Select All", "Cmd+A")) selectAll();

            ImGui::EndPopup();
        }
    }

    void renderDiagnosticsDrawer(float height) {
        ImGui::Separator();
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
        if (ImGui::BeginChild("DiagnosticsDrawer", ImVec2(0, height), true)) {
            // Header bar of the drawer
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.95f, 1.0f, 1.0f));
            ImGui::Text("Diagnostics (%zu issues)", diagnostics_.size());
            ImGui::PopStyleColor();

            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_MD_REFRESH " Re-check")) {
                runSyntaxCheckAsync();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_MD_CLOSE " Close")) {
                show_diagnostics_drawer_ = false;
            }

            ImGui::Separator();

            if (diagnostics_.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.85f, 0.4f, 1.0f));
                ImGui::Text(ICON_MD_CHECK " No compiler diagnostics reported.");
                ImGui::PopStyleColor();
            } else {
                for (size_t i = 0; i < diagnostics_.size(); ++i) {
                    const auto& d = diagnostics_[i];
                    ImGui::PushID(static_cast<int>(i));

                    // Severity Badge
                    if (d.severity == "error" || d.severity == "fatal error") {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
                        ImGui::Text("[ERROR]");
                    } else if (d.severity == "warning") {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.2f, 1.0f));
                        ImGui::Text("[WARN ]");
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.7f, 0.8f, 1.0f));
                        ImGui::Text("[NOTE ]");
                    }
                    ImGui::PopStyleColor();

                    // Line navigation button
                    ImGui::SameLine();
                    std::string line_btn = std::format("Ln {}:{}", d.line, d.column);
                    if (ImGui::SmallButton(line_btn.c_str())) {
                        jumpToLine(d.line);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Click to jump to line %d", d.line);
                    }

                    // Message text
                    ImGui::SameLine();
                    ImGui::TextWrapped("%s", d.message.c_str());

                    // Fix with AI button (right aligned)
                    ImGui::SameLine(ImGui::GetWindowWidth() - 110.0f);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.32f, 0.48f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.42f, 0.64f, 1.0f));
                    if (ImGui::SmallButton(ICON_MD_AUTO_AWESOME " Fix with AI")) {
                        jumpToLine(d.line);
                        triggerFixWithAI(d.line);
                    }
                    ImGui::PopStyleColor(2);

                    ImGui::PopID();
                }
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    void renderStatusBar() {
        ImGui::Separator();
        float avail_w = ImGui::GetContentRegionAvail().x;
        int cur_line = getCurrentLine();
        int cur_col = getCurrentColumn();

        // 1. Cursor position
        ImGui::TextDisabled("Ln %d, Col %d", cur_line, cur_col);
        ImGui::SameLine();

        // 2. Diagnostic Status Pill
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
        if (is_checking_syntax_) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.3f, 0.4f, 0.7f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.85f, 1.0f, 1.0f));
            ImGui::SmallButton(ICON_MD_REFRESH " Checking...");
            ImGui::PopStyleColor(2);
        } else if (diagnostics_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.3f, 0.15f, 0.6f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
            if (ImGui::SmallButton(ICON_MD_CHECK " Clean")) {
                runSyntaxCheckAsync();
            }
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("No compiler errors or warnings. Click to re-run syntax check.");
            }
        } else {
            // Issues exist
            ImVec4 btn_col = (errors_count_ > 0) ? ImVec4(0.45f, 0.15f, 0.15f, 0.8f) : ImVec4(0.4f, 0.35f, 0.15f, 0.8f);
            ImVec4 txt_col = (errors_count_ > 0) ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) : ImVec4(1.0f, 0.85f, 0.3f, 1.0f);

            ImGui::PushStyleColor(ImGuiCol_Button, btn_col);
            ImGui::PushStyleColor(ImGuiCol_Text, txt_col);
            std::string pill_label = std::format("{} {} err, {} warn", 
                (errors_count_ > 0 ? ICON_MD_ERROR : ICON_MD_WARNING), errors_count_, warnings_count_);
            if (ImGui::SmallButton(pill_label.c_str())) {
                toggleDiagnosticsDrawer();
            }
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Click to toggle diagnostics drawer (%zu total issues)", diagnostics_.size());
            }

            // Quick navigation buttons
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_MD_ARROW_DROP_UP "##prev_diag")) {
                jumpToPrevDiagnostic();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Previous Issue (Shift+F4)");

            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_MD_ARROW_DROP_DOWN "##next_diag")) {
                jumpToNextDiagnostic();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Next Issue (F4)");

            // Check if current line has an issue
            bool cur_line_has_issue = false;
            for (const auto& d : diagnostics_) {
                if (d.line == cur_line) {
                    cur_line_has_issue = true;
                    break;
                }
            }
            if (cur_line_has_issue) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.7f, 0.9f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.55f, 0.85f, 1.0f));
                if (ImGui::SmallButton(ICON_MD_AUTO_AWESOME " Fix with AI")) {
                    triggerFixWithAI(cur_line);
                }
                ImGui::PopStyleColor(2);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fix current line error with AI (Alt+Enter)");
            }
        }
        ImGui::PopStyleVar();

        // 3. Notification messages
        if (!ai_notification_message_.empty()) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.85f, 1.0f, 1.0f));
            ImGui::Text("• %s", ai_notification_message_.c_str());
            ImGui::PopStyleColor();
        } else if (!save_message_.empty()) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
            ImGui::Text("• %s", save_message_.c_str());
            ImGui::PopStyleColor();
        }

        // Right-aligned file details
        if (text_editor_.GetTotalLines() > 0) {
            std::string lines_info = std::format("{} lines", text_editor_.GetTotalLines());
            float info_w = ImGui::CalcTextSize(lines_info.c_str()).x;
            if (avail_w - info_w > ImGui::GetCursorPosX() + 20.0f) {
                ImGui::SameLine(avail_w - info_w);
                ImGui::TextDisabled("%s", lines_info.c_str());
            }
        }
    }

    std::string source_file_;
    std::string buffer_;
    std::string error_;
    bool file_modified_ = false;
    std::string save_message_;
    float save_message_time_ = 0.0f;
    std::string ai_notification_message_;
    float ai_notification_time_ = 0.0f;
    bool should_focus_ = false;  // Flag to track when the window should grab focus
    bool is_active_ = false;     // Flag to track whether a document is currently active
    
    // Diagnostics & Quick-Fix state
    std::vector<rouen::helpers::Diagnostic> diagnostics_;
    int errors_count_{0};
    int warnings_count_{0};
    std::atomic<bool> is_checking_syntax_{false};
    std::mutex syntax_mutex_;
    std::optional<rouen::helpers::SyntaxCheckResult> pending_syntax_result_{std::nullopt};
    bool show_diagnostics_drawer_{false};
    int context_menu_line_{0};

    // Text editor
    ::TextEditor text_editor_;

    // Color variables
    ImVec4 success_color;
    ImVec4 error_color;
    ImVec4 warning_color;
};

} // namespace editor
} // namespace rouen
