#pragma once

#include <memory>
#include <string>
#include <algorithm>
#include <cctype>

#include <imgui/imgui.h>
#include <SDL2/SDL.h>

#include "../registrar.hpp"
#include "editor_interface.hpp"
#include "text_editor.hpp"
#include "image_editor.hpp"

namespace rouen {
namespace editor {

class Editor {
public:
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
    }
    
    virtual ~Editor() = default;

    bool empty() const {
        return active_editor_ == nullptr || active_editor_->empty();
    }

    void clear() {
        if (text_editor_) text_editor_->clear();
        if (image_editor_) image_editor_->clear();
        active_editor_ = nullptr;
    }

    void select(const std::string& uri) {
        // Determine which editor to use based on file type
        if (isImageFile(uri)) {
            image_editor_->select(uri);
            active_editor_ = image_editor_.get();
        } else {
            text_editor_->select(uri);
            active_editor_ = text_editor_.get();
        }
    }

    bool saveFile() {
        if (active_editor_) {
            return active_editor_->saveFile();
        }
        return false;
    }

    void render() {
        // Handle Ctrl+W to close the editor
        if (ImGui::IsKeyPressed(ImGuiKey_W) && ImGui::GetIO().KeyCtrl) {
            clear();
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
        
        if (ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_MenuBar)) {
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
    }

private:
    void renderMenuBar() {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New", "Ctrl+N")) {
                    clear();
                }
                
                if (ImGui::MenuItem("Open...", "Ctrl+O")) {
                    "create_card"_sfn("dir");
                }
                
                ImGui::Separator();
                
                bool isTextEditorActive = dynamic_cast<TextEditor*>(active_editor_) != nullptr;
                
                if (ImGui::MenuItem("Save", "Ctrl+S", nullptr, active_editor_ && isTextEditorActive)) {
                    saveFile();
                }
                
                if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", nullptr, active_editor_ && isTextEditorActive)) {
                    // In a real implementation, this would open a save dialog
                    // For now just save to the current file
                    saveFile();
                }
                
                ImGui::Separator();
                
                if (ImGui::MenuItem("Close", "Ctrl+W")) {
                    clear();
                }
                
                ImGui::EndMenu();
            }
            
            if (ImGui::BeginMenu("Edit")) {
                TextEditor* textEditor = dynamic_cast<TextEditor*>(active_editor_);
                bool hasTextEditor = textEditor != nullptr;
                
                bool hasSelection = hasTextEditor && textEditor->hasSelection();
                
                if (ImGui::MenuItem("Undo", "Ctrl+Z", nullptr, hasTextEditor && textEditor->canUndo())) {
                    textEditor->undo();
                }
                
                if (ImGui::MenuItem("Redo", "Ctrl+Y", nullptr, hasTextEditor && textEditor->canRedo())) {
                    textEditor->redo();
                }
                
                ImGui::Separator();
                
                if (ImGui::MenuItem("Cut", "Ctrl+X", nullptr, hasSelection)) {
                    textEditor->cut();
                }
                
                if (ImGui::MenuItem("Copy", "Ctrl+C", nullptr, hasSelection)) {
                    textEditor->copy();
                }
                
                if (ImGui::MenuItem("Paste", "Ctrl+V", nullptr, hasTextEditor)) {
                    textEditor->paste();
                }
                
                if (ImGui::MenuItem("Select All", "Ctrl+A", nullptr, hasTextEditor)) {
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
            
            // Display file path in the menu bar (right-aligned)
            if (active_editor_ && !active_editor_->empty()) {
                TextEditor* textEditor = dynamic_cast<TextEditor*>(active_editor_);
                
                float menuWidth = ImGui::GetWindowWidth() - 150.0f;
                ImGui::SameLine(menuWidth);
                
                // Get the file path from the active editor
                std::string displayPath = "";
                if (textEditor && textEditor->isModified()) {
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "*");
                    ImGui::SameLine();
                }
            }
            
            ImGui::EndMenuBar();
        }
    }

    std::unique_ptr<TextEditor> text_editor_;
    std::unique_ptr<ImageEditor> image_editor_;
    EditorInterface* active_editor_ = nullptr;
};

} // namespace editor
} // namespace rouen