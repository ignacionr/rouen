#include "metadata_serialization.hpp"

namespace mail {
    bool serialize_tags(const std::vector<std::string>& tags, std::string& out_json) {
        glz::write_json(tags, out_json);
        return true;
    }
    bool deserialize_tags(const char* json, std::vector<std::string>& tags) {
        if (!json || !*json) return false;
        auto res = glz::read_json(tags, json);
        return !res;
    }
    bool serialize_action_links(const std::map<std::string, std::string>& links, std::string& out_json) {
        glz::write_json(links, out_json);
        return true;
    }
    bool deserialize_action_links(const char* json, std::map<std::string, std::string>& links) {
        if (!json || !*json) return false;
        auto res = glz::read_json(links, json);
        return !res;
    }
}
