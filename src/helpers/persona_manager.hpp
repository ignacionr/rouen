#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <unordered_set>
#include <functional>
#include <optional>
#include "glaze_include.hpp"
#include "platform_utils.hpp"
#include "../registrar.hpp"

namespace rouen::helpers {

    struct Persona {
        std::string name;
        std::string description;
        std::vector<std::string> allowed_mcps;
        std::string system_prompt;
        std::string llm_config_name;
        bool enable_search{false};
        std::vector<std::string> allowed_personas;
        float temperature{0.7f};
        std::string thinking_level{};
        int max_tool_iterations{10};
        int max_output_tokens{8192};
        std::string context_compaction_strategy{"summarize"}; // "none", "sliding_window", "summarize"
        int max_context_turns{30};
        int preserve_recent_turns{10};

        [[nodiscard]] std::string get_effective_thinking_level() const {
            if (!thinking_level.empty()) {
                return thinking_level;
            }
            if (temperature <= 0.15f) return "minimal";
            if (temperature <= 0.35f) return "low";
            if (temperature >= 0.7f) return "high";
            return "medium";
        }

        struct glaze {
            using T = Persona;
            static constexpr auto value = glz::object(
                "name", &T::name,
                "description", &T::description,
                "allowed_mcps", &T::allowed_mcps,
                "system_prompt", &T::system_prompt,
                "llm_config_name", &T::llm_config_name,
                "enable_search", &T::enable_search,
                "allowed_personas", &T::allowed_personas,
                "temperature", &T::temperature,
                "thinking_level", &T::thinking_level,
                "max_tool_iterations", &T::max_tool_iterations,
                "max_output_tokens", &T::max_output_tokens,
                "context_compaction_strategy", &T::context_compaction_strategy,
                "max_context_turns", &T::max_context_turns,
                "preserve_recent_turns", &T::preserve_recent_turns
            );
        };
    };

    struct PersonaSaveModel {
        size_t active_index{0};
        std::string progress_monitor_persona{"Progress Monitor"};
        std::vector<Persona> personas;

        struct glaze {
            using T = PersonaSaveModel;
            static constexpr auto value = glz::object(
                "active_index", &T::active_index,
                "progress_monitor_persona", &T::progress_monitor_persona,
                "personas", &T::personas
            );
        };
    };

    struct PersonaActiveMeta {
        std::string active_persona;
        size_t active_index{0};
        std::string progress_monitor_persona{"Progress Monitor"};

        struct glaze {
            using T = PersonaActiveMeta;
            static constexpr auto value = glz::object(
                "active_persona", &T::active_persona,
                "active_index", &T::active_index,
                "progress_monitor_persona", &T::progress_monitor_persona
            );
        };
    };

    struct PersonaManifest {
        std::vector<std::string> personas;

        struct glaze {
            using T = PersonaManifest;
            static constexpr auto value = glz::object(
                "personas", &T::personas
            );
        };
    };

    class PersonaManager {
    public:
        using sync_hook_t = std::function<void(std::string_view dataset, std::string_view key, std::string_view content, bool is_deleted)>;

        static PersonaManager& instance() {
            static PersonaManager mgr;
            return mgr;
        }

        static std::optional<Persona> load_persona_from_resource_files(
            const std::filesystem::path& json_path,
            const std::filesystem::path& md_path = {}
        ) {
            if (!std::filesystem::exists(json_path) || !std::filesystem::is_regular_file(json_path)) {
                return std::nullopt;
            }
            std::ifstream jf(json_path);
            if (!jf.is_open()) {
                return std::nullopt;
            }
            std::string json_content((std::istreambuf_iterator<char>(jf)), std::istreambuf_iterator<char>());
            Persona p{};
            auto err = glz::read<glz::opts{.error_on_unknown_keys = false}>(p, json_content);
            if (err || p.name.empty()) {
                return std::nullopt;
            }

            if (p.system_prompt.empty()) {
                std::filesystem::path prompt_path = md_path;
                if (prompt_path.empty()) {
                    prompt_path = json_path;
                    prompt_path.replace_extension(".md");
                }
                if (std::filesystem::exists(prompt_path) && std::filesystem::is_regular_file(prompt_path)) {
                    std::ifstream mf(prompt_path);
                    if (mf.is_open()) {
                        p.system_prompt.assign(
                            std::istreambuf_iterator<char>(mf),
                            std::istreambuf_iterator<char>()
                        );
                        while (!p.system_prompt.empty() &&
                               (p.system_prompt.back() == '\n' || p.system_prompt.back() == '\r')) {
                            p.system_prompt.pop_back();
                        }
                    }
                }
            }
            return p;
        }

