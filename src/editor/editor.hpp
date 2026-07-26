#pragma once

#include <memory>
#include <string>
#include <algorithm>
#include <cctype>

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
                    if (target == "editor" || target.empty()) {
                        return take_snapshot(path, w, h);
                    }
                    try {
                        auto card_fn = registrar::get<std::function<std::string(const std::string&, const std::string&, int, int)>>("card_save_snapshot");
                        if (card_fn && *card_fn) {
                            return (*card_fn)(target, path, w, h);
                        }
                    } catch (...) {}
                    return std::string(R"({"success":false,"error":"Unknown screenshot target"})");
                }
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
        } catch (...) {}
    }

    std::string take_snapshot(const std::string& filepath, int width = 800, int height = 600) {
        SDL_GPUDevice* device = nullptr;
        try {
            auto device_ptr = registrar::get<SDL_GPUDevice*>("main_gpu_device");
            if (device_ptr && *device_ptr) {
                device = *device_ptr;
            }
        } catch (...) {}

        auto render_fn = [this, width, height]() {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(static_cast<float>(width), static_cast<float>(height)));
            this->render();
        };

        RouenGPUTexture* snapshot_texture = rouen::helpers::capture_imgui(
            width, height, render_fn, device
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

        std::string target_path = filepath.empty() ? "/tmp/editor_snapshot.png" : filepath;
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
            if (ImGui::IsKeyPressed(ImGuiKey_W) && ctrl) {
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
                }
                
                ImGui::EndMenu();
            }
            
            // Display file path / modification status in the menu bar (right-aligned)
            if (active_editor_ && !active_editor_->empty()) {
                TextEditor* textEditor = dynamic_cast<TextEditor*>(active_editor_);
                
                float menuWidth = ImGui::GetWindowWidth() - 150.0f;
                ImGui::SameLine(menuWidth);
                
                if (textEditor) {
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
