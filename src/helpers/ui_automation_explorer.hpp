#pragma once

#include <string>
#include <vector>
#include <utility>
#include <memory>
#include <optional>
#include <cstdint>

namespace rouen::helpers {

    struct ui_element_attribute {
        std::string name;
        std::string value;
    };

    struct ui_element_node {
        std::string id;             // AutomationID, AXIdentifier, or synthesized ID
        std::string name;           // Title / Name / Text label
        std::string role;           // AXRole or UIA ControlType (e.g., "Window", "Button", "Text", "Group", "Slider", etc.)
        std::string subrole;        // AXSubrole or ClassName / LocalizedControlType
        std::string description;    // Help/Description string if available
        std::string value;          // Current value (for sliders, text fields, check boxes)
        
        float x{0.0f};              // Bounding box position X
        float y{0.0f};              // Bounding box position Y
        float width{0.0f};          // Bounding box width
        float height{0.0f};         // Bounding box height
        
        bool enabled{true};
        bool focused{false};

        std::vector<ui_element_attribute> attributes; // Detailed key-value pairs
        std::vector<ui_element_node> children;        // Child UI elements
    };

    struct ui_element_value_info {
        std::string id;
        std::string name;
        std::string role;
        std::string subrole;
        std::string value;
        std::string description;
    };

    struct ui_automation_result {
        bool success{false};
        bool permission_denied{false}; // e.g. macOS Accessibility permission missing
        std::string error_message;
        ui_element_node root;
        size_t total_node_count{0};

        // Flattens and extracts text fields / edit boxes / element values in the UI hierarchy
        std::vector<ui_element_value_info> extract_values(bool edit_boxes_only = false) const;
    };

    class ui_automation_explorer {
    public:
        // Captures/inspects the Accessibility / UI Automation tree for a given process PID
        static ui_automation_result inspect_process(int64_t pid, int max_depth = 6, int max_children_per_node = 100);
        
        // Convenience method to directly extract all texts and values from a process's UI controls (e.g. edit boxes)
        static std::vector<ui_element_value_info> extract_process_values(int64_t pid, bool edit_boxes_only = true, int max_depth = 8);

        // Requests the text/value of a specific control in a process by ID or Name
        static std::optional<std::string> request_control_value(int64_t pid, std::string_view identifier_or_name);

        // Checks if accessibility permissions are granted (macOS specific, returns true on Windows)
        static bool check_accessibility_permissions(bool prompt_if_missing = false);
    };

} // namespace rouen::helpers
