#pragma once

#include <string>
#include <vector>

namespace mail {
    // Define a struct for email metadata that can be serialized/deserialized with Glaze
    struct EmailMetadata {
        std::string id = "unknown";
        int urgency = 0;
        std::string category = "unprocessed";
        std::string summary = "";
        std::vector<std::string> tags = {"unprocessed"};
    };
}