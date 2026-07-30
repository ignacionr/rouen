#include "metadata_serialization.hpp"
#include <glaze/json/read.hpp>
#include <glaze/json/write.hpp>
#include <map>
#include <string>
#include <vector>

namespace mail {
    bool serialize_tags(const std::vector<std::string>& tags, std::string& out_json) {
        auto result = glz::write_json(tags, out_json);
        return !result; // Return true if no error occurred
    }
    bool deserialize_tags(const char* json, std::vector<std::string>& tags) {
        if (!json || !*json) return false;
        auto res = glz::read_json(tags, json);
        return !res;
    }
    bool serialize_action_links(const std::map<std::string, std::string>& links, std::string& out_json) {
        auto result = glz::write_json(links, out_json);
        return !result; // Return true if no error occurred
    }
    bool deserialize_action_links(const char* json, std::map<std::string, std::string>& links) {
        if (!json || !*json) return false;
        auto res = glz::read_json(links, json);
        return !res;
    }
}
