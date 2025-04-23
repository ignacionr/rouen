#pragma once

#include "../../editor/editor.hpp"

// This is an adapter class that forwards to the new editor implementation
class editor {
public:
    editor() {
        // Create the new editor implementation
        editor_ = std::make_unique<rouen::editor::Editor>();
    }

    virtual ~editor() = default;

    bool empty() const {
        return editor_->empty();
    }

    void clear() {
        editor_->clear();
    }

    void select(const std::string& uri) {
        editor_->select(uri);
    }

    bool saveFile() {
        return editor_->saveFile();
    }

    void render() {
        editor_->render();
    }

private:
    std::unique_ptr<rouen::editor::Editor> editor_;
};