        static std::vector<Persona> load_default_personas_from_resources() {
            std::vector<Persona> result;
            try {
                auto manifest_path = rouen::platform::get_resource_path("manifest.json", "personas");
                if (!std::filesystem::exists(manifest_path)) {
                    return result;
                }
                std::ifstream mf(manifest_path);
                if (!mf.is_open()) {
                    return result;
                }
                std::string manifest_content((std::istreambuf_iterator<char>(mf)), std::istreambuf_iterator<char>());
                PersonaManifest manifest{};
                if (glz::read<glz::opts{.error_on_unknown_keys = false}>(manifest, manifest_content)) {
                    return result;
                }

                auto dir = manifest_path.parent_path();
                result.reserve(manifest.personas.size());
                for (const auto& slug : manifest.personas) {
                    auto json_path = dir / (slug + ".json");
                    auto md_path = dir / (slug + ".md");
                    if (!std::filesystem::exists(json_path)) {
                        json_path = rouen::platform::get_resource_path(slug + ".json", "personas");
                    }
                    if (!std::filesystem::exists(md_path)) {
                        md_path = rouen::platform::get_resource_path(slug + ".md", "personas");
                    }
                    if (auto loaded = load_persona_from_resource_files(json_path, md_path); loaded.has_value()) {
                        result.push_back(std::move(*loaded));
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "[PersonaManager] load_default_personas_from_resources error: " << e.what() << std::endl;
            }
            return result;
        }

        static std::string slugify(std::string_view text) {
            std::string slug;
            slug.reserve(text.size());

            bool last_dash = false;
            for (char c : text) {
                if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
                    slug.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                    last_dash = false;
                } else if (!last_dash) {
                    slug.push_back('-');
                    last_dash = true;
                }
            }

            while (!slug.empty() && slug.front() == '-') {
                slug.erase(slug.begin());
            }
            while (!slug.empty() && slug.back() == '-') {
                slug.pop_back();
            }

            return slug.empty() ? "persona" : slug;
        }

        static Persona create_default_progress_monitor_persona() {
            Persona p{};
            p.name = "Progress Monitor";
            p.description = "Fast, lightweight observer persona that summarizes in-flight task progress and estimates remaining completion time.";
            p.allowed_mcps = {};
            p.system_prompt =
                "You are the Rouen AI Progress Monitor. Your sole responsibility is to inspect an in-flight user request, "
                "recent conversation context, and live execution telemetry (elapsed seconds, completed internal turns, "
                "active and finished tool or persona calls) and produce a concise progress update with an approximate remaining duration.\n\n"
                "Respond ONLY with a valid JSON object matching this exact schema:\n"
                "{\n"
                "  \"summary\": \"<1-2 sentence user-friendly summary of current progress and what is happening right now>\",\n"
                "  \"current_activity\": \"<short label for current step or tool>\",\n"
                "  \"estimated_remaining_seconds\": <integer approximate seconds remaining until completion, or -1 if unknown>,\n"
                "  \"confidence\": \"<low|medium|high>\"\n"
                "}";
            p.llm_config_name = "Gemini Flash Lite";
            p.enable_search = false;
            p.allowed_personas = {};
            p.temperature = 0.1f;
            p.thinking_level = "minimal";
            p.max_tool_iterations = 0;
            p.max_output_tokens = 512;
            p.context_compaction_strategy = "none";
            p.max_context_turns = 10;
            p.preserve_recent_turns = 6;
            return p;
        }

        void set_sync_hook(sync_hook_t hook) {
            sync_hook_ = std::move(hook);
        }

        const std::vector<Persona>& get_personas() const {
            return personas_;
        }

        size_t get_active_persona_index() const {
            return active_persona_index_;
        }

        const Persona& get_active_persona() const {
            if (active_persona_index_ < personas_.size()) {
                return personas_[active_persona_index_];
            }
            static Persona fallback{"Default Assistant", "Fallback persona", {"terminal", "editor", "deck", "adaptive_card", "wikipedia", "youtube", "git", "calendar", "weather", "alarm", "pomodoro", "notes", "contacts"}, "You are a helpful assistant.", "Default", false, {}, 0.7f, "high", 10, 8192, "summarize", 30, 10};
            return fallback;
        }

        [[nodiscard]] const std::string& get_progress_monitor_persona() const noexcept {
            return progress_monitor_persona_;
        }

        [[nodiscard]] const std::string& get_progress_monitor_persona_name() const noexcept {
            return progress_monitor_persona_;
        }

        void set_progress_monitor_persona(std::string_view name) {
            if (name.empty()) {
                progress_monitor_persona_ = "Progress Monitor";
            } else if (const auto* existing = get_persona_by_name(name)) {
                progress_monitor_persona_ = existing->name;
            } else {
                progress_monitor_persona_.assign(name);
            }
            save_personas();
            sync_active_persona();
        }

        [[nodiscard]] const Persona& get_progress_monitor_persona_config() const {
            if (!progress_monitor_persona_.empty()) {
                if (const auto* p = get_persona_by_name(progress_monitor_persona_)) {
                    return *p;
                }
            }
            if (const auto* default_pm = get_persona_by_name("Progress Monitor")) {
                return *default_pm;
            }
            static const Persona builtin_pm = create_default_progress_monitor_persona();
            return builtin_pm;
        }

        void select_persona(size_t index) {
            if (index < personas_.size()) {
                active_persona_index_ = index;
                save_personas();
                sync_active_persona();
            }
        }

        [[nodiscard]] std::optional<size_t> find_persona_index(std::string_view name) const {
            for (size_t i = 0; i < personas_.size(); ++i) {
                if (personas_[i].name == name) {
                    return i;
                }
            }
            std::string lower_target;
            lower_target.reserve(name.size());
            for (char c : name) lower_target.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

            for (size_t i = 0; i < personas_.size(); ++i) {
                std::string lower_p;
                lower_p.reserve(personas_[i].name.size());
                for (char c : personas_[i].name) lower_p.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                if (lower_p == lower_target) {
                    return i;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] const Persona* get_persona_by_name(std::string_view name) const {
            auto idx = find_persona_index(name);
            if (idx.has_value() && *idx < personas_.size()) {
                return &personas_[*idx];
            }
            return nullptr;
        }

        bool select_persona_by_name(std::string_view name) {
            auto idx = find_persona_index(name);
            if (idx.has_value()) {
                select_persona(*idx);
                return true;
            }
            return false;
        }

        void add_persona(const Persona& persona) {
            personas_.push_back(persona);
            save_personas();
            sync_persona_item(persona, false);
        }

        void update_persona(size_t index, const Persona& persona) {
            if (index < personas_.size()) {
                std::string old_name = personas_[index].name;
                std::string new_name = persona.name;
                personas_[index] = persona;

                // If name changed, update references in allowed_personas of other personas
                if (old_name != new_name && !old_name.empty()) {
                    Persona old_p;
                    old_p.name = old_name;
                    sync_persona_item(old_p, true /* is_deleted */);

                    if (progress_monitor_persona_ == old_name) {
                        progress_monitor_persona_ = new_name;
                    }

                    for (auto& p : personas_) {
                        for (auto& ref : p.allowed_personas) {
                            if (ref == old_name) {
                                ref = new_name;
                            }
                        }
                    }
                }
                save_personas();
                sync_persona_item(persona, false);
            }
        }

        void delete_persona(size_t index) {
            if (personas_.size() <= 1) {
                // Keep at least one persona
                return;
            }
            if (index < personas_.size()) {
                std::string name_to_remove = personas_[index].name;
                Persona deleted_p = personas_[index];
                personas_.erase(personas_.begin() + static_cast<std::ptrdiff_t>(index));

                // Remove references to deleted persona
                for (auto& p : personas_) {
                    p.allowed_personas.erase(
                        std::remove(p.allowed_personas.begin(), p.allowed_personas.end(), name_to_remove),
                        p.allowed_personas.end()
                    );
                }

                if (progress_monitor_persona_ == name_to_remove) {
                    progress_monitor_persona_ = "Progress Monitor";
                }

                if (active_persona_index_ >= personas_.size()) {
                    active_persona_index_ = personas_.size() - 1;
                }
                save_personas();
                sync_persona_item(deleted_p, true /* is_deleted */);
                sync_active_persona();
            }
        }

        // Export all personas to individual JSON files for Universal Sync
        bool export_to_directory(const std::filesystem::path& dir) const {
            try {
                std::filesystem::create_directories(dir);
                std::unordered_set<std::string> written_files;

                for (const auto& p : personas_) {
                    std::string fname = slugify(p.name) + ".json";
                    written_files.insert(fname);
                    auto path = dir / fname;
                    std::string json_str = glz::write<glz::opts{.prettify = true}>(p).value_or("");
                    if (!json_str.empty()) {
                        std::ofstream file(path);
                        if (file.is_open()) {
                            file << json_str;
                        }
                    }
                }

                PersonaActiveMeta meta;
                if (active_persona_index_ < personas_.size()) {
                    meta.active_persona = personas_[active_persona_index_].name;
                    meta.active_index = active_persona_index_;
                }
                meta.progress_monitor_persona = progress_monitor_persona_.empty() ? "Progress Monitor" : progress_monitor_persona_;
                std::string meta_json = glz::write<glz::opts{.prettify = true}>(meta).value_or("");
                if (!meta_json.empty()) {
                    std::ofstream meta_file(dir / "active.json");
                    if (meta_file.is_open()) {
                        meta_file << meta_json;
                    }
                }
                written_files.insert("active.json");

                // Evict obsolete persona files in sync cache
                if (std::filesystem::exists(dir)) {
                    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                        if (entry.is_regular_file() && entry.path().extension() == ".json") {
                            std::string filename = entry.path().filename().string();
                            if (!written_files.contains(filename)) {
                                std::filesystem::remove(entry.path());
                            }
                        }
                    }
                }
                return true;
            } catch (const std::exception& e) {
                std::cerr << "[PersonaManager] export_to_directory error: " << e.what() << std::endl;
                return false;
            }
        }

        // Import individual persona files from Universal Sync cache directory
        bool import_from_directory(const std::filesystem::path& dir) {
            if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
                return false;
            }

            is_sync_suppressed_ = true;
            struct Guard {
                bool& flag;
                ~Guard() { flag = false; }
            } guard{is_sync_suppressed_};

            try {
                std::vector<Persona> imported_personas;
                std::string active_persona_name;
                std::string imported_monitor_persona;

                for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                    if (!entry.is_regular_file() || entry.path().extension() != ".json") {
                        continue;
                    }
                    std::string filename = entry.path().filename().string();
                    if (filename == "active.json") {
                        std::ifstream f(entry.path());
                        if (f.is_open()) {
                            std::string meta_content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                            PersonaActiveMeta meta{};
                            if (!glz::read<glz::opts{.error_on_unknown_keys = false}>(meta, meta_content)) {
                                active_persona_name = meta.active_persona;
                                imported_monitor_persona = meta.progress_monitor_persona;
                            }
                        }
                        continue;
                    }

                    if (auto loaded = load_persona_from_resource_files(entry.path()); loaded.has_value()) {
                        imported_personas.push_back(std::move(*loaded));
                    }
                }

                if (imported_personas.empty()) {
                    return false;
                }

                // Upsert imported personas
                for (const auto& imp : imported_personas) {
                    bool found = false;
                    for (auto& p : personas_) {
                        if (p.name == imp.name) {
                            p = imp;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        personas_.push_back(imp);
                    }
                }

                // Sync deletions: remove local personas not present in imported list
                // Never delete core built-in personas
                auto is_core_persona = [](std::string_view name) {
                    return name == "Rouen Assistant" || 
                           name == "Autonomous Engineer" || 
                           name == "Code & Git Architect" || 
                           name == "Adaptive Card Architect" || 
                           name == "Persona Architect" || 
                           name == "Progress Monitor" || 
                           name == "Terminal Specialist" || 
                           name == "Editor Specialist" || 
                           name == "Git & GitHub Specialist";
                };
                for (auto it = personas_.begin(); it != personas_.end(); ) {
                    if (is_core_persona(it->name)) {
                        ++it;
                        continue;
                    }
                    bool exists_in_imported = false;
                    for (const auto& imp : imported_personas) {
                        if (imp.name == it->name) {
                            exists_in_imported = true;
                            break;
                        }
                    }
                    if (!exists_in_imported && personas_.size() > 1) {
                        std::string removed_name = it->name;
                        it = personas_.erase(it);
                        for (auto& p : personas_) {
                            p.allowed_personas.erase(
                                std::remove(p.allowed_personas.begin(), p.allowed_personas.end(), removed_name),
                                p.allowed_personas.end()
                            );
                        }
                    } else {
                        ++it;
                    }
                }

                // Restore active persona
                if (!active_persona_name.empty()) {
                    for (size_t i = 0; i < personas_.size(); ++i) {
                        if (personas_[i].name == active_persona_name) {
                            active_persona_index_ = i;
                            break;
                        }
                    }
                }
                if (active_persona_index_ >= personas_.size()) {
                    active_persona_index_ = 0;
                }
                if (!imported_monitor_persona.empty()) {
                    progress_monitor_persona_ = imported_monitor_persona;
                }

                sanitize_and_migrate_personas();
                export_to_directory(dir);
                save_personas();
                return true;
            } catch (const std::exception& e) {
                std::cerr << "[PersonaManager] import_from_directory error: " << e.what() << std::endl;
                return false;
            }
        }

        void reload() {
            load_personas();
        }

    private:
        PersonaManager() {
            setup_default_personas();
            load_personas();
        }

        ~PersonaManager() = default;

        void setup_default_personas() {
            personas_ = load_default_personas_from_resources();
            if (personas_.empty()) {
                Persona fallback{};
                fallback.name = "Rouen Assistant";
                fallback.description = "Primary orchestrator persona for Rouen.";
                fallback.allowed_mcps = {"deck", "persona", "calendar", "notes", "contacts", "terminal", "git", "editor", "rss", "wikipedia", "youtube", "alarm", "pomodoro", "mesh"};
                fallback.system_prompt = "You are Rouen Assistant, the primary coordinator for Rouen.";
                fallback.llm_config_name = "Gemini 3.8 Flash";
                fallback.temperature = 0.1f;
                fallback.thinking_level = "high";
                fallback.max_tool_iterations = 100;
                fallback.max_output_tokens = 16384;
                personas_.push_back(std::move(fallback));
            }
            if (!get_persona_by_name("Progress Monitor")) {
                personas_.push_back(create_default_progress_monitor_persona());
            }
        }

        void load_personas() {
            is_sync_suppressed_ = true;
            struct Guard {
                bool& flag;
                ~Guard() { flag = false; }
            } guard{is_sync_suppressed_};

            try {
                auto path = rouen::platform::get_user_config_directory() / "personas.json";
                if (!std::filesystem::exists(path)) {
                    sanitize_and_migrate_personas();
                    save_personas(); // Save the defaults
                    return;
                }

                std::ifstream file(path);
                if (!file.is_open()) return;

                std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                file.close();

                PersonaSaveModel model;
                auto err = glz::read<glz::opts{.error_on_unknown_keys = false}>(model, content);
                if (err) {
                    std::cerr << "[Persona] Failed to parse personas.json: " << glz::format_error(err, content) << std::endl;
                    return;
                }

                if (!model.personas.empty()) {
                    personas_ = std::move(model.personas);
                }
                
                if (model.active_index < personas_.size()) {
                    active_persona_index_ = model.active_index;
                } else {
                    active_persona_index_ = 0;
                }

                if (!model.progress_monitor_persona.empty()) {
                    progress_monitor_persona_ = model.progress_monitor_persona;
                } else {
                    progress_monitor_persona_ = "Progress Monitor";
                }

                sanitize_and_migrate_personas();
            } 
            catch (const std::exception& e) {
                std::cerr << "[Persona] Exception loading personas: " << e.what() << std::endl;
            }
        }

        void sanitize_and_migrate_personas() {
            bool modified = false;

            // 1. Load authoritative built-in personas from resources/personas/
            std::vector<Persona> default_personas = load_default_personas_from_resources();
            bool has_default_pm = false;
            for (const auto& dp : default_personas) {
                if (dp.name == "Progress Monitor") {
                    has_default_pm = true;
                    break;
                }
            }
            if (!has_default_pm) {
                default_personas.push_back(create_default_progress_monitor_persona());
            }

            // 2. Ensure all built-in personas exist and upgrade their specialization/prompts from resources
            for (const auto& dp : default_personas) {
                Persona* existing = nullptr;
                for (auto& ep : personas_) {
                    if (ep.name == dp.name) {
                        existing = &ep;
                        break;
                    }
                }
                if (!existing) {
                    personas_.push_back(dp);
                    modified = true;
                    continue;
                }

                if (existing->llm_config_name.empty() ||
                    existing->llm_config_name == "Default" ||
                    existing->llm_config_name == "Local MLX" ||
                    existing->llm_config_name == "Gemini Flash" ||
                    existing->llm_config_name != dp.llm_config_name) {
                    existing->llm_config_name = dp.llm_config_name;
                    modified = true;
                }
                if (std::abs(existing->temperature - dp.temperature) > 0.001f) {
                    existing->temperature = dp.temperature;
                    modified = true;
                }
                if (existing->thinking_level != dp.thinking_level) {
                    existing->thinking_level = dp.thinking_level;
                    modified = true;
                }
                if (existing->max_tool_iterations != dp.max_tool_iterations) {
                    existing->max_tool_iterations = dp.max_tool_iterations;
                    modified = true;
                }
                if (existing->max_output_tokens != dp.max_output_tokens) {
                    existing->max_output_tokens = dp.max_output_tokens;
                    modified = true;
                }
                if (existing->description != dp.description && !dp.description.empty()) {
                    existing->description = dp.description;
                    modified = true;
                }
                if (existing->system_prompt != dp.system_prompt && !dp.system_prompt.empty()) {
                    existing->system_prompt = dp.system_prompt;
                    modified = true;
                }
                for (const auto& mcp : dp.allowed_mcps) {
                    if (std::find(existing->allowed_mcps.begin(), existing->allowed_mcps.end(), mcp) == existing->allowed_mcps.end()) {
                        existing->allowed_mcps.push_back(mcp);
                        modified = true;
                    }
                }
                for (const auto& sub : dp.allowed_personas) {
                    if (std::find(existing->allowed_personas.begin(), existing->allowed_personas.end(), sub) == existing->allowed_personas.end()) {
                        existing->allowed_personas.push_back(sub);
                        modified = true;
                    }
                }
            }

            // 3. Sanitize bounds and legacy model names on all personas (including custom ones)
            for (auto& p : personas_) {
                bool const is_progress_monitor = (p.name == "Progress Monitor");
                if (is_progress_monitor) {
                    if (p.max_tool_iterations < 0) {
                        p.max_tool_iterations = 0;
                        modified = true;
                    }
                } else if (p.max_tool_iterations <= 0) {
                    p.max_tool_iterations = 10;
                    modified = true;
                }
                if (p.max_output_tokens <= 0) {
                    p.max_output_tokens = is_progress_monitor ? 512 : 8192;
                    modified = true;
                }
                if (p.max_context_turns <= 0) {
                    p.max_context_turns = 30;
                    modified = true;
                }
                if (p.preserve_recent_turns <= 0) {
                    p.preserve_recent_turns = 10;
                    modified = true;
                }
                if (p.context_compaction_strategy.empty()) {
                    p.context_compaction_strategy = "summarize";
                    modified = true;
                }
                if (p.llm_config_name == "Local MLX" || p.llm_config_name == "Default" || p.llm_config_name.empty()) {
                    p.llm_config_name = is_progress_monitor ? "Gemini Flash Lite" : "Gemini 3.8 Flash";
                    modified = true;
                }
            }

            if (progress_monitor_persona_.empty() || !get_persona_by_name(progress_monitor_persona_)) {
                progress_monitor_persona_ = "Progress Monitor";
                modified = true;
            }

            if (modified) {
                save_personas();
                for (const auto& p : personas_) {
                    sync_persona_item(p, false);
                }
            }
        }

        void notify_sync(std::string_view dataset, std::string_view key, std::string_view content, bool is_deleted) const {
            if (is_sync_suppressed_) {
                return;
            }
            if (sync_hook_) {
                sync_hook_(dataset, key, content, is_deleted);
                return;
            }
            auto hook_fn = registrar::try_get<std::function<bool(std::string_view, std::string_view, std::string_view, bool)>>("universal_sync_item");
            if (hook_fn) {
                (*hook_fn)(dataset, key, content, is_deleted);
            }
        }

        void sync_persona_item(const Persona& persona, bool is_deleted = false) const {
            std::string slug = slugify(persona.name);
            std::string buffer = is_deleted ? "" : glz::write<glz::opts{.prettify = true}>(persona).value_or("");
            notify_sync("personas", slug + ".json", buffer, is_deleted);
        }

        void sync_active_persona() const {
            PersonaActiveMeta meta;
            if (active_persona_index_ < personas_.size()) {
                meta.active_persona = personas_[active_persona_index_].name;
                meta.active_index = active_persona_index_;
            }
            meta.progress_monitor_persona = progress_monitor_persona_.empty() ? "Progress Monitor" : progress_monitor_persona_;
            std::string meta_json = glz::write<glz::opts{.prettify = true}>(meta).value_or("");
            if (!meta_json.empty()) {
                notify_sync("personas", "active.json", meta_json, false);
            }
        }

        void save_personas() const {
            try {
                auto path = rouen::platform::get_user_config_directory() / "personas.json";
                
                PersonaSaveModel model;
                model.active_index = active_persona_index_;
                model.progress_monitor_persona = progress_monitor_persona_.empty() ? "Progress Monitor" : progress_monitor_persona_;
                model.personas = personas_;

                std::string buffer = glz::write<glz::opts{.prettify = true}>(model).value_or("");
                if (!buffer.empty()) {
                    std::ofstream file(path);
                    if (file.is_open()) {
                        file << buffer;
                    }
                    notify_sync("config", "personas.json", buffer, false);
                }
            } 
            catch (const std::exception& e) {
                std::cerr << "[Persona] Exception saving personas: " << e.what() << std::endl;
            }
        }

        std::vector<Persona> personas_;
        size_t active_persona_index_{0};
        std::string progress_monitor_persona_{"Progress Monitor"};
        mutable sync_hook_t sync_hook_;
        mutable bool is_sync_suppressed_{false};
    };

}
