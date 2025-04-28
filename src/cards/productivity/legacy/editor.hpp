#pragma once

#include <fstream>
#include <string>
#include <algorithm>
#include <cctype>

#include "imgui.h"
#include <SDL.h>
#include <SDL_image.h>

// ImGuiColorTextEdit include
#include <TextEditor.h>

#include "../../registrar.hpp"
#include "../../helpers/texture_helper.hpp"

#include "../../fonts.hpp"

class editor {
public:
    editor() 
        : image_texture_(nullptr)
        , image_width_(0)
        , image_height_(0)
        , should_focus_(false)
    {
        // Initialize colors
        success_color = {0.0f, 1.0f, 0.0f, 1.0f}; // Green success text
        error_color = {1.0f, 0.0f, 0.0f, 1.0f};   // Red error text
        warning_color = {1.0f, 1.0f, 0.0f, 1.0f}; // Yellow warning text

        registrar::add<std::function<void(std::string const &)>>(
            "edit",
            std::make_shared<std::function<void(std::string const &)>>(
                [this](std::string const &uri) { select(uri); }
            )
        );
        
        try {
            // Get the SDL renderer from the registrar
            renderer_ = *registrar::get<SDL_Renderer*>("main_renderer");
        } catch (const std::exception& e) {
            // No renderer available
            renderer_ = nullptr;
        }
        
        // Initialize the text editor
        text_editor_.SetShowWhitespaces(false);
        text_editor_.SetTabSize(4);
        
        // Set up a dark theme for the editor
        auto lang = TextEditor::LanguageDefinition::CPlusPlus();
        text_editor_.SetLanguageDefinition(lang);
        
        // Use a dark palette
        TextEditor::Palette palette = text_editor_.GetDarkPalette();
        text_editor_.SetPalette(palette);
    }
    
    virtual ~editor() {
        if (image_texture_) {
            SDL_DestroyTexture(image_texture_);
            image_texture_ = nullptr;
        }
    }

    bool empty() const {
        return source_file_.empty();
    }

    // Helper to check if a string ends with a specific suffix (case insensitive)
    static bool endsWithCaseInsensitive(const std::string& str, const std::string& suffix) {
        if (str.length() < suffix.length()) {
            return false;
        }
        
        return std::equal(
            suffix.rbegin(), suffix.rend(), str.rbegin(),
            [](char a, char b) {
                return std::tolower(a) == std::tolower(b);
            }
        );
    }
    
    // Check if file is an image based on extension
    static bool isImageFile(const std::string& filename) {
        return endsWithCaseInsensitive(filename, ".png") ||
               endsWithCaseInsensitive(filename, ".jpg") ||
               endsWithCaseInsensitive(filename, ".jpeg");
    }

    void clear() {
        source_file_.clear();
        buffer_.clear();
        error_.clear();
        file_modified_ = false;
        
        if (image_texture_) {
            SDL_DestroyTexture(image_texture_);
            image_texture_ = nullptr;
            image_width_ = 0;
            image_height_ = 0;
        }
        
        text_editor_.SetText("");
        text_editor_.SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
    }

