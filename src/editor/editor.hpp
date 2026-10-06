#pragma once

#include <memory>
#include <string>
#include <algorithm>
#include <cctype>
#include <filesystem>

#include "../helpers/imgui_include.hpp"
#include "../helpers/sdl_compat.hpp"

#include <format>
#include <SDL3_image/SDL_image.h>

#include "../registrar.hpp"
#include "../helpers/capture_helper.hpp"
#include "../helpers/texture_helper.hpp"
#include "editor_interface.hpp"
#include "text_editor.hpp"
#include "image_editor.hpp"
#include "../../external/IconsMaterialDesign.h"

namespace rouen {
namespace editor {

class Editor {
public:
    enum class PendingAction { None, Close, New, Open, SelectFile };

    Editor() {
        // Initialize the sub-editors
        text_editor_ = std::make_unique<TextEditor>();
        image_editor_ = std::make_unique<ImageEditor>();
        
        // Register with the global registrar
        registrar::add<std::function<void(std::string const &)>>(
            "edit",
            std::make_shared<std::function<void(std::string const &)>>(
                [this](std::string const &uri) { select(uri); }
            )
        );

        registrar::add<std::function<void()>>(
            "clear_editor",
            std::make_shared<std::function<void()>>(
                [this]() { requestClose(); }
            )
        );

        registrar::add<std::function<bool()>>(
            "is_editor_empty",
            std::make_shared<std::function<bool()>>(
                [this]() { return empty(); }
            )
        );

        registrar::add<std::function<std::string(const std::string&, int, int)>>(
            "editor_save_snapshot",
            std::make_shared<std::function<std::string(const std::string&, int, int)>>(
                [this](const std::string& path, int w, int h) { return take_snapshot(path, w, h); }
            )
        );

        registrar::add<std::function<std::string(const std::string&, const std::string&, int, int)>>(
            "take_screenshot",
            std::make_shared<std::function<std::string(const std::string&, const std::string&, int, int)>>(
                [this](const std::string& target, const std::string& path, int w, int h) {
                    if (target == "editor" && active_editor_ && !active_editor_->empty()) {
                        return take_snapshot(path, w, h);
                    }
                    try {
                        auto card_fn = registrar::get<std::function<std::string(const std::string&, const std::string&, int, int)>>("card_save_snapshot");
                        if (card_fn && *card_fn) {
                            return (*card_fn)(target, path, w, h);
                        }
                    } catch (...) {}
                    return take_app_snapshot(path, w, h);
                }
            )
        );

        registrar::add<std::function<void(const std::map<int, std::string>&)>>(
            "editor_set_error_markers",
            std::make_shared<std::function<void(const std::map<int, std::string>&)>>(
                [this](const std::map<int, std::string>& markers) { setErrorMarkers(markers); }
            )
        );

        registrar::add<std::function<void()>>(
            "editor_clear_error_markers",
            std::make_shared<std::function<void()>>(
                [this]() { clearErrorMarkers(); }
            )
        );

        registrar::add<std::function<std::string()>>(
            "editor_get_active_file",
            std::make_shared<std::function<std::string()>>(
                [this]() { return getActiveFile(); }
            )
        );

        registrar::add<std::function<std::string()>>(
            "editor_get_text",
            std::make_shared<std::function<std::string()>>(
                [this]() { return getText(); }
            )
        );

        registrar::add<std::function<void(int)>>(
            "editor_jump_to_line",
            std::make_shared<std::function<void(int)>>(
                [this](int line) { jumpToLine(line); }
            )
        );

        registrar::add<std::function<void(int)>>(
            "editor_fix_with_ai",
            std::make_shared<std::function<void(int)>>(
                [this](int line) { triggerFixWithAI(line); }
            )
        );

        registrar::add<std::function<void()>>(
            "editor_check_syntax",
            std::make_shared<std::function<void()>>(
                [this]() { runSyntaxCheckAsync(); }
            )
        );

        registrar::add<std::function<void(const std::vector<rouen::helpers::Diagnostic>&)>>(
            "editor_set_diagnostics",
            std::make_shared<std::function<void(const std::vector<rouen::helpers::Diagnostic>&)>>(
                [this](const std::vector<rouen::helpers::Diagnostic>& diags) { setDiagnostics(diags); }
            )
        );

        registrar::add<std::function<void(bool)>>(
            "editor_show_drawer",
            std::make_shared<std::function<void(bool)>>(
                [this](bool show) { setDiagnosticsDrawer(show); }
            )
        );

        registrar::add<std::function<void()>>(
            "editor_toggle_drawer",
            std::make_shared<std::function<void()>>(
                [this]() { toggleDiagnosticsDrawer(); }
            )
        );
    }
    
