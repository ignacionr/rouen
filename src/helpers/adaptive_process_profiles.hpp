#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <openssl/evp.h>

#include "config_service.hpp"
#include "glaze_include.hpp"
#include "platform_utils.hpp"

namespace rouen::helpers {

struct adaptive_process_profile {
    std::string title;
    std::string command;
    std::string scriptPath;
    std::string sha256;
};

struct adaptive_process_profiles_doc {
    bool allowArbitraryCommands{true};
    std::map<std::string, adaptive_process_profile> profiles;
};

class adaptive_process_profiles {
public:
    struct resolution_result {
        bool allowed{true};
        std::string resolved_command;
        std::string resolved_title;
        std::string error_message;
    };

    static adaptive_process_profiles& instance() {
        static adaptive_process_profiles inst;
        return inst;
    }

    bool load_from_json(std::string_view json_str) {
        adaptive_process_profiles_doc doc{};
        auto err = glz::read<glz::opts{.error_on_unknown_keys = false}>(doc, json_str);
        if (err) {
            return false;
        }
        std::lock_guard<std::mutex> const lock(mutex_);
        doc_ = std::move(doc);
        explicitly_loaded_ = true;
        return true;
    }

    bool load_from_file(std::filesystem::path const& path) {
        std::ifstream ifs(path);
        if (!ifs) {
            return false;
        }
        std::stringstream ss;
        ss << ifs.rdbuf();
        return load_from_json(ss.str());
    }

    void reset() {
        std::lock_guard<std::mutex> const lock(mutex_);
        doc_ = adaptive_process_profiles_doc{};
        explicitly_loaded_ = false;
    }

    [[nodiscard]] bool is_allowlist_only() {
        ensure_loaded();
        std::lock_guard<std::mutex> const lock(mutex_);
        return is_allowlist_only_unlocked();
    }

    [[nodiscard]] resolution_result resolve(std::string_view locator) {
        ensure_loaded();
        std::lock_guard<std::mutex> const lock(mutex_);

        std::string const key{locator};
        auto it = doc_.profiles.find(key);
        if (it != doc_.profiles.end()) {
            auto const& prof = it->second;
            if (!prof.sha256.empty()) {
                std::filesystem::path target_file = !prof.scriptPath.empty()
                    ? std::filesystem::path(prof.scriptPath)
                    : extract_script_path_from_command(prof.command);
                std::string const digest = compute_file_sha256(target_file);
                if (!equals_ignore_case(digest, prof.sha256)) {
                    return resolution_result{
                        .allowed = false,
                        .resolved_command = {},
                        .resolved_title = prof.title,
                        .error_message = std::format(
                            "Command rejected: SHA-256 integrity check failed for profile '{}' ({})",
                            key, target_file.string())
                    };
                }
            }
            return resolution_result{
                .allowed = true,
                .resolved_command = prof.command,
                .resolved_title = prof.title.empty() ? key : prof.title,
                .error_message = {}
            };
        }

        if (is_allowlist_only_unlocked()) {
            return resolution_result{
                .allowed = false,
                .resolved_command = {},
                .resolved_title = {},
                .error_message = "Command rejected: Edge node is in allowlist-only mode (ISA/IEC 62443 lockdown)"
            };
        }

        return resolution_result{
            .allowed = true,
            .resolved_command = key,
            .resolved_title = key,
            .error_message = {}
        };
    }

    [[nodiscard]] std::string to_catalog_json() {
        ensure_loaded();
        std::lock_guard<std::mutex> const lock(mutex_);
        glz::json_t profiles_arr = glz::json_t::array_t{};
        auto& arr = profiles_arr.get<glz::json_t::array_t>();
        for (auto const& [name, prof] : doc_.profiles) {
            glz::json_t item = glz::json_t::object_t{};
            item["name"] = name;
            item["title"] = prof.title.empty() ? name : prof.title;
            item["uri"] = std::format("adaptive-process:{}", name);
            item["hasSha256"] = !prof.sha256.empty();
            arr.push_back(std::move(item));
        }
        glz::json_t root = glz::json_t::object_t{};
        root["allowArbitraryCommands"] = !is_allowlist_only_unlocked();
        root["profiles"] = std::move(profiles_arr);
        std::string out;
        (void)glz::write_json(root, out);
        return out;
    }

