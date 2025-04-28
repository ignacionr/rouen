#pragma once

#include <fstream>
#include <string>
#include "imgui.h"
#include <TextEditor.h>

#include "editor_interface.hpp"
#include "../fonts.hpp"

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
        return source_file_.empty();
    }

    void clear() override {
        source_file_.clear();
        buffer_.clear();
        error_.clear();
        file_modified_ = false;
        save_message_.clear();
        
        text_editor_.SetText("");
        text_editor_.SetLanguageDefinition(::TextEditor::LanguageDefinition::CPlusPlus());
    }

    void select(const std::string& uri) override {
        source_file_ = uri;
        
        // Set flag to focus the window on next render
        should_focus_ = true;
        
        buffer_.clear();
        error_.clear();
        
        // Handle as text file
        try {
            std::ifstream input{uri};
            if (!input) {
                throw std::runtime_error("Could not open file: " + uri);
            }
            
            // Read the entire file into buffer_
            buffer_ = std::string(
                std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>()
            );
            
            // Set text in the TextEditor widget
            text_editor_.SetText(buffer_);
            
            // Try to detect language from file extension
            if (endsWithCaseInsensitive(uri, ".cpp") || endsWithCaseInsensitive(uri, ".h") || 
                endsWithCaseInsensitive(uri, ".hpp") || endsWithCaseInsensitive(uri, ".cc")) {
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
            return true;
        }
        catch(std::exception const &e) {
            error_ = std::string("Error saving file: ") + e.what();
            return false;
        }
    }

    void render() override {
        if (empty()) {
            return;
        }

        if (!error_.empty()) {
            ImGui::TextColored(error_color, "Error: %s", error_.c_str());
        }
        else {
            rouen::fonts::with_font fnt{rouen::fonts::FontType::Mono};
            // Create a child window for the text editor
            ImGui::Separator();
            
            // Render the TextEditor widget
            text_editor_.Render("##editor", ImGui::GetContentRegionAvail(), true);
            
            // Update the modified flag
            if (text_editor_.IsTextChanged()) {
                file_modified_ = true;
            }
        }
    }

    bool shouldFocus() const {
        return should_focus_;
    }

    void resetFocus() {
        should_focus_ = false;
    }

    bool isModified() const {
        return file_modified_;
    }

    // Expose text editor methods that might be needed by parent classes
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

private:
    std::string source_file_;
    std::string buffer_;
    std::string error_;
    bool file_modified_ = false;
    std::string save_message_;
    float save_message_time_ = 0.0f;
    bool should_focus_ = false;  // Flag to track when the window should grab focus
    
    // Text editor
    ::TextEditor text_editor_;

    // Color variables
    ImVec4 success_color;
    ImVec4 error_color;
    ImVec4 warning_color;
};

} // namespace editor
} // namespace rouen