    virtual ~Editor() {
        try {
            registrar::remove<std::function<void(std::string const &)>>("edit");
            registrar::remove<std::function<void()>>("clear_editor");
            registrar::remove<std::function<bool()>>("is_editor_empty");
            registrar::remove<std::function<std::string(const std::string&, int, int)>>("editor_save_snapshot");
            registrar::remove<std::function<std::string(const std::string&, const std::string&, int, int)>>("take_screenshot");
            registrar::remove<std::function<void(const std::map<int, std::string>&)>>("editor_set_error_markers");
            registrar::remove<std::function<void()>>("editor_clear_error_markers");
            registrar::remove<std::function<std::string()>>("editor_get_active_file");
            registrar::remove<std::function<std::string()>>("editor_get_text");
            registrar::remove<std::function<void(int)>>("editor_jump_to_line");
            registrar::remove<std::function<void(int)>>("editor_fix_with_ai");
            registrar::remove<std::function<void()>>("editor_check_syntax");
            registrar::remove<std::function<void(const std::vector<rouen::helpers::Diagnostic>&)>>("editor_set_diagnostics");
            registrar::remove<std::function<void(bool)>>("editor_show_drawer");
            registrar::remove<std::function<void()>>("editor_toggle_drawer");
        } catch (...) {}
    }

    std::string take_app_snapshot(const std::string& filepath, int width = 0, int height = 0) {
        SDL_GPUDevice* device = nullptr;
        try {
            auto device_ptr = registrar::get<SDL_GPUDevice*>("main_gpu_device");
            if (device_ptr && *device_ptr) {
                device = *device_ptr;
            }
        } catch (...) {}

        ImGuiIO& io = ImGui::GetIO();
        int capture_w = (width > 0) ? width : static_cast<int>(io.DisplaySize.x);
        int capture_h = (height > 0) ? height : static_cast<int>(io.DisplaySize.y);
        if (capture_w <= 0) capture_w = 800;
        if (capture_h <= 0) capture_h = 600;

        RouenGPUTexture* snapshot_texture = rouen::helpers::capture_imgui(
            capture_w, capture_h, nullptr, device
        );

        if (!snapshot_texture) {
            return R"({"success":false,"error":"Failed to create snapshot texture"})";
        }

        SDL_Surface* surface = rouen::helpers::download_gpu_texture(
            device, snapshot_texture, capture_w, capture_h
        );

        if (!surface) {
            TextureHelper::destroyTexture(snapshot_texture);
            return R"({"success":false,"error":"Failed to download GPU texture to surface"})";
        }

        std::string target_path = filepath;
        if (target_path.empty()) {
            std::error_code ec;
            auto temp_dir = std::filesystem::temp_directory_path(ec);
            target_path = (ec ? std::filesystem::path("snapshot.png") : (temp_dir / "snapshot.png")).string();
        }
        bool saved = IMG_SavePNG(surface, target_path.c_str());
        SDL_DestroySurface(surface);
        TextureHelper::destroyTexture(snapshot_texture);

        if (saved) {
            return std::format(R"({{"success":true,"message":"Application snapshot saved","file":"{}","width":{},"height":{}}})",
                target_path, capture_w, capture_h);
        } else {
            return std::format(R"({{"success":false,"error":"Failed to save PNG: {}"}})", SDL_GetError());
        }
    }

    std::string take_snapshot(const std::string& filepath, int width = 800, int height = 600) {
        int capture_w = (width > 0) ? width : 1200;
        int capture_h = (height > 0) ? height : 800;
        SDL_GPUDevice* device = nullptr;
        try {
            auto device_ptr = registrar::get<SDL_GPUDevice*>("main_gpu_device");
            if (device_ptr && *device_ptr) {
                device = *device_ptr;
            }
        } catch (...) {}

        auto render_fn = [this, capture_w, capture_h]() {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(static_cast<float>(capture_w), static_cast<float>(capture_h)));
            this->render();
        };

        RouenGPUTexture* snapshot_texture = rouen::helpers::capture_imgui(
            capture_w, capture_h, render_fn, device
        );

