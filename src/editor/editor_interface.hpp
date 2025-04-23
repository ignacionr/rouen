#pragma once

#include <string>

namespace rouen {
namespace editor {

// Base interface for all editors
class EditorInterface {
public:
    virtual ~EditorInterface() = default;
    
    // Check if editor has no file loaded
    virtual bool empty() const = 0;
    
    // Clear and reset the editor state
    virtual void clear() = 0;
    
    // Select a file to edit
    virtual void select(const std::string& uri) = 0;
    
    // Save the current file (if applicable)
    virtual bool saveFile() = 0;
    
    // Render the editor UI
    virtual void render() = 0;
};

// Helper to check if a string ends with a specific suffix (case insensitive)
inline bool endsWithCaseInsensitive(const std::string& str, const std::string& suffix) {
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
inline bool isImageFile(const std::string& filename) {
    return endsWithCaseInsensitive(filename, ".png") ||
           endsWithCaseInsensitive(filename, ".jpg") ||
           endsWithCaseInsensitive(filename, ".jpeg");
}

} // namespace editor
} // namespace rouen