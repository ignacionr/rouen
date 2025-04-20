#pragma once

#include <string>

#include <imgui/imgui.h>

#include "../registrar.hpp"

class editor {
public:
    editor() {
        registrar::add<std::function<void(std::string const &)>>(
            "edit",
            std::make_shared<std::function<void(std::string const &)>>(
                [this](std::string const &uri) { source_file_ = uri; }
            )
        );
    }
    virtual ~editor() = default;

    void render(const ImVec2& position, const ImVec2& size) {
        ImGui::SetNextWindowPos(position, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        if (ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoResize)) {
            ImGui::TextUnformatted(source_file_.c_str());
        }
        ImGui::End();
    }

private:
    std::string source_file_;
};