        if (!snapshot_texture) {
            return R"({"success":false,"error":"Failed to create snapshot texture"})";
        }

        SDL_Surface* surface = rouen::helpers::download_gpu_texture(
            device, snapshot_texture, width, height
        );

        if (!surface) {
            TextureHelper::destroyTexture(snapshot_texture);
            return R"({"success":false,"error":"Failed to download GPU texture to surface"})";
        }

        std::string target_path = filepath;
        if (target_path.empty()) {
            std::error_code ec;
            auto temp_dir = std::filesystem::temp_directory_path(ec);
            target_path = (ec ? std::filesystem::path("editor_snapshot.png") : (temp_dir / "editor_snapshot.png")).string();
        }
        bool saved = IMG_SavePNG(surface, target_path.c_str());
        SDL_DestroySurface(surface);
        TextureHelper::destroyTexture(snapshot_texture);

        if (saved) {
            return std::format(R"({{"success":true,"message":"Editor snapshot saved","file":"{}","width":{},"height":{}}})",
                target_path, width, height);
        } else {
            return std::format(R"({{"success":false,"error":"Failed to save PNG: {}"}})", SDL_GetError());
        }
    }

    bool empty() const {
        return active_editor_ == nullptr || active_editor_->empty();
    }

    std::string getText() const {
        if (text_editor_) {
            return text_editor_->getText();
        }
        return "";
    }

    std::string getActiveFile() const {
        if (text_editor_) {
            return text_editor_->getSourceFile();
        }
        return "";
    }

    void setErrorMarkers(const ::TextEditor::ErrorMarkers& markers) {
        if (text_editor_) {
            text_editor_->setErrorMarkers(markers);
        }
    }

    void clearErrorMarkers() {
        if (text_editor_) {
            text_editor_->clearErrorMarkers();
        }
    }

    const ::TextEditor::ErrorMarkers& getErrorMarkers() const {
        static const ::TextEditor::ErrorMarkers empty_markers;
        if (text_editor_) {
            return text_editor_->getErrorMarkers();
        }
        return empty_markers;
    }

    void setDiagnostics(const std::vector<rouen::helpers::Diagnostic>& diags) {
        if (text_editor_) {
            text_editor_->setDiagnostics(diags);
        }
    }

    void jumpToLine(int line) {
        if (text_editor_) {
            text_editor_->jumpToLine(line);
        }
    }

    void triggerFixWithAI(int line) {
        if (text_editor_) {
            text_editor_->triggerFixWithAI(line);
        }
    }

    void runSyntaxCheckAsync() {
        if (text_editor_) {
            text_editor_->runSyntaxCheckAsync();
        }
    }

    void jumpToNextDiagnostic() {
        if (text_editor_) {
            text_editor_->jumpToNextDiagnostic();
        }
    }

    void jumpToPrevDiagnostic() {
        if (text_editor_) {
            text_editor_->jumpToPrevDiagnostic();
        }
    }

    void toggleDiagnosticsDrawer() {
        if (text_editor_) {
            text_editor_->toggleDiagnosticsDrawer();
        }
    }

    void setDiagnosticsDrawer(bool show) {
        if (text_editor_) {
            text_editor_->showDiagnosticsDrawer(show);
        }
    }


    void clear() {
        if (text_editor_) text_editor_->clear();
        if (image_editor_) image_editor_->clear();
        active_editor_ = nullptr;
        pending_action_ = PendingAction::None;
        pending_uri_.clear();
        show_confirm_modal_ = false;
    }

    void requestClose() {
        if (active_editor_ && active_editor_->isModified()) {
            pending_action_ = PendingAction::Close;
            show_confirm_modal_ = true;
        } else {
            clear();
        }
    }

    void requestNew() {
        if (active_editor_ && active_editor_->isModified()) {
            pending_action_ = PendingAction::New;
            show_confirm_modal_ = true;
        } else {
            clear();
            text_editor_->select("");
            active_editor_ = text_editor_.get();
        }
    }

    void requestOpen() {
        if (active_editor_ && active_editor_->isModified()) {
            pending_action_ = PendingAction::Open;
            show_confirm_modal_ = true;
        } else {
            "create_card"_sfn("dir");
        }
    }

