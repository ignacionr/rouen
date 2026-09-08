module;

#include <map>
#include <string>
#include <vector>

export module rouen.models.mail.metadata_serialization;

export namespace mail {
    bool serialize_tags(const std::vector<std::string>& tags, std::string& out_json);
    bool deserialize_tags(const char* json, std::vector<std::string>& tags);
    bool serialize_action_links(const std::map<std::string, std::string>& links, std::string& out_json);
    bool deserialize_action_links(const char* json, std::map<std::string, std::string>& links);
}