    void select(auto const &uri) {
        source_file_ = uri;
        
        // Set flag to focus the window on next render
        should_focus_ = true;
        
        // Clear previous resources
        if (image_texture_) {
            SDL_DestroyTexture(image_texture_);
            image_texture_ = nullptr;
            image_width_ = 0;
            image_height_ = 0;
        }
        
        buffer_.clear();
        error_.clear();
        
        // Check if this is an image file
        if (isImageFile(uri)) {
            if (renderer_) {
                // Load image as texture
                image_texture_ = TextureHelper::loadTextureFromFile(
                    renderer_,
                    uri.c_str(),
                    image_width_,
                    image_height_
                );
                
                if (!image_texture_) {
                    error_ = "Failed to load image: " + std::string(SDL_GetError());
                }
            } else {
                error_ = "Cannot load image: SDL renderer not available";
            }
        } else {
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
                    text_editor_.SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
                } else if (endsWithCaseInsensitive(uri, ".c")) {
                    text_editor_.SetLanguageDefinition(TextEditor::LanguageDefinition::C());
                } else if (endsWithCaseInsensitive(uri, ".glsl") || endsWithCaseInsensitive(uri, ".frag") || 
                           endsWithCaseInsensitive(uri, ".vert")) {
                    text_editor_.SetLanguageDefinition(TextEditor::LanguageDefinition::GLSL());
                } else if (endsWithCaseInsensitive(uri, ".sql")) {
                    text_editor_.SetLanguageDefinition(TextEditor::LanguageDefinition::SQL());
                } else if (endsWithCaseInsensitive(uri, ".lua")) {
                    text_editor_.SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
                } else {
                    text_editor_.SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
                }
            }
            catch(std::exception const &e) {
                error_ = e.what();
            }
        }
    }

    bool saveFile() {
        if (source_file_.empty() || isImageFile(source_file_)) {
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

    void render() {
        if (ImGui::IsKeyPressed(ImGuiKey_W) && ImGui::GetIO().KeyCtrl) {
            clear();
        }

        // Push a custom style for this window to have square corners
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        
        // Set focus to this window if requested
        if (should_focus_) {
            ImGui::SetNextWindowFocus();
            should_focus_ = false; // Reset the flag after using it
        }
        
        if (ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_MenuBar)) {
            // Add a menu bar with standard options
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("New", "Ctrl+N")) {
                        clear();
                    }
                    
                    if (ImGui::MenuItem("Open...", "Ctrl+O")) {
                        "create_card"_sfn("dir");
                    }
                    
                    ImGui::Separator();
                    
                    if (ImGui::MenuItem("Save", "Ctrl+S", nullptr, !source_file_.empty() && !isImageFile(source_file_))) {
                        saveFile();
                    }
                    
                    if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", nullptr, !source_file_.empty() && !isImageFile(source_file_))) {
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
                    bool hasSelection = !source_file_.empty() && !isImageFile(source_file_) && text_editor_.HasSelection();
                    
                    if (ImGui::MenuItem("Undo", "Ctrl+Z", nullptr, !source_file_.empty() && !isImageFile(source_file_) && text_editor_.CanUndo())) {
                        text_editor_.Undo();
                    }
                    
                    if (ImGui::MenuItem("Redo", "Ctrl+Y", nullptr, !source_file_.empty() && !isImageFile(source_file_) && text_editor_.CanRedo())) {
                        text_editor_.Redo();
                    }
                    
                    ImGui::Separator();
                    
                    if (ImGui::MenuItem("Cut", "Ctrl+X", nullptr, hasSelection)) {
                        text_editor_.Cut();
                    }
                    
                    if (ImGui::MenuItem("Copy", "Ctrl+C", nullptr, hasSelection)) {
                        text_editor_.Copy();
                    }
                    
                    if (ImGui::MenuItem("Paste", "Ctrl+V", nullptr, !source_file_.empty() && !isImageFile(source_file_))) {
                        text_editor_.Paste();
                    }
                    
                    if (ImGui::MenuItem("Select All", "Ctrl+A", nullptr, !source_file_.empty() && !isImageFile(source_file_))) {
                        text_editor_.SetSelection(TextEditor::Coordinates(), TextEditor::Coordinates(text_editor_.GetTotalLines(), 0));
                    }
                    
                    ImGui::EndMenu();
                }
                
                if (ImGui::BeginMenu("View")) {
                    bool showWhitespaces = text_editor_.IsShowingWhitespaces();
                    if (ImGui::MenuItem("Show Whitespaces", nullptr, &showWhitespaces)) {
                        text_editor_.SetShowWhitespaces(showWhitespaces);
                    }
                    
                    ImGui::EndMenu();
                }
                
                // Display file path in the menu bar (right-aligned)
                float menuWidth = ImGui::GetWindowWidth() - 150.0f;
                if (!source_file_.empty()) {
                    ImGui::SameLine(menuWidth);
                    
                    // Display a truncated version of the path if it's too long
                    std::string displayPath = source_file_;
                    if (displayPath.length() > 40) {
                        displayPath = "..." + displayPath.substr(displayPath.length() - 37);
                    }
                    
                    ImGui::Text("%s", displayPath.c_str());
                    
                    // Show modified indicator
                    if (file_modified_) {
                        ImGui::SameLine();
                        ImGui::TextColored(warning_color, "*");
                    }
                }
                
                ImGui::EndMenuBar();
            }
            
            if (!error_.empty()) {
                ImGui::TextColored(error_color, "Error: %s", error_.c_str());
            }
            else if (image_texture_) {
                // Display the image
                ImGui::Separator();
                
                // Calculate the display size while maintaining aspect ratio
                ImVec2 content_size = ImGui::GetContentRegionAvail();
                float aspect_ratio = static_cast<float>(image_width_) / static_cast<float>(image_height_);
                
                ImVec2 display_size;
                if (content_size.x / aspect_ratio <= content_size.y) {
                    // Width constrained
                    display_size = ImVec2(content_size.x, content_size.x / aspect_ratio);
                } else {
                    // Height constrained
                    display_size = ImVec2(content_size.y * aspect_ratio, content_size.y);
                }
                
                // Center the image in the available space
                ImVec2 pos = ImGui::GetCursorPos();
                pos.x += (content_size.x - display_size.x) * 0.5f;
                ImGui::SetCursorPos(pos);
                
                // Draw the image
                ImGui::Image((ImTextureID)(intptr_t)image_texture_, display_size);
            }
            else if (!source_file_.empty()) {
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
            else if (!source_file_.empty()) {
                ImGui::TextColored(warning_color, "No content loaded");
            }
        }
        ImGui::End();
        
        // Restore the original style
        ImGui::PopStyleVar();
    }

private:
    std::string source_file_;
    std::string buffer_;
    std::string error_;
    bool file_modified_ = false;
    std::string save_message_;
    float save_message_time_ = 0.0f;
    bool should_focus_ = false;  // Flag to track when the window should grab focus
    
    // Text editor
    TextEditor text_editor_;
    
    // Image handling
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* image_texture_ = nullptr;
    int image_width_ = 0;
    int image_height_ = 0;

    // Color variables
    ImVec4 success_color;
    ImVec4 error_color;
    ImVec4 warning_color;
};