    void select(const std::string& uri) {
        if (active_editor_ && active_editor_->isModified()) {
            pending_action_ = PendingAction::SelectFile;
            pending_uri_ = uri;
            show_confirm_modal_ = true;
        } else {
            doSelect(uri);
        }
    }

    bool saveFile() {
        if (active_editor_) {
            return active_editor_->saveFile();
        }
        return false;
    }

    void render() {
        // Handle Ctrl+S / Cmd+S (Save) and Ctrl+W / Cmd+W (Close)
        auto& io = ImGui::GetIO();
        auto ctrl = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;

        if (active_editor_ && !active_editor_->empty()) {
            if (ImGui::IsKeyPressed(ImGuiKey_S) && ctrl) {
                saveFile();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_W) && ctrl && !io.KeyAlt && !io.KeyShift) {
                requestClose();
            }
        }

        // Push a custom style for this window to have square corners
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        
        // Set focus to this window if requested
        if (active_editor_) {
            if (dynamic_cast<TextEditor*>(active_editor_) && 
                static_cast<TextEditor*>(active_editor_)->shouldFocus()) {
                ImGui::SetNextWindowFocus();
                static_cast<TextEditor*>(active_editor_)->resetFocus();
            } else if (dynamic_cast<ImageEditor*>(active_editor_) && 
                       static_cast<ImageEditor*>(active_editor_)->shouldFocus()) {
                ImGui::SetNextWindowFocus();
                static_cast<ImageEditor*>(active_editor_)->resetFocus();
            }
        }
        
        if (ImGui::Begin("Editor", nullptr, 
            ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_MenuBar|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoNavInputs)) {
            // Add a menu bar with standard options
            renderMenuBar();
            
            // Render the active editor content
            if (active_editor_) {
                active_editor_->render();
            }
        }
        ImGui::End();
        
        // Restore the original style
        ImGui::PopStyleVar();

        // Render confirmation modal if unsaved changes exist
        renderConfirmModal();
    }

private:
    void doSelect(const std::string& uri) {
        if (isImageFile(uri)) {
            image_editor_->select(uri);
            active_editor_ = image_editor_.get();
        } else {
            text_editor_->select(uri);
            active_editor_ = text_editor_.get();
        }
    }

    void executePendingAction() {
        PendingAction action = pending_action_;
        std::string uri = pending_uri_;
        pending_action_ = PendingAction::None;
        pending_uri_.clear();

        switch (action) {
            case PendingAction::Close:
                clear();
                break;
            case PendingAction::New:
                clear();
                text_editor_->select("");
                active_editor_ = text_editor_.get();
                break;
            case PendingAction::Open:
                clear();
                "create_card"_sfn("dir");
                break;
            case PendingAction::SelectFile:
                clear();
                doSelect(uri);
                break;
            case PendingAction::None:
                break;
        }
    }

