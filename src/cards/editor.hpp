#pragma once

#include <fstream>
#include <string>
#include <algorithm>
#include <cctype>

#include <imgui/imgui.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include "../registrar.hpp"
#include "../helpers/texture_helper.hpp"

class editor {
public:
    editor() 
        : image_texture_(nullptr)
        , image_width_(0)
        , image_height_(0) 
    {
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
    }
    
    virtual ~editor() {
        if (image_texture_) {
            SDL_DestroyTexture(image_texture_);
            image_texture_ = nullptr;
        }
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

    void select(auto const &uri) {
        source_file_ = uri;
        
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
        if (ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoResize)) {
            ImGui::TextUnformatted(source_file_.c_str());
            
            // Add save button if we have a text file loaded
            if (!source_file_.empty() && !isImageFile(source_file_)) {
                ImGui::SameLine(ImGui::GetWindowWidth() - 100); // Position the button to the right
                if (ImGui::Button("Save")) {
                    saveFile();
                }
                
                // Display the save message if it exists and the timer hasn't expired
                if (save_message_time_ > 0.0f) {
                    save_message_time_ -= ImGui::GetIO().DeltaTime; // Decrease timer
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%s", save_message_.c_str());
                    
                    if (save_message_time_ <= 0.0f) {
                        save_message_.clear();
                    }
                }
            }
            
            if (!error_.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Error: %s", error_.c_str());
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
            else if (!buffer_.empty()) {
                // Create a child window for the text editor
                ImGui::Separator();
                
                // Add a text input area with the file contents
                static ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput;
                ImVec2 size = ImGui::GetContentRegionAvail();
                
                // Input text multiline takes the buffer directly
                buffer_.reserve(buffer_.size() * 5/4);
                if (ImGui::InputTextMultiline("##editor", buffer_.data(), buffer_.capacity(), ImGui::GetContentRegionAvail())) {
                    // Mark as modified when the text changes
                    buffer_ = buffer_.data();
                    file_modified_ = true;
                }
            }
            else if (!source_file_.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "No content loaded");
            }
        }
        ImGui::End();
    }

private:
    std::string source_file_;
    std::string buffer_;
    std::string error_;
    bool file_modified_ = false;
    std::string save_message_;
    float save_message_time_ = 0.0f;
    
    // Image handling
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* image_texture_ = nullptr;
    int image_width_ = 0;
    int image_height_ = 0;
};