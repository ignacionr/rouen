#pragma once

#include <fstream>
#include <string>

#include <imgui/imgui.h>

#include "../registrar.hpp"

class editor {
public:
    editor() {
        registrar::add<std::function<void(std::string const &)>>(
            "edit",
            std::make_shared<std::function<void(std::string const &)>>(
                [this](std::string const &uri) { select(uri); }
            )
        );
    }
    virtual ~editor() = default;

    void select(auto const &uri) {
        source_file_ = uri;
        try {
            std::ifstream input{uri};
            if (!input) {
                throw std::runtime_error("Could not open file: " + uri);
            }
            
            buffer_.clear();
            error_.clear();
            
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

    void render() {
        if (ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoResize)) {
            ImGui::TextUnformatted(source_file_.c_str());
            
            if (!error_.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Error: %s", error_.c_str());
            }
            else if (!buffer_.empty()) {
                // Create a child window for the text editor
                ImGui::Separator();
                
                // Add a text input area with the file contents
                static ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput;
                ImVec2 size = ImGui::GetContentRegionAvail();
                
                // Input text multiline takes the buffer directly
                if (ImGui::InputTextMultiline("##editor", buffer_.data(), buffer_.size(), ImGui::GetContentRegionAvail())) {
                    // Handle text changes here if needed
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
};