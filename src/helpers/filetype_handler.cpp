#include "filetype_handler.hpp"

#include <fstream>
#include <iterator>
#include <sstream>

#include "debug.hpp"
#include "platform_utils.hpp"

namespace rouen::helpers {

    namespace {
        constexpr const char* kConfigFileName = "filetype_rules.json";

        std::filesystem::path config_path() {
            return rouen::platform::get_user_config_directory() / kConfigFileName;
        }

        std::string trim(const std::string& s) {
            auto first = s.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return "";
            auto last = s.find_last_not_of(" \t\r\n");
            return s.substr(first, last - first + 1);
        }
    }

    FiletypeHandler& FiletypeHandler::instance() {
        static FiletypeHandler handler;
        return handler;
    }

    FiletypeHandler::FiletypeHandler() {
        if (std::filesystem::exists(config_path())) {
            load_rules();
        } else {
            setup_default_rules();
        }
    }

    void FiletypeHandler::setup_default_rules() {
        rules_.clear();
        rules_.push_back({"CMakeLists.txt", "cmake:{}", true});
        rules_.push_back({".pdf,.PDF", "pdf:{}", false});
        rules_.push_back({".png,.jpg,.jpeg,.bmp,.gif,.webp,.tif,.tiff,.tga,.avif,.jxl,.svg,.ico,.cur,"
                           ".pnm,.pbm,.pgm,.ppm,.xpm,.xcf,.qoi,.lbm,.pcx",
                           "image:{}", false});
        rules_.push_back({".mp4,.mkv,.avi,.mov,.webm,.mp3,.wav,.aac,.flac,.ogg,.m4a,.wma,.m4v,.mpg,.mpeg,.3gp,.opus",
                           "media:{}", false});
        save_rules();
    }

    void FiletypeHandler::load_rules() {
        auto path = config_path();
        if (!std::filesystem::exists(path)) {
            setup_default_rules();
            return;
        }

        try {
            std::ifstream file(path);
            if (!file.is_open()) {
                setup_default_rules();
                return;
            }

            std::string json_str((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            file.close();

            FiletypeHandlerSaveModel save_model;
            auto err = glz::read_json(save_model, json_str);
            if (err) {
                LOG_COMPONENT("FiletypeHandler", LOG_LEVEL_ERROR, "Failed to parse filetype_rules.json");
                setup_default_rules();
                return;
            }

            rules_ = save_model.rules;
            if (rules_.empty()) {
                setup_default_rules();
            }
        } catch (const std::exception& e) {
            LOG_COMPONENT("FiletypeHandler", LOG_LEVEL_ERROR, std::string("Error loading filetype rules: ") + e.what());
            setup_default_rules();
        }
    }

    void FiletypeHandler::save_rules() const {
        try {
            FiletypeHandlerSaveModel save_model;
            save_model.rules = rules_;

            std::string json_str;
            auto err = glz::write_json(save_model, json_str);
            if (err) {
                LOG_COMPONENT("FiletypeHandler", LOG_LEVEL_ERROR, "Failed to serialize filetype rules");
                return;
            }

            std::ofstream file(config_path());
            if (file.is_open()) {
                file << json_str;
                file.close();
            }
        } catch (const std::exception& e) {
            LOG_COMPONENT("FiletypeHandler", LOG_LEVEL_ERROR, std::string("Error saving filetype rules: ") + e.what());
        }
    }

    bool FiletypeHandler::validate_uri_format(const std::string& uri_format) {
        auto pos = uri_format.find("{}");
        if (pos == std::string::npos) return false;
        return uri_format.find("{}", pos + 2) == std::string::npos;
    }

    std::vector<std::string> FiletypeHandler::split_patterns(const std::string& match) {
        std::vector<std::string> patterns;
        std::stringstream ss(match);
        std::string token;
        while (std::getline(ss, token, ',')) {
            std::string trimmed = trim(token);
            if (!trimmed.empty()) {
                patterns.push_back(trimmed);
            }
        }
        return patterns;
    }

    bool FiletypeHandler::matches_pattern(const std::string& pattern, const std::string& filename, const std::string& extension) {
        if (pattern.starts_with('.')) {
            return pattern == extension;
        }
        return pattern == filename;
    }

    std::string FiletypeHandler::format_uri(const std::string& uri_format, const std::string& path_str) {
        auto pos = uri_format.find("{}");
        if (pos == std::string::npos) return uri_format;
        return uri_format.substr(0, pos) + path_str + uri_format.substr(pos + 2);
    }

    std::optional<std::string> FiletypeHandler::resolve(const std::filesystem::path& path, bool ctrl_held) const {
        std::string const filename = path.filename().string();
        std::string const extension = path.extension().string();

        for (const auto& rule : rules_) {
            if (rule.requires_ctrl && !ctrl_held) continue;

            for (const auto& pattern : split_patterns(rule.match)) {
                if (matches_pattern(pattern, filename, extension)) {
                    return format_uri(rule.uri_format, path.string());
                }
            }
        }

        return std::nullopt;
    }

    void FiletypeHandler::add_rule(const FiletypeRule& rule) {
        rules_.push_back(rule);
        save_rules();
    }

    void FiletypeHandler::update_rule(size_t index, const FiletypeRule& rule) {
        if (index >= rules_.size()) return;
        rules_[index] = rule;
        save_rules();
    }

    void FiletypeHandler::delete_rule(size_t index) {
        if (index >= rules_.size()) return;
        rules_.erase(rules_.begin() + static_cast<std::ptrdiff_t>(index));
        save_rules();
    }

    void FiletypeHandler::move_rule(size_t index, int direction) {
        if (index >= rules_.size()) return;
        auto target = static_cast<std::ptrdiff_t>(index) + direction;
        if (target < 0 || target >= static_cast<std::ptrdiff_t>(rules_.size())) return;
        std::swap(rules_[index], rules_[static_cast<size_t>(target)]);
        save_rules();
    }

    void FiletypeHandler::reset_to_defaults() {
        setup_default_rules();
    }

} // namespace rouen::helpers