    [[nodiscard]] static std::string compute_file_sha256(std::filesystem::path const& filepath) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file) {
            return {};
        }
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (!ctx) {
            return {};
        }
        if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
            EVP_MD_CTX_free(ctx);
            return {};
        }
        std::array<char, 8192> buf{};
        while (file.good()) {
            file.read(buf.data(), static_cast<std::streamsize>(buf.size()));
            auto const n = file.gcount();
            if (n > 0) {
                if (EVP_DigestUpdate(ctx, buf.data(), static_cast<size_t>(n)) != 1) {
                    EVP_MD_CTX_free(ctx);
                    return {};
                }
            }
        }
        std::array<unsigned char, EVP_MAX_MD_SIZE> hash{};
        unsigned int hash_len = 0;
        if (EVP_DigestFinal_ex(ctx, hash.data(), &hash_len) != 1) {
            EVP_MD_CTX_free(ctx);
            return {};
        }
        EVP_MD_CTX_free(ctx);

        std::string hex;
        hex.reserve(hash_len * 2);
        for (unsigned int i = 0; i < hash_len; ++i) {
            hex += std::format("{:02x}", hash[i]);
        }
        return hex;
    }

private:
    adaptive_process_profiles() = default;

    void ensure_loaded() {
        std::lock_guard<std::mutex> const lock(mutex_);
        if (explicitly_loaded_) {
            return;
        }
        auto cfg = ConfigService::instance();
        std::string const custom_path = cfg->get_env("ADAPTIVE_PROCESS_PROFILES_PATH");
        std::vector<std::filesystem::path> candidates;
        if (!custom_path.empty()) {
            candidates.emplace_back(custom_path);
        }
        candidates.push_back(rouen::platform::get_user_config_directory() / "adaptive_process_profiles.json");
        candidates.push_back(std::filesystem::current_path() / "adaptive_process_profiles.json");

        for (auto const& p : candidates) {
            std::error_code ec;
            if (std::filesystem::exists(p, ec)) {
                std::ifstream ifs(p);
                if (ifs) {
                    std::stringstream ss;
                    ss << ifs.rdbuf();
                    adaptive_process_profiles_doc parsed{};
                    if (!glz::read<glz::opts{.error_on_unknown_keys = false}>(parsed, ss.str())) {
                        doc_ = std::move(parsed);
                        break;
                    }
                }
            }
        }
        explicitly_loaded_ = true;
    }

    [[nodiscard]] bool is_allowlist_only_unlocked() const {
        if (!doc_.allowArbitraryCommands) {
            return true;
        }
        auto cfg = ConfigService::instance();
        std::string const env_val = cfg->get_env("ADAPTIVE_PROCESS_ALLOWLIST_ONLY");
        return env_val == "1" || equals_ignore_case(env_val, "true") || equals_ignore_case(env_val, "yes");
    }

    [[nodiscard]] static bool equals_ignore_case(std::string_view a, std::string_view b) {
        if (a.size() != b.size()) {
            return false;
        }
        for (size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] static std::filesystem::path extract_script_path_from_command(std::string_view cmd) {
        // Check tokens in command for an existing file path or .py/.sh/.js token
        std::istringstream iss{std::string(cmd)};
        std::string token;
        std::vector<std::string> tokens;
        while (iss >> token) {
            if (!token.empty() && (token.front() == '"' || token.front() == '\'')) {
                token.erase(0, 1);
            }
            if (!token.empty() && (token.back() == '"' || token.back() == '\'')) {
                token.pop_back();
            }
            tokens.push_back(token);
        }
        for (size_t i = 1; i < tokens.size(); ++i) {
            if (!tokens[i].starts_with("-")) {
                return std::filesystem::path(tokens[i]);
            }
        }
        return tokens.empty() ? std::filesystem::path{} : std::filesystem::path(tokens.front());
    }

    std::mutex mutex_;
    adaptive_process_profiles_doc doc_{};
    bool explicitly_loaded_{false};
};

} // namespace rouen::helpers

template <>
struct glz::meta<rouen::helpers::adaptive_process_profile> {
    using T = rouen::helpers::adaptive_process_profile;
    static constexpr auto values = glz::object(
        "title", &T::title,
        "command", &T::command,
        "scriptPath", &T::scriptPath,
        "sha256", &T::sha256
    );
    static constexpr auto options = glz::opts{.error_on_unknown_keys = false};
};

template <>
struct glz::meta<rouen::helpers::adaptive_process_profiles_doc> {
    using T = rouen::helpers::adaptive_process_profiles_doc;
    static constexpr auto values = glz::object(
        "allowArbitraryCommands", &T::allowArbitraryCommands,
        "profiles", &T::profiles
    );
    static constexpr auto options = glz::opts{.error_on_unknown_keys = false};
};