    void renderConfirmModal() {
        if (show_confirm_modal_) {
            ImGui::OpenPopup("Unsaved Changes##EditorConfirmModal");
            show_confirm_modal_ = false;
        }

        if (ImGui::IsPopupOpen("Unsaved Changes##EditorConfirmModal")) {
            ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

            if (ImGui::BeginPopupModal("Unsaved Changes##EditorConfirmModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("The current file has unsaved changes.\nDo you want to save your changes before proceeding?");
                ImGui::Separator();
                ImGui::Spacing();

                if (ImGui::Button("Save", ImVec2(100, 0))) {
                    saveFile();
                    executePendingAction();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Don't Save", ImVec2(100, 0))) {
                    executePendingAction();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(100, 0))) {
                    pending_action_ = PendingAction::None;
                    pending_uri_.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
    }

    void renderMenuBar() {
        auto& io = ImGui::GetIO();
        const char* modStr = io.ConfigMacOSXBehaviors ? "Cmd+" : "Ctrl+";
        std::string newShortcut = std::string(modStr) + "N";
        std::string openShortcut = std::string(modStr) + "O";
        std::string saveShortcut = std::string(modStr) + "S";
        std::string saveAsShortcut = std::string(modStr) + "Shift+S";
        std::string closeShortcut = std::string(modStr) + "W";

        std::string undoShortcut = std::string(modStr) + "Z";
        std::string redoShortcut = std::string(modStr) + "Y";
        std::string cutShortcut = std::string(modStr) + "X";
        std::string copyShortcut = std::string(modStr) + "C";
        std::string pasteShortcut = std::string(modStr) + "V";
        std::string selectAllShortcut = std::string(modStr) + "A";

        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New", newShortcut.c_str())) {
                    requestNew();
                }
                
                if (ImGui::MenuItem("Open...", openShortcut.c_str())) {
                    requestOpen();
                }
                
                ImGui::Separator();
                
                bool isTextEditorActive = dynamic_cast<TextEditor*>(active_editor_) != nullptr;
                
                if (ImGui::MenuItem("Save", saveShortcut.c_str(), nullptr, active_editor_ && isTextEditorActive)) {
                    saveFile();
                }
                
                if (ImGui::MenuItem("Save As...", saveAsShortcut.c_str(), nullptr, active_editor_ && isTextEditorActive)) {
                    saveFile();
                }
                
                ImGui::Separator();
                
                if (ImGui::MenuItem("Close", closeShortcut.c_str())) {
                    requestClose();
                }
                
                ImGui::EndMenu();
            }
            
            if (ImGui::BeginMenu("Edit")) {
                TextEditor* textEditor = dynamic_cast<TextEditor*>(active_editor_);
                bool hasTextEditor = textEditor != nullptr;
                
                bool hasSelection = hasTextEditor && textEditor->hasSelection();
                
                if (ImGui::MenuItem("Undo", undoShortcut.c_str(), nullptr, hasTextEditor && textEditor->canUndo())) {
                    textEditor->undo();
                }
                
                if (ImGui::MenuItem("Redo", redoShortcut.c_str(), nullptr, hasTextEditor && textEditor->canRedo())) {
                    textEditor->redo();
                }
                
                ImGui::Separator();
                
                if (ImGui::MenuItem("Cut", cutShortcut.c_str(), nullptr, hasSelection)) {
                    textEditor->cut();
                }
                
                if (ImGui::MenuItem("Copy", copyShortcut.c_str(), nullptr, hasSelection)) {
                    textEditor->copy();
                }
                
                if (ImGui::MenuItem("Paste", pasteShortcut.c_str(), nullptr, hasTextEditor)) {
                    textEditor->paste();
                }
                
                if (ImGui::MenuItem("Select All", selectAllShortcut.c_str(), nullptr, hasTextEditor)) {
                    textEditor->selectAll();
                }
                
                ImGui::EndMenu();
            }
            
            if (ImGui::BeginMenu("View")) {
                TextEditor* textEditor = dynamic_cast<TextEditor*>(active_editor_);
                if (textEditor) {
                    bool showWhitespaces = textEditor->isShowingWhitespaces();
                    if (ImGui::MenuItem("Show Whitespaces", nullptr, &showWhitespaces)) {
                        textEditor->setShowWhitespaces(showWhitespaces);
                    }
                    const std::string& src = textEditor->getSourceFile();
                    if (endsWithCaseInsensitive(src, ".md") || endsWithCaseInsensitive(src, ".markdown")) {
                        if (ImGui::MenuItem("View in Markdown Viewer")) {
                            "create_card"_sfn(std::format("markdown:{}", src));
                        }
                    }
                }
                
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Tools")) {
                TextEditor* textEditor = dynamic_cast<TextEditor*>(active_editor_);
                bool hasTextEditor = textEditor != nullptr;

                if (ImGui::MenuItem("Check Syntax", "F7", nullptr, hasTextEditor)) {
                    if (textEditor) textEditor->runSyntaxCheckAsync();
                }
                if (ImGui::MenuItem("Next Issue", "F4", nullptr, hasTextEditor && !textEditor->getDiagnostics().empty())) {
                    if (textEditor) textEditor->jumpToNextDiagnostic();
                }
                if (ImGui::MenuItem("Previous Issue", "Shift+F4", nullptr, hasTextEditor && !textEditor->getDiagnostics().empty())) {
                    if (textEditor) textEditor->jumpToPrevDiagnostic();
                }
                if (ImGui::MenuItem("Fix with AI", "Alt+Enter", nullptr, hasTextEditor)) {
                    if (textEditor) textEditor->triggerFixWithAI(textEditor->getCurrentLine());
                }
                ImGui::Separator();
                if (hasTextEditor) {
                    bool drawer_open = textEditor->isDiagnosticsDrawerOpen();
                    if (ImGui::MenuItem("Diagnostics Drawer", "Cmd+E", &drawer_open)) {
                        textEditor->toggleDiagnosticsDrawer();
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Review Diff (Git HEAD)", "Cmd+D", nullptr, hasTextEditor)) {
                    if (textEditor && !textEditor->getSourceFile().empty()) {
                        "create_card"_sfn(std::format("diff:{}", textEditor->getSourceFile()));
                    } else {
                        "create_card"_sfn("diff");
                    }
                }
                if (ImGui::MenuItem("Staging & Visual Diff Buffer", "Cmd+Shift+D", nullptr)) {
                    "create_card"_sfn("diff:staged");
                }
                auto undo_fn = registrar::try_get<std::function<bool()>>("code_editor_undo");
                auto can_undo_fn = registrar::try_get<std::function<bool()>>("code_editor_can_undo");
                bool can_undo_val = (can_undo_fn && *can_undo_fn && (*can_undo_fn)());
                if (ImGui::MenuItem("Undo Last Code Edit", "Cmd+Z (Disk)", nullptr, can_undo_val)) {
                    if (undo_fn && *undo_fn) (*undo_fn)();
                }

                auto redo_fn = registrar::try_get<std::function<bool()>>("code_editor_redo");
                auto can_redo_fn = registrar::try_get<std::function<bool()>>("code_editor_can_redo");
                bool can_redo_val = (can_redo_fn && *can_redo_fn && (*can_redo_fn)());
                if (ImGui::MenuItem("Redo Last Code Edit", "Cmd+Y (Disk)", nullptr, can_redo_val)) {
                    if (redo_fn && *redo_fn) (*redo_fn)();
                }
                ImGui::EndMenu();
            }
            
            // Display diagnostics and file modification status in the menu bar (right-aligned)
            if (active_editor_ && !active_editor_->empty()) {
                TextEditor* textEditor = dynamic_cast<TextEditor*>(active_editor_);
                if (textEditor) {
                    float right_section_w = 320.0f;
                    float right_pos = std::max(ImGui::GetCursorPosX() + 10.0f, ImGui::GetWindowWidth() - right_section_w);
                    ImGui::SameLine(right_pos);

                    // Status Pill
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
                    if (textEditor->isCheckingSyntax()) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.85f, 1.0f, 1.0f));
                        ImGui::TextDisabled(ICON_MD_REFRESH " Checking...");
                        ImGui::PopStyleColor();
                    } else if (textEditor->getDiagnostics().empty()) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
                        ImGui::Text(ICON_MD_CHECK " Clean");
                        ImGui::PopStyleColor();
                    } else {
                        int errs = textEditor->getErrorsCount();
                        int warns = textEditor->getWarningsCount();
                        std::string badge = std::format("{} {}e, {}w", (errs > 0 ? ICON_MD_ERROR : ICON_MD_WARNING), errs, warns);
                        if (ImGui::SmallButton(badge.c_str())) {
                            textEditor->toggleDiagnosticsDrawer();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Click to toggle diagnostics drawer (%zu total)", textEditor->getDiagnostics().size());
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton(ICON_MD_ARROW_DROP_UP)) textEditor->jumpToPrevDiagnostic();
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Previous Issue (Shift+F4)");
                        ImGui::SameLine();
                        if (ImGui::SmallButton(ICON_MD_ARROW_DROP_DOWN)) textEditor->jumpToNextDiagnostic();
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Next Issue (F4)");
                    }

                    ImGui::SameLine();
                    if (ImGui::SmallButton(ICON_MD_COMPARE " Diff")) {
                        if (!textEditor->getSourceFile().empty()) {
                            "create_card"_sfn(std::format("diff:{}", textEditor->getSourceFile()));
                        } else {
                            "create_card"_sfn("diff");
                        }
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Review visual diff with Git HEAD (Cmd+D)");

                    ImGui::PopStyleVar();

                    ImGui::SameLine();
                    if (textEditor->isModified()) {
                        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "* Modified");
                    } else {
                        ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.5f, 1.0f), "Saved");
                    }
                }
            }
            
            ImGui::EndMenuBar();
        }
    }

    std::unique_ptr<TextEditor> text_editor_;
    std::unique_ptr<ImageEditor> image_editor_;
    EditorInterface* active_editor_ = nullptr;

    PendingAction pending_action_ = PendingAction::None;
    std::string pending_uri_;
    bool show_confirm_modal_ = false;
};

} // namespace editor
} // namespace